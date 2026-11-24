# Development

Building, flashing and testing DeskBuddy: the code layout, a fresh Windows setup, the firmware, the desktop simulator, the
host tests and the factory firmware backup. What the firmware does is in [features.md](features.md), the Windows app in
[companion.md](companion.md).

## Code layout

```
firmware/deskbuddy/           Board firmware: tasks, touch, UART link, the hardware under the application (port.c), options (menuconfig "DeskBuddy")
firmware/components/app/      The application, shared by board and simulator: menu, companion commands, Clock Mode, messages, recorded events (app.h, app_port.h)
firmware/simulator/           Desktop simulator (Windows): the application in a window, mouse as touch, TCP link for the companion
firmware/components/gfx/      Hardware-independent renderer: dirty rectangles, sprites, compositor, double-buffer repair
firmware/components/buddy/    Placeholder character: behavior (moods, reactions, activities, KITT, game faces), ASCII programmer art, pose-to-sprite view
firmware/components/layout/   Screen split: character stage + control panel
firmware/components/link/     Serial protocol to the Windows companion: frame parsing and formatting (docs/protocol.md)
firmware/components/marquee/  LED marquee for messages: 5x7 dot-matrix font, text policy, scrolling sign sprite
firmware/components/menu/     Touchscreen menu in the control panel: item table, navigation and touch rules, pixel-art view, the Tic-Tac-Toe board
firmware/components/ttt/      Tic-Tac-Toe: rules, Buddy's moves (minimax with deliberate blunders), rounds and score, his lines (pure logic)
firmware/components/missile/  Buddy Missile Defense: game logic, Buddy's game faces, drawing (md, md_buddy, md_view)
firmware/components/pdc/      PDC Manager "Ze Doen Maar Wat" on the device: lease, confirmation, results
firmware/components/usage/    The strips at the bottom of the stage: Claude usage (the companion's lease, countdowns; usage, usage_view) and the PC load stacked above it (sysinfo, sysinfo_view)
firmware/components/office/   Clock Mode (first called Office Mode): stroke font, the faces (Classic, Mechanical Skeleton), moon art, scene switch, Buddy's zoom
firmware/components/display/  Application-owned frame buffers, scanout fill, buffer-swap proof
firmware/components/board/    Board support: I2C, IO expander, RGB panel, GT911 touch, backlight
firmware/bringup/             Bring-up firmware: test pattern, touch dot, performance benchmarks (hardware regression)
firmware/tests/host/          Host-side unit tests and PNG snapshot tool (no ESP32 needed)
companion/                    Windows companion app (C#, .NET 10, WinForms): DeskBuddy Control Center with pages for Buddy, activities, the weekly schedule, the history, LED-sign messages, PDC Manager (ZDMW), usage (Claude usage, PC load) and the device (connection, simulator start, Windows options, diagnostics)
companion/DeskBuddy.Firmware/ Firmware package format shared by the companion and the tool: versions, ESP image and partition checks, signed manifest, update policy (docs/firmware-packages.md)
companion/DeskBuddy.FirmwareTool/  Developer tool: pack, sign, verify firmware packages; keygen
assets/buddy/                 Buddy's real pixel art exported for the companion (faces, can, moon, app icons); made by host-test -Icons
tools/common.ps1              Pinned ESP-IDF and .NET versions, tool paths and serial-port resolution
tools/setup-idf.ps1           One-time ESP-IDF install
tools/setup-dotnet.ps1        One-time per-user .NET SDK install (no administrator rights)
tools/idf.ps1                 Runs idf.py in the pinned environment (auto-detects the port)
tools/companion.ps1           Builds and starts the companion app; -Publish makes the double-clickable exe
tools/pdc-fake-server.py      Local stand-in for PDC Manager's ZDMW (no Teams) to try the companion's PDC integration
tools/host-test.ps1           Builds and runs the host tests with MSVC
tools/simulator.ps1           Builds and starts the desktop simulator
tools/flash-backup.ps1        Full 16 MB flash backup / verify / restore
tools/package-firmware.ps1    Builds the committed firmware in a fresh directory and makes a firmware package (docs/firmware-packages.md)
tools/fetch-esptool.ps1       Fetches the pinned esptool v4.12.0 the Companion embeds, with its sources (companion/third_party/esptool)
tools/schedule-board-test/    Scripts of the P6 schedule board test (docs/schedule-board-test.md)
docs/                         Hardware (hardware.md), measured results (bringup-results.md, rendering-results.md), serial protocol (protocol.md), firmware versions and packages (firmware-packages.md), schedule and history design (schedule.md, schedule-board-test.md, examples/), daily use on the laptop (daily-use.md)
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

The Companion needs the .NET 10 SDK (per user, no admin): `tools\setup-dotnet.ps1`, then `tools\companion.ps1 -Publish` for `companion\publish\DeskBuddy.Companion.exe`. Its data lives in `%APPDATA%\DeskBuddy` and is not in Git (history, settings, this installation's id: each PC keeps its own). To start from the example week instead of an empty schedule, copy `docs\examples\schedule.demo.json` there as `schedule.json` before the first start (it is off: switch it to *Live* in the Schedule page to apply it). Setting up the laptop for daily use, moving that folder between PCs, and backing it up: [daily-use.md](daily-use.md).

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
  - automatic demo pokes, Monster animations and Clock Mode toggles (hands-off measurements);
  - `GFX_VERIFY` debug mode.
- **Debug variants without touching the committed config:** use a separate build directory and an extra defaults file, e.g.
  `tools\idf.ps1 -C firmware\deskbuddy -B $env:TEMP\db-verify -D SDKCONFIG=$env:TEMP\db-verify\sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;$env:TEMP\verify.defaults" flash`.
- **Port selection:** the port comes from `-p`, else `$env:ESPPORT`, else auto-detection. Auto-detection prefers the CH343 UART port (USB `1A86:55D3`) over native USB (`303A:1001`).
- **Which Type-C port to use:** prefer the one labelled **UART**. Its DTR/RTS auto-reset works whatever firmware is running.
  - Set DIP switch #16 (UART select) to the ESP32 side. In the other position the CH343 talks to the external UART header, and esptool gets "No serial data received".
- **Native USB port** (labelled **USB**) also works with our firmware. Any firmware that drives IO expander pin EXIO5 high disconnects it, and the factory firmware does this.
- **Host power settings:** disable Windows "USB battery saver" (stop USB devices when the screen turns off). The panel blanked once while the firmware kept running, possibly because of this (not confirmed); see [rendering-results.md](rendering-results.md).
- **Pinned versions:**
  - ESP-IDF is pinned in `tools/common.ps1`.
  - Managed components are pinned in `firmware/*/dependencies.lock`.
  - Build settings are in `sdkconfig.defaults`. `sdkconfig` is generated and not committed.

## Desktop simulator

```powershell
tools\simulator.ps1                  # build and start: a 1024x600 window, the log in the console
tools\simulator.ps1 -Scale 2         # every panel pixel as 2x2 screen pixels
tools\simulator.ps1 -Stage 70 -PanelLeft -Verify   # menuconfig's layout options and GFX_VERIFY
```

- **Same code as the board:** everything above the hardware lives in `firmware/components/app` and is compiled unchanged into both: the character, menu, companion commands, Clock Mode, messages, PDC. Each side implements `app_port.h` (time, memory, frame buffers, backlight, the link): `firmware/deskbuddy/main/port.c` on the board, `firmware/simulator/sim_port.c` here. Behaviour changes go into `app` and show up in both.
- **Touch:** the left mouse button is a finger: press, drag, release, like the GT911 reports it.
- **Companion:** the simulator listens on `127.0.0.1:47801` and speaks the board's protocol byte for byte, log lines included (they show in the companion's *Diagnostics*). Easiest: *Device → Build and start* in the companion builds, starts and connects in one go. A simulator started from a terminal is found by auto-detect when no board is plugged in, or chosen as *Simulator* in the port list. Clock, moods, activities, messages and PDC work as with the board.
- **Like the board:** the clock starts at 1970 and shows *CLOCK NOT SET* until the companion sets it; the screen stays black until both buffers are drawn; the firmware version is `firmware/deskbuddy/version.txt` and the build the Git commit, as on the board (`info` says `board=simulator`).
- **Not simulated:** the panel scanout and PSRAM timing (`PERF` lines show render times but `scanout 0.00 Hz`), the backlight controller and the IO expander; measure those on the board.
- **Build:** MSVC like the host tests, in `firmware/tests/host/build-sim` (Release, no sanitizer). The host tests build it too, so a change in `app` that breaks the simulator fails `tools\host-test.ps1`.

## Host tests (no hardware)

```powershell
tools\host-test.ps1              # 167 unit tests: gfx (dirty rectangles, scene, double-buffer repair, animation), buddy (moods, Monster, activities, the question, Focus, KITT, game faces), office (Clock Mode, faces, zoom), layout, link (protocol), marquee, menu, app (the whole application on a fake platform, info included), pdc, ttt, missile and usage (the Claude usage and PC load strips) (MSVC, AddressSanitizer by default)
tools\host-test.ps1 -Snapshot    # also renders poses, Monster key frames, a Monster contact sheet, a mood contact sheet (mood_sheet.png), the menu (menu_*.png, and menu_sheet.png: the panel in every state), the LED marquee, the Clock Mode clock and the zoom into it (zoom_in_sheet.png, zoom_out_sheet.png), the Mechanical Skeleton face (skeleton_*.png: the face, close-ups, its ticks, the minute roll, the hourly whir, the CLOCK FACES page), the activities (act_*.png and activity_sheet.png: water, sneeze, stare, meditate, focus, the Peukie? and ZDMW questions), KITT (kitt_*.png, kitt_sheet.png, kitt_face_sheet.png), Tic-Tac-Toe (ttt_*.png, ttt_sheet.png), Missile Defense (md_*.png, md_sheet.png), the Claude usage strip (usage.png, usage_ask.png, office_usage.png, skeleton_usage.png) and the PC load strip (sysinfo.png, usage_sysinfo.png, office_usage_sysinfo.png) to firmware\tests\host\build\snapshots\*.png (162 files)
tools\host-test.ps1 -Icons       # exports Buddy's art for the companion to assets\buddy\ (faces incl. the moods, can, moon, the MY DAY icons myday-*.png, icon-16..256.png, buddy.ico)
tools\host-test.ps1 -Assets      # regenerates the Mechanical Skeleton's packed artwork (firmware\components\office\src\office_skeleton_assets.c, committed) from its drawing code; needed after changing office_skeleton_art.c (a host test fails until then)
```

- **Requirements:** Visual Studio Build Tools with the C++ workload, including *C++ AddressSanitizer* for x64 (the tests build with it by default: CMake option `HOST_SANITIZE`, ON). If linking fails because the x64 AddressSanitizer runtime is missing (as on the development PC, where only x86 is installed), either add it in the Visual Studio Installer, or build without the sanitizer: after a first `tools\host-test.ps1` has created `firmware\tests\host\build`, run `cmake -B firmware\tests\host\build -DHOST_SANITIZE=OFF` once (in a Developer PowerShell, or with ESP-IDF's CMake); CMake keeps that setting, so `tools\host-test.ps1` works from then on. CMake and Ninja come from Visual Studio's *C++ CMake tools* or, without them, from ESP-IDF's tools. On Linux/macOS, plain `cmake` + `ctest` in `firmware/tests/host` works too (tests only; the simulator is Windows-only).
- **Managed machines:** the script locates the Windows SDK itself, because group policy may block `reg.exe`, which Visual Studio's own environment script relies on.

## Factory firmware

The verified 16 MB factory dump is kept **outside Git**. Its filename, checksum, location and restore procedure are in [HANDOVER.md](../HANDOVER.md#factory-firmware-backup).

```powershell
tools\flash-backup.ps1 -Action verify  -Image <dump>.bin    # read-only compare
tools\flash-backup.ps1 -Action restore -Image <dump>.bin    # write all 16 MB + verify
```
