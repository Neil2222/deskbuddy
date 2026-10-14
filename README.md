# DeskBuddy

A standalone desk gadget with animated pixel-art characters, built on the Waveshare ESP32-S3-Touch-LCD-7B.

Status: board bring-up done; rendering engine and a placeholder robot character running on the device, including its first personality animation (drinking a can of Monster Energy through a charging cable) and an Office Mode desk clock. A Windows companion app connects over USB: it sets the clock, triggers actions and sends messages that scroll across a retro LED sign. No workplace integrations or persistent state yet.
See [HANDOVER.md](HANDOVER.md) for project state and next steps. Measurements: [docs/bringup-results.md](docs/bringup-results.md) and [docs/rendering-results.md](docs/rendering-results.md).

## Layout

```
firmware/deskbuddy/           Application: render loop, touch task, serial link to the companion, options (menuconfig "DeskBuddy")
firmware/components/gfx/      Hardware-independent renderer: dirty rectangles, sprites, compositor, double-buffer repair
firmware/components/buddy/    Placeholder character: behavior, Monster sequence, ASCII programmer art, pose-to-sprite view
firmware/components/layout/   Screen split: character stage + reserved control-panel area
firmware/components/link/     Serial protocol to the Windows companion: frame parsing and formatting (docs/protocol.md)
firmware/components/marquee/  LED marquee for messages: 5x7 dot-matrix font, text policy, scrolling sign sprite
firmware/components/office/   Office Mode: antialiased stroke font, desk-clock face, moon button, dark scene switch
firmware/components/display/  Application-owned frame buffers, scanout fill, buffer-swap proof
firmware/components/board/    Board support: I2C, IO expander, RGB panel, GT911 touch, backlight
firmware/bringup/             Bring-up firmware: test pattern, touch dot, performance benchmarks (hardware regression)
firmware/tests/host/          Host-side unit tests and PNG snapshot tool (no ESP32 needed)
companion/                    Windows companion app (C#, .NET 10, WinForms): connection, clock sync, actions, device log
tools/common.ps1              Pinned ESP-IDF and .NET versions, tool paths and serial-port resolution
tools/setup-idf.ps1           One-time ESP-IDF install
tools/setup-dotnet.ps1        One-time per-user .NET SDK install (no administrator rights)
tools/idf.ps1                 Runs idf.py in the pinned environment (auto-detects the port)
tools/companion.ps1           Builds and starts the companion app
tools/host-test.ps1           Builds and runs the host tests with MSVC
tools/flash-backup.ps1        Full 16 MB flash backup / verify / restore
docs/                         Hardware findings, measured results, serial protocol
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

- **Touch:** tap the robot for a surprise and happy hops. Tap the Monster can in the control panel and he drinks it through a cable plugged into his head (about 7 s; taps during the show are ignored).
- **Office Mode:** tap the moon at the bottom of the control panel. The screen shows a desk clock (24-hour time and date), and the character is paused; a running Monster sequence is dropped. Tap anywhere to bring DeskBuddy back. The backlight keeps its brightness: the board's backlight controller did not apply brightness changes reliably.
  - **If the screen stays dark or very dim** (seen after plugging in and after flashing): unplug the board for about 10 s. If it is dark right after plugging in, pressing RESET also helps.
  - **The clock is set by the Windows companion.** The board has no RTC and the ESP32 clock restarts at 1970 on every power-up, so until the companion has connected the clock shows `--:--` and *CLOCK NOT SET* rather than a guessed time. The time zone comes from Windows.
- **Options** (`tools\idf.ps1 -C firmware\deskbuddy menuconfig` → *DeskBuddy*):
  - stage width (default 65 %) and panel side;
  - backlight brightness;
  - performance-log interval;
  - automatic demo pokes, Monster animations and Office Mode toggles (hands-off measurements);
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

## Windows companion

```powershell
tools\setup-dotnet.ps1    # once: .NET 10 SDK -> %LOCALAPPDATA%\Microsoft\dotnet (no administrator rights)
tools\companion.ps1       # build and start
tools\companion.ps1 -Test # unit tests (message encoding, frames)
```

- **Connection:** finds DeskBuddy's UART port (CH343, USB `1A86:55D3`) by itself and reconnects after unplugging. Opening the port does not reset the board. *Disconnect* keeps it disconnected until *Connect*.
- **Clock:** synced on connect, when the board restarts, every hour, and when Windows' time or time zone changes or the laptop wakes up. Daylight-saving switches happen on time even while the companion is not running.
- **Actions:** Surprise, Monster, Office Mode, Buddy Mode, Sync clock. Mode and connection state are shown, plus a log of commands, replies and the board's own log output (so a separate serial monitor is not needed).
- **Messages:** type up to 100 characters and press Enter or *Send to Buddy*: he jumps and the text scrolls across an amber LED sign above him (below the clock in Office Mode, which stays on), one LED at a time, about 2.7 characters a second. Short messages repeat so they stay up at least 8 s; a full-length one takes about 40 s. A message sent during a Monster sequence or while another one is up waits its turn (one at a time). Accents show without accents, emoji as a box. The last 10 messages are listed for reuse.
- **Flashing while it runs:** `tools\idf.ps1 ... flash` / `monitor` signals the companion to let go of the port and reconnects it afterwards. Other serial tools (for example PuTTY) need *Disconnect* first: Windows lets only one program open a COM port.
- **Without the companion** DeskBuddy works as before; only the clock stays *CLOCK NOT SET* after a power-up until the companion connects.
- Protocol: [docs/protocol.md](docs/protocol.md).

## Host tests (no hardware)

```powershell
tools\host-test.ps1              # gfx, buddy, office, layout, link (protocol) and marquee unit tests (MSVC, AddressSanitizer)
tools\host-test.ps1 -Snapshot    # also renders poses, Monster key frames, a Monster contact sheet, the LED marquee and the Office Mode clock to firmware\tests\host\build\snapshots\*.png
```

- **Requirements:** Visual Studio Build Tools with the C++ workload. On Linux/macOS, plain `cmake` + `ctest` in `firmware/tests/host` works too.
- **Managed machines:** the script locates the Windows SDK itself, because group policy may block `reg.exe`, which Visual Studio's own environment script relies on.

## Factory firmware

The verified 16 MB factory dump is kept **outside Git**. Its filename, checksum, location and restore procedure are in [HANDOVER.md](HANDOVER.md#factory-firmware-backup).

```powershell
tools\flash-backup.ps1 -Action verify  -Image <dump>.bin    # read-only compare
tools\flash-backup.ps1 -Action restore -Image <dump>.bin    # write all 16 MB + verify
```
