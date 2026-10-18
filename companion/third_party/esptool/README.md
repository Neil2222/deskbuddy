# esptool v4.12.0 (bundled flashing tool)

The Companion's Firmware Manager writes firmware with Espressif's esptool. It ships Espressif's own standalone
Windows build unchanged and runs it as a separate program; DeskBuddy code does not link to it or modify it.

| What | Value |
|---|---|
| Program | `esptool.exe` from `esptool-v4.12.0-windows-amd64.zip`, <https://github.com/espressif/esptool/releases/tag/v4.12.0> |
| Release zip SHA-256 | `42fddc5e6a05716868ad77fb43acbf53be041f97abed87ff850df1dc88140889` (GitHub's published asset digest) |
| `esptool.exe` SHA-256 | `1cd8386a7934862eb6ce1e14af3cc2309b0a743de1171fd09fab2f2ef2f5d1a3` (pinned in `EsptoolBundle.ExeSha256`) |
| Authenticode | signed by Espressif Systems (Shanghai) Co., Ltd. |
| License | GNU General Public License v2 or later ([LICENSE](LICENSE), the file shipped in the release zip) |
| Corresponding source | `esptool-4.12.0-source.tar.gz` (the v4.12.0 tag, SHA-256 `0d099edae2d76edfeaed1b3c6699c278e168809e30845311431a0a812888ca83`) and `esptool-legacy-flasher-stub-1.11.1-source.tar.gz` (SHA-256 `f056a3f717178c6ec6f66a4560e32b23d180c67119dbb9d1da774845b49c2bfc`), see below |

`tools\fetch-esptool.ps1` downloads the zip and both source archives, checks all three hashes, and puts
`esptool.exe` and the source archives in this folder. Those files are not committed (`.gitignore`).
`DeskBuddy.Companion.csproj` embeds `esptool.exe` when it is present. `tools\companion.ps1 -Publish` fetches
it first and copies this folder, sources included, next to the published exe as `third-party\esptool\`.

## What must travel with the Companion

Whoever passes the Companion on passes on everything in `third-party\esptool\`:

- **GPL-2.0-or-later (esptool and the legacy flasher stub inside it):** the license text and the complete
  corresponding source, as files next to the program (GPL v2 section 3(a)), not only a link.
  - The esptool source archive holds the PyInstaller build definition Espressif uses
    (`.github/workflows/build_esptool.yml`).
  - `esptool.exe` carries the legacy flasher stub binaries (`esptool/targets/stub_flasher/1/*.json`), released
    under GPL-2.0-or-later from esptool-legacy-flasher-stub v1.11.1; its source archive is included as well.
- **The permissive notices below:** these components are inside `esptool.exe`.

This is the project's reading of the licenses, not legal advice. Have it reviewed before public distribution.

## Components inside esptool.exe

Listed from the PyInstaller archive table of contents of the pinned `esptool.exe`. Versions are given where the
bundled code states them.

| Component | Version | License | Text |
|---|---|---|---|
| esptool | 4.12.0 | GPL-2.0-or-later | [LICENSE](LICENSE) |
| esptool legacy flasher stub (`stub_flasher/1`) | 1.11.1 | GPL-2.0-or-later | [LICENSE](LICENSE), [licenses/stub-flasher-1-README.md](licenses/stub-flasher-1-README.md) |
| esp-flasher-stub (`stub_flasher/2`) | 0.7.0 | Apache-2.0 OR MIT | [licenses/stub-flasher-2-LICENSE-APACHE.txt](licenses/stub-flasher-2-LICENSE-APACHE.txt), [licenses/stub-flasher-2-LICENSE-MIT.txt](licenses/stub-flasher-2-LICENSE-MIT.txt) |
| pyserial | 3.5 | BSD-3-Clause | [licenses/pyserial-3.5.txt](licenses/pyserial-3.5.txt) |
| bitstring | 4.4.0 | MIT | [licenses/bitstring-4.4.0.txt](licenses/bitstring-4.4.0.txt) |
| tibs (used by bitstring) | as bundled | MIT | [licenses/tibs.txt](licenses/tibs.txt) |
| bitarray | as bundled | PSF-2.0 | [licenses/bitarray.txt](licenses/bitarray.txt) |
| intelhex | as bundled | BSD-3-Clause | [licenses/intelhex.txt](licenses/intelhex.txt) |
| Python runtime (`python311.dll`, standard library) | 3.11 | PSF-2.0 | [licenses/python-3.11.txt](licenses/python-3.11.txt) |
| Software incorporated in the Python runtime: OpenSSL (`libssl-3`, `libcrypto-3`), expat, libffi, zlib, libmpdec and others | as bundled | various | [licenses/python-3.11-incorporated-software.rst](licenses/python-3.11-incorporated-software.rst), [licenses/openssl-3.txt](licenses/openssl-3.txt) (Apache-2.0) |
| bzip2 (`_bz2`) | as bundled | bzip2 license (BSD-style) | [licenses/bzip2.txt](licenses/bzip2.txt) |
| xz / liblzma (`_lzma`) | as bundled | 0BSD | [licenses/xz.txt](licenses/xz.txt) |
| PyInstaller bootloader | as bundled | GPL-2.0-or-later with the bootloader exception (no source obligation for the bootloader in a frozen program) | [licenses/pyinstaller.txt](licenses/pyinstaller.txt) |
| Microsoft Visual C++ runtime (`VCRUNTIME140.dll`), Universal CRT (`ucrtbase.dll`, `api-ms-win-*.dll`) | as bundled | Microsoft redistributable runtime, under Microsoft's terms | (no text required to be passed on) |

The license texts were taken from each project's repository at the stated version where a tag exists, otherwise
from its main branch. The libffi, expat and zlib texts are the ones in CPython 3.11's license document.
