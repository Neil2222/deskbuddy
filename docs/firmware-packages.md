# Firmware packages

How DeskBuddy firmware is versioned, packaged and signed. The Companion will install a package only after
checking it completely (`companion/DeskBuddy.Firmware`). This page covers the package format, how it is checked, and
how the Firmware Manager writes it to a board over USB ([Installing](#installing-the-firmware-manager)), with
Espressif's esptool v4.12.0 ([decision](#flashing-backend-decision)).

**Validation status.** Only one board exists, and it is the working DeskBuddy; no test may restore the factory
firmware on it.

| Operation | Status |
|---|---|
| Identify | **Physically tested** on the working board (18 Oct 2026, Companion 1.1.0 test build): chip, PSRAM, flash and MAC read; DeskBuddy reconnected 3 s after the reset |
| Update (app only) | **Physically tested** on the working board (18 Oct 2026): `57eb475` → 1.0.0 (`28c96c5`) with full backup, bootloader and partition table check, write of 0x10000 only, read-back verify, reconnect with `hw=ok` |
| Install | Automated tests against a simulated board and esptool only |
| Repair (full reinstall) | Automated tests against a simulated board and esptool only |
| Install on a factory-new Waveshare board | **Not physically validated** (no factory-new board to test on) |

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
    { "name": "bootloader", "offset": "0x0", "file": "bootloader.bin", "size": 22208, "sha256": "…" },
    { "name": "partition-table", "offset": "0x8000", "file": "partition-table.bin", "size": 3072, "sha256": "…" },
    { "name": "app", "offset": "0x10000", "file": "deskbuddy.bin", "size": 554688, "sha256": "…" }
  ],
  "preserve": [
    { "name": "nvs", "offset": "0x9000", "size": "0x6000" },
    { "name": "phy_init", "offset": "0xf000", "size": "0x1000" }
  ],
  "compatibleBootloaders": [
    { "sha256": "…", "note": "deskbuddy_02c11c8_full_16MB.bin (ESP-IDF v5.5.5, built Oct 15 2026 19:58:21)" }
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

**The production key** (provisioned 2026-10-18): `deskbuddy-firmware-2026`, SHA-256 fingerprint
`00ecd57e1e562ee38a99595b5795f85b92c28281a84cea2987edf40c37e34c44`.
- **Fingerprint checks:** the tool, a SHA-256 of the PEM's DER, and `openssl pkey -pubin -outform DER | openssl dgst
  -sha256` all give the same value.
- **Public half:** the only trusted key, in `companion/DeskBuddy.Companion/Firmware/TrustedKeys/`.
- **Private half:** in `%USERPROFILE%\DeskBuddy-signing\`, outside any repository. Its permissions are not
  inherited: only that Windows user and SYSTEM can open the folder.

A key is trusted only by being built in. It is never trusted from a file next to the exe, from an environment
variable, or because it signed the package being opened, and a newly generated key is never trusted automatically.

The private key cannot end up anywhere it should not:
- **Git:** it lives outside the working tree, and `*.pem` is ignored besides.
  `deskbuddy-firmware-tool keygen` and `sign` refuse a key path inside any Git working tree.
- **Packages:** a package holds only `manifest.json`, `manifest.sig` and the three images; anything else is refused
  when it is written and when it is opened.
- **The Companion:** the build fails if `Firmware/TrustedKeys` holds a private key, and so does the Companion when
  it starts.

The key is not encrypted at rest. Its protection is the folder permissions and the Windows account. Moving it to
encrypted offline storage (an encrypted USB stick, a password manager), and keeping it only there, is recommended
once the first release is signed.

The procedure, for this key and any later one:

1. **Create the key offline,** on a machine that is not used for daily work, or on a removable drive:
   ```
   deskbuddy-firmware-tool keygen --private deskbuddy-firmware-2026.pem --public deskbuddy-firmware-2026.pub.pem
   ```
   `openssl ecparam -name prime256v1 -genkey -noout | openssl pkcs8 -topk8 -nocrypt` makes the same kind of key.
2. **Keep the private key out of Git.** Keep it outside any repository (the tool refuses otherwise). Store it
   encrypted, with two offline copies (for example a password-manager attachment and an encrypted USB stick). Never
   put it in CI.
3. **Commit only the public key** as `companion/DeskBuddy.Companion/Firmware/TrustedKeys/deskbuddy-firmware-<year>.pub.pem`.
   Record its SHA-256 fingerprint here and in that folder's README. Rebuild and publish the Companion: from then on
   it trusts that key and no other.
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

## Installing (the Firmware Manager)

The Companion's **Firmware** page (its own entry in the sidebar; the card needs the room, and its log stays visible
during a run): *Open package…*, the board, what may be done, and the run with its progress and result. Nothing
happens without a click and the confirmation dialog.
- **Identify board…** is read-only. esptool `flash_id` reads the chip, PSRAM, flash chip and MAC; the board restarts
  into download mode and back. Nothing is read from the flash and nothing is written. A connected DeskBuddy must
  come back as the same device with the same firmware.
- **An Install never runs over DeskBuddy.** It reads the app header at 0x10000 first (4 KB, read-only) and refuses
  if that is already DeskBuddy, even when the Companion was not connected to it.

| Operation | Writes | When |
|---|---|---|
| **Install** | bootloader 0x0, partition table 0x8000, app 0x10000 | a board without DeskBuddy (its app header is checked); the user confirms the exact model |
| **Update** | the app at 0x10000 only | a connected DeskBuddy whose bootloader and partition table are verified compatible first (below) |
| **Repair (full reinstall)** | all three again | any DeskBuddy, or a board an interrupted run left without one; the model is confirmed and *REPAIR* typed |

**Compatibility for an app-only Update (`BootloaderCheck`).** A DeskBuddy that answers on the serial link only
proves the app runs; it says nothing about the bootloader under it. Before an Update the Companion reads 0x0–0x8FFF
back from the board (read-only). It goes on only if all of these hold:
- the partition table sector holds exactly the package's partition table;
- 0x0 holds a valid ESP image (checksum and SHA-256) for the ESP32-S3 with DIO / 80 MHz / 16 MB;
- that image's SHA-256 is the package's own bootloader, or one the signed manifest lists in `compatibleBootloaders`.

Anything else is refused, with nothing written, and Repair is offered. `tools\package-firmware.ps1
-CompatibleBootloaderFrom <full backup>` lists a bootloader taken from a board's own backup. Every rollback image of
the working board holds the same bootloader: SHA-256 `d5848346…208b`, ESP-IDF v5.5.5, built Oct 15 2026 19:58:21.
The package's own bootloader differs only because reproducible builds drop the date.

**A run, step by step (`FirmwareInstaller`).** Nothing is written before every check has passed.
1. **Take the port:** the Companion closes its connection and holds `Local\DeskBuddy.PortHold`.
   - The Firmware Manager refuses to start if another tool already holds it.
   - `tools\idf.ps1` and `tools\flash-backup.ps1` now refuse when the Companion holds it.
2. **Identify the board** (`flash_id`). Required: ESP32-S3, *Embedded PSRAM 8MB*, 40 MHz crystal, 16 MB flash.
   - For an Update or Repair, the MAC must also be the connected DeskBuddy's `dev=`.
   - This never proves the 7B: the user confirms the model unless DeskBuddy 1.0.0 or newer reported
     `board=waveshare-esp32-s3-touch-lcd-7b hw=ok`.
3. **Full backup** (16 MB `read_flash`, then `verify_flash` against the board, then SHA256SUMS.txt and board.txt).
   - On by default for an existing DeskBuddy; optional for a new board.
   - A failed backup stops the run, and the user decides explicitly.
   - Saved in `Documents\DeskBuddy-backups\fw-<old>-before-<new>-<time>` (`<data dir>\firmware-backups` with
     `DESKBUDDY_DATA_DIR`).
   - `SHA256SUMS.txt` is one GNU line (`<sha256>  <file>`, LF): in that folder `sha256sum -c SHA256SUMS.txt` (Git
     Bash, or Git's `sha256sum.exe` from PowerShell or cmd) prints `OK`; `Get-FileHash -Algorithm SHA256 <file>`
     shows the same hash. `board.txt` (UTF-8, no BOM) has the chip, MAC, the firmware it held and the restore command.
4. **Update only: the bootloader check** above.
5. **Write** (`write_flash --flash_mode keep --flash_freq keep --flash_size keep`, the regions above).
   - The image files come from the verified package and are checked again on disk just before.
   - esptool erases only the sectors each file covers, and checks each region's MD5.
6. **Verify** (`verify_flash`), then a hard reset.
7. **Reconnect:** within 45 s DeskBuddy must answer with the same device, exactly the package's version and build,
   `board=` the 7B, a new boot, and `hw=ok`.

**Every esptool run is checked as it goes.** The chip and MAC are printed right after connecting, before esptool
loads its stub. A different chip or MAC stops the run there, so a board swapped mid-way is never written.
- **Never used:** `erase_flash`, `erase_region`, `--erase-all` and any eFuse tool. The argument builder refuses them.
- **Child process environment:** `ESPTOOL_*` variables are removed, and esptool runs in an empty folder of its own,
  so no `esptool.cfg` can change what it does.
- **Process tree:** esptool runs in a Windows job object, so cancelling (or the Companion exiting) ends the whole
  PyInstaller process tree, and nothing keeps the COM port open. The window refuses to close while a run is in
  progress.

**Failures and recovery.** Each failure has its own message and what to do next:

| Failure | Recovery |
|---|---|
| Port busy | close the other tool |
| No serial data | the UART USB-C port, DIP switch #16 on the ESP32 side, another cable |
| Not in download mode | manual mode: hold BOOT, tap RESET, release BOOT (the card asks before each step) |
| Wrong chip, wrong MAC | connect only the board meant |
| Backup failed | retry, or decide explicitly to go without one |
| Bootloader not compatible | Repair |
| Interrupted while writing | Repair; the ROM bootloader is never written, so this is always recoverable over the UART port |
| Verify failed | run the same operation again |
| No reconnect | press RESET, then Repair or restore the backup |
| Wrong firmware after | Repair |
| Hardware check failed | restore the backup |

Cancelling before the write changes nothing; cancelling while writing asks first and then says a Repair is needed.
While a run has the port, the sidebar's status names the step (*Identifying the board*, *Backing up the flash*,
*Checking the backup*, *Checking the bootloader*, *Flashing firmware*, *Verifying firmware*, *Restarting DeskBuddy*);
only *Flashing firmware* writes. The result, with what to do next and the backup's path, stays on the card.

**Restoring a backup** (recovery outside the Companion, independent of its esptool):
1. Quit the Companion (window X, or *Quit DeskBuddy* in tray mode); nothing else may have the COM port.
2. `tools\flash-backup.ps1 -Action verify -Image <backup>.bin -Port COM6` (optional, read-only: is it on the board already?).
3. `tools\flash-backup.ps1 -Action restore -Image <backup>.bin -Port COM6`: writes the 16 MB image and verifies it.
4. If the board does not enter download mode: hold BOOT, tap RESET, release BOOT, and run it again.

The working board's image from before the first update is
`Documents\DeskBuddy-backups\fw-57eb475-before-1.0.0-20261018-133137\deskbuddy_441bf68d1178_full_16MB.bin`
(SHA-256 `b0bb17e2…42b7`).

**The flashing tool.** `esptool.exe` v4.12.0, Espressif's signed Windows build, pinned by SHA-256 (zip
`42fddc5e…0889`, exe `1cd8386a…d1a3`).
- `tools\fetch-esptool.ps1` downloads and checks it; it is embedded in the Companion.
- The Companion writes it to `%LOCALAPPDATA%\DeskBuddy\Tools\esptool-4.12.0` and checks it against the pin before
  every run. It holds the file open (no writers) while it is used.
- Licence, notices and the complete corresponding sources travel with it as `third-party\esptool\`:
  [companion/third_party/esptool/README.md](../companion/third_party/esptool/README.md).

## Flashing backend decision

**Decided (2026-10-18): esptool v4.12.0**, shipping only `esptool.exe`. The comparison below was made for this
board's required operations, from the release archives and source; no serial port was touched.

| | esptool v4.12.0 | esptool v5.6.0 | espflash v4.6.0 |
|---|---|---|---|
| Windows artifact | `esptool.exe` 10.9 MB, PyInstaller onefile, Authenticode-signed by Espressif | 14.5 MB, same packaging, signed | `espflash.exe` 17.1 MB, unsigned, needs the VC++ runtime |
| License | GPL-2.0-or-later (separate process; ship license and source) | same | MIT OR Apache-2.0 |
| Three regions, byte-exact, one session | yes (`write_flash --flash_* keep`) | yes | one `write-bin` per region, so three connect cycles |
| Chip identity (PSRAM size, flash ID) | `chip_id` / `flash_id` | yes | no PSRAM size and no flash ID on the S3 |
| Progress when piped | plain lines (`Writing at 0x… (N %)`) | rich UI, not verified piped | progress bar, hidden or not machine-readable [inference] |
| Verification | MD5 after every write, `verify_flash` | same | MD5 on write, no separate verify |
| Used on this board | every flash, backup and restore so far (through ESP-IDF 5.5.5) | never | never |

espflash has the simpler license, but it cannot identify the board's PSRAM and flash, and it cannot write the
three regions in one session. Its reliability for the required operations is therefore not comparable.
