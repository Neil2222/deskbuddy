# Firmware packages

How DeskBuddy firmware is versioned, packaged and signed. The Companion will install a package only after
checking it completely (`companion/DeskBuddy.Firmware`). This page covers the package format and how it is checked.
The Firmware Manager that writes a package to a board over USB does not exist yet. Its flashing backend is
`esptool` v4.12.0, recommended in the [comparison](#flashing-backend-decision) below and still waiting for approval.

**Not yet physically validated:** installing on a factory-new board. Only one board exists, and it is the
working DeskBuddy. Nothing in this page has been written to a board yet.

## Versions

- **Firmware version:** Semantic Versioning in `firmware/deskbuddy/version.txt`, for example `1.0.0`.
  - ESP-IDF puts it in the app description, and the board reports it as `hello fw=` and `info fw=`.
  - Pre-releases are written like `1.1.0-rc.1`. There is no build metadata; the commit is reported separately.
  - Precedence follows SemVer, so `1.0.0-rc.1` < `1.0.0` < `1.0.1` < `1.10.0`.
- **Build:** the full Git commit, recorded when CMake configures (`firmware/build_commit.cmake`) and reported as
  `info build=`.
  - A build with uncommitted changes to tracked files is reported as `<commit>-dirty`.
  - Packages are never made from such builds.
- **Firmware from before versioning** reports a short commit (`fw=57eb475`) and has no `info`. The update policy
  treats it as older than any version.
- **Companion version:** `<Version>` in `DeskBuddy.Companion.csproj`, currently 1.1.0. The SDK appends `+<commit>`.
  Each package states the oldest Companion that may install it (`minCompanion`).
- **Protocol:** `LINK_PROTO` (1). A package for another protocol version is never offered.

**Release rule:** bump `version.txt` in its own commit, then package that commit. Every released version is a
new version; a package is never rebuilt under an old number.

## Reproducible builds

`CONFIG_APP_REPRODUCIBLE_BUILD=y` (sdkconfig.defaults) removes the build date and time and the local paths from
the binaries. The same commit therefore gives byte-identical images, whatever directory it is built in.
`tools/package-firmware.ps1 -CheckReproducible` builds twice in different directories and compares the three
images.

The sdkconfig in `firmware/deskbuddy` is regenerated from `sdkconfig.defaults` only for options it does not
already have. Delete it, or run `idf.py fullclean`, before a development build is expected to match a package.
The packaging script always uses a fresh sdkconfig of its own.

## Making a package

```powershell
tools\package-firmware.ps1                   # dist\firmware\deskbuddy-<version>-waveshare-esp32-s3-touch-lcd-7b.zip, unsigned
tools\package-firmware.ps1 -Key <private.pem> # signed, then read back with that key
tools\package-firmware.ps1 -CheckReproducible
```

The script:
1. Refuses a working tree with changes or untracked files.
2. Builds the firmware in a new directory under `%TEMP%`, with an sdkconfig made from `sdkconfig.defaults`.
3. Checks that the build changed no tracked file (for example `dependencies.lock`).
4. Runs `deskbuddy-firmware-tool pack`, which:
   - reads `flasher_args.json` for which file goes where;
   - takes the version and ESP-IDF version from the app description;
   - writes the manifest;
   - puts the result through the same checks the Companion makes.

It flashes nothing and opens no serial port. `dist/` is ignored by Git.

The tool on its own (`companion/DeskBuddy.FirmwareTool`):

```
deskbuddy-firmware-tool pack   --build <dir> --commit <sha> --protocol <n> --min-companion <version> --out <dir>
deskbuddy-firmware-tool sign   --package <zip> --key <private.pem>
deskbuddy-firmware-tool verify --package <zip> --public-key <public.pem> [--public-key ...]
deskbuddy-firmware-tool keygen --private <file> --public <file>
```

## The package

A zip with exactly these files, and no folders:

| File | Content |
|---|---|
| `manifest.json` | what the package is (below) |
| `manifest.sig` | ECDSA P-256 / SHA-256 signature over the exact bytes of `manifest.json`, DER-encoded |
| `bootloader.bin` | written at 0x0 |
| `partition-table.bin` | written at 0x8000 |
| `deskbuddy.bin` | written at 0x10000 |

```json
{
  "schema": 1,
  "product": "deskbuddy",
  "version": "1.0.0",
  "build": "<40 hex>",
  "board": "waveshare-esp32-s3-touch-lcd-7b",
  "chip": "esp32s3",
  "flash": { "size": "16MB", "mode": "dio", "freq": "80m" },
  "protocol": 1,
  "minCompanion": "1.1.0",
  "idf": "v5.5.5",
  "images": [
    { "name": "bootloader", "offset": "0x0", "file": "bootloader.bin", "size": 22272, "sha256": "…" },
    { "name": "partition-table", "offset": "0x8000", "file": "partition-table.bin", "size": 3072, "sha256": "…" },
    { "name": "app", "offset": "0x10000", "file": "deskbuddy.bin", "size": 554688, "sha256": "…" }
  ],
  "preserve": [
    { "name": "nvs", "offset": "0x9000", "size": "0x6000" },
    { "name": "phy_init", "offset": "0xf000", "size": "0x1000" }
  ]
}
```

The images are written as they are. Their headers already say DIO, 80 MHz and 16 MB, so the flashing tool must
not patch them (esptool: `--flash_mode keep --flash_freq keep --flash_size keep`). The bytes on the flash are then
the bytes the manifest hashes.

## What the Companion checks

`FirmwarePackage.Open` returns a package only when all of these pass, in this order. Anything else is refused
with a reason (`PackageError`).

1. **A flat zip:** at most 8 entries, no paths, no duplicate names, at most 16 MB in total.
2. **Signature:** `manifest.sig` must verify the exact bytes of `manifest.json` with a key this Companion trusts.
   An unsigned package, another key, a changed manifest or a damaged signature are all refused.
3. **Manifest:** schema 1. Every field is present, no field is unknown, and each value has one spelling
   (lower-case hex, a 40-digit commit, a real version).
4. **Board:** a board this Companion knows. The chip and flash settings must be that board's.
5. **Regions:** exactly bootloader at 0x0, partition table at 0x8000 and app at 0x10000, each within its region.
   - The regions come from `BoardProfile` in the Companion, not from the package.
   - No write may reach the preserved regions, counted in whole 4 KB sectors because flash erases sectors.
6. **Files:** exactly the manifest's files. Each must have the size and SHA-256 the manifest names.
7. **Images:**
   - The bootloader and app are valid ESP images: segments, checksum, and appended SHA-256.
   - Both are for the ESP32-S3 and the board's flash settings.
   - The partition table's MD5 is valid and its layout is exactly `nvs 0x9000+0x6000`, `phy_init 0xf000+0x1000`,
     `factory 0x10000+0x300000`. Any other layout is refused; changing it needs its own approved migration.
8. **Build:** the app description names `deskbuddy`, the manifest's version and ESP-IDF version. The app contains
   the manifest's commit, with nothing after it, so a `-dirty` build cannot pass.

**Update policy (`UpdatePolicy`):**
- A package is never offered to another board, or to a board whose own check says `hw=mismatch`.
- It is never offered if it needs a newer Companion or another protocol.
- A newer version is an update; the same version is a reinstall.
- An older version is a downgrade, only offered on an explicit request with its own confirmation.
- Firmware from before versioning is always updated.

**First-time installation:** the board has no DeskBuddy to report a board or check, so the policy offers an
install. It will only go ahead once the board has been identified as an ESP32-S3 with 8 MB PSRAM and 16 MB flash
and the user has confirmed the exact model. Those signals do not tell the 7B apart from other Waveshare boards with
the same module. This path is not physically validated: there is no factory-new board to test it on.

## What a package never touches

- **0x9000–0xFFFF:** the `nvs` and `phy_init` partitions. They are the same on the factory firmware and hold that
  board's calibration data. DeskBuddy itself stores nothing on the board.
- **Everything after the app:** the rest of the 16 MB, including what is left of the factory app on a board that
  had it.
- **eFuses:** never. Erasing the whole flash is never done either.
- **The PC:** the Companion's data (`%APPDATA%\DeskBuddy`: Schedule, History, settings, PDC) is not part of a
  package and is not changed by installing one.

## Signing

**Algorithm:** ECDSA over P-256 with SHA-256. The signature is DER-encoded (RFC 3279), as `openssl dgst -sha256
-sign` writes it. The public key is a PEM `PUBLIC KEY` (SubjectPublicKeyInfo).

**Tests:** the automated tests make new key pairs at every run (`PackageSignature.CreateKeyPair`). No test key is
stored in the repository.

**There is no production key yet.** None has been created and the Companion trusts none, so it would refuse
every package. When the Firmware Manager is released, the production process is as follows:

1. **Create the key offline,** on a machine that is not used for daily work, or on a removable drive:
   ```
   deskbuddy-firmware-tool keygen --private deskbuddy-firmware-2026.pem --public deskbuddy-firmware-2026.pub.pem
   ```
   `openssl ecparam -name prime256v1 -genkey -noout | openssl pkcs8 -topk8 -nocrypt` makes the same kind of key.
2. **Keep the private key out of Git.** `*.pem` is already ignored. Store it encrypted, with two offline copies
   (for example a password-manager attachment and an encrypted USB stick). Never put it in CI.
3. **Commit only the public key,** to be embedded in the Companion as its trusted key. Name it after the key so a
   second one can be added later.
4. **Sign each release offline:**
   ```
   tools\package-firmware.ps1                      # on the build machine: the unsigned package
   deskbuddy-firmware-tool sign --package <zip> --key <private.pem>   # where the key is
   deskbuddy-firmware-tool verify --package <zip> --public-key <public.pem>
   ```
   Equivalent with openssl: extract `manifest.json`, run
   `openssl dgst -sha256 -sign <private.pem> -out manifest.sig manifest.json`, and add `manifest.sig` to the zip.
5. **Rotating or revoking a key:** release a Companion that trusts the new key alongside the old, sign from then on
   with the new one, and later release a Companion without the old key.
   - A leaked key is removed in the next Companion release.
   - Packages signed with it are refused from then on.

## Distribution (later)

- **Offline:** the Companion will open a package file the user selects, or one bundled with it. No network
  is needed.
- **GitHub Releases (Milestone B):** the source repository stays private. Packages will be published in a
  separate public repository that holds only release assets: the package zip, its SHA-256, and release notes. No
  source or build files go there.
  - The Companion would ask that repository's Releases API for the latest release.
  - It would download over HTTPS.
  - It would then run exactly the same `FirmwarePackage.Open` checks. The download location adds no trust; the
    signature is what counts.

## Flashing backend decision

Comparison for this board's required operations, done 2026-10-18 from the release archives and source; no serial
port was touched:

| | esptool v4.12.0 | esptool v5.6.0 | espflash v4.6.0 |
|---|---|---|---|
| Windows artifact | `esptool.exe` 10.9 MB, PyInstaller onefile, Authenticode-signed by Espressif | 14.5 MB, same packaging, signed | `espflash.exe` 17.1 MB, unsigned, needs the VC++ runtime |
| License | GPL-2.0-or-later (separate process; ship license and source) | same | MIT OR Apache-2.0 |
| Three regions, byte-exact, one session | yes (`write_flash --flash_* keep`) | yes | one `write-bin` per region, so three connect cycles |
| Chip identity (PSRAM size, flash ID) | `chip_id` / `flash_id` | yes | no PSRAM size and no flash ID on the S3 |
| Progress when piped | plain lines (`Writing at 0x… (N %)`) | rich UI, not verified piped | progress bar, hidden or not machine-readable [inference] |
| Verification | MD5 after every write, `verify_flash` | same | MD5 on write, no separate verify |
| Used on this board | every flash, backup and restore so far (through ESP-IDF 5.5.5) | never | never |

**Recommendation:** esptool v4.12.0, shipping only `esptool.exe`. espflash has the simpler license, but it cannot
identify the board's PSRAM and flash, and it cannot write the three regions in one session. Its reliability for the
required operations is therefore not comparable. Implementation waits for approval.
