# DeskBuddy

A standalone desk gadget with animated pixel-art characters, built on the Waveshare ESP32-S3-Touch-LCD-7B.

Status: board bring-up done; rendering engine and a placeholder robot character running on the device. No Windows bridge, workplace integrations or persistent state yet.
See [HANDOVER.md](HANDOVER.md) for project state and next steps. Measurements: [docs/bringup-results.md](docs/bringup-results.md) and [docs/rendering-results.md](docs/rendering-results.md).

## Layout

```
firmware/deskbuddy/           Application: render loop, touch task, layout options (menuconfig "DeskBuddy")
firmware/components/gfx/      Hardware-independent renderer: dirty rectangles, sprites, compositor, double-buffer repair
firmware/components/buddy/    Placeholder character: behavior, ASCII programmer art, pose-to-sprite view
firmware/components/layout/   Screen split: character stage + reserved control-panel area
firmware/components/display/  Application-owned frame buffers, scanout fill, buffer-swap proof
firmware/components/board/    Board support: I2C, IO expander, RGB panel, GT911 touch, backlight
firmware/bringup/             Bring-up firmware: test pattern, touch dot, performance benchmarks (hardware regression)
firmware/tests/host/          Host-side unit tests and PNG snapshot tool (no ESP32 needed)
tools/common.ps1              Pinned ESP-IDF version, IDF path and serial-port resolution
tools/setup-idf.ps1           One-time ESP-IDF install
tools/idf.ps1                 Runs idf.py in the pinned environment (auto-detects the port)
tools/host-test.ps1           Builds and runs the host tests with MSVC
tools/flash-backup.ps1        Full 16 MB flash backup / verify / restore
docs/                         Hardware findings and measured results
```

## Fresh Windows setup

Prerequisites: Git, Python 3.9–3.13, PowerShell 5.1+ (built in). The CH343 USB-UART driver installs automatically through Windows Update. If it doesn't, use [WCH's driver](https://www.wch-ic.com/downloads/CH343SER_EXE.html).

```powershell
git clone https://github.com/neil2222/deskbuddy.git
cd deskbuddy
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned   # once, if scripts are blocked
tools\setup-idf.ps1                                   # ESP-IDF v5.5.5 -> %USERPROFILE%\esp\v5.5.5\esp-idf
```

To keep ESP-IDF elsewhere, set `DESKBUDDY_IDF_PATH` to the `esp-idf` directory first, for both setup and later use. The scripts check that this checkout is exactly v5.5.5.

Optional: clone Waveshare's reference code into `_ref/`, which is gitignored:

```powershell
git clone --depth 1 https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7B.git _ref/ESP32-S3-Touch-LCD-7B
```

## Build, flash, monitor

```powershell
tools\idf.ps1 -C firmware\deskbuddy build
tools\idf.ps1 -C firmware\deskbuddy flash monitor     # Ctrl+] exits the monitor
tools\idf.ps1 -C firmware\bringup -p COM7 flash       # bring-up regression firmware, explicit port
```

- **Options** (`tools\idf.ps1 -C firmware\deskbuddy menuconfig` → *DeskBuddy*):
  - stage width (default 65 %) and panel side;
  - backlight brightness;
  - performance-log interval;
  - automatic demo pokes;
  - `GFX_VERIFY` debug mode.
- **Debug variants without touching the committed config:** use a separate build directory and an extra defaults file, e.g.
  `tools\idf.ps1 -C firmware\deskbuddy -B $env:TEMP\db-verify -D SDKCONFIG=$env:TEMP\db-verify\sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;$env:TEMP\verify.defaults" flash`.
- **Port selection:** the port comes from `-p`, else `$env:ESPPORT`, else auto-detection. Auto-detection prefers the CH343 UART port (USB `1A86:55D3`) over native USB (`303A:1001`).
- **Which Type-C port to use:** prefer the one labelled **UART**. Its DTR/RTS auto-reset works whatever firmware is running.
  - Set DIP switch #16 (UART select) to the ESP32 side. In the other position the CH343 talks to the external UART header, and esptool gets "No serial data received".
- **Native USB port** (labelled **USB**) also works with our firmware. Any firmware that drives IO expander pin EXIO5 high disconnects it, and the factory firmware does this.
- **Host power settings:** disable Windows "USB battery saver" (stop USB devices when the screen turns off). The panel blanked once while the firmware kept running, possibly because of this (not confirmed); see [docs/rendering-results.md](docs/rendering-results.md).
- **Pinned versions:**
  - ESP-IDF is pinned in `tools/common.ps1`.
  - Managed components are pinned in `firmware/*/dependencies.lock`.
  - Build settings are in `sdkconfig.defaults`. `sdkconfig` is generated and not committed.

## Host tests (no hardware)

```powershell
tools\host-test.ps1              # gfx, buddy and layout unit tests (MSVC, AddressSanitizer)
tools\host-test.ps1 -Snapshot    # also renders the robot's poses to firmware\tests\host\build\snapshots\*.png
```

- **Requirements:** Visual Studio Build Tools with the C++ workload. On Linux/macOS, plain `cmake` + `ctest` in `firmware/tests/host` works too.
- **Managed machines:** the script locates the Windows SDK itself, because group policy may block `reg.exe`, which Visual Studio's own environment script relies on.

## Factory firmware

The verified 16 MB factory dump is kept **outside Git**. Its filename, checksum, location and restore procedure are in [HANDOVER.md](HANDOVER.md#factory-firmware-backup).

```powershell
tools\flash-backup.ps1 -Action verify  -Image <dump>.bin    # read-only compare
tools\flash-backup.ps1 -Action restore -Image <dump>.bin    # write all 16 MB + verify
```
