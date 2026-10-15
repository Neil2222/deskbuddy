# DeskBuddy

A standalone desk gadget with animated pixel-art characters, built on the Waveshare ESP32-S3-Touch-LCD-7B.

Status: board bring-up done; rendering engine and a placeholder robot character running on the device, with personality: seven moods (idle, relaxed, a smoke break, angry, sleepy, working, happy), a Monster Energy animation (drinking a can through a charging cable), a touchscreen menu, and Clock Mode: Buddy zooms in until his face becomes a desk clock. A Windows companion app connects over USB: it sets the clock, chooses his mood, triggers actions and sends messages that scroll across a retro LED sign. No workplace integrations or persistent state yet.
See [HANDOVER.md](HANDOVER.md) for project state and next steps. Measurements: [docs/bringup-results.md](docs/bringup-results.md) and [docs/rendering-results.md](docs/rendering-results.md).

## Layout

```
firmware/deskbuddy/           Application: render loop, touch task, serial link to the companion, options (menuconfig "DeskBuddy")
firmware/components/gfx/      Hardware-independent renderer: dirty rectangles, sprites, compositor, double-buffer repair
firmware/components/buddy/    Placeholder character: behavior (moods, reactions, Monster sequence), ASCII programmer art, pose-to-sprite view
firmware/components/layout/   Screen split: character stage + control panel
firmware/components/link/     Serial protocol to the Windows companion: frame parsing and formatting (docs/protocol.md)
firmware/components/marquee/  LED marquee for messages: 5x7 dot-matrix font, text policy, scrolling sign sprite
firmware/components/menu/     Touchscreen menu in the control panel: item table, navigation and touch rules, pixel-art view
firmware/components/office/   Clock Mode (first called Office Mode): stroke font, desk-clock face, moon art, scene switch, Buddy's zoom
firmware/components/display/  Application-owned frame buffers, scanout fill, buffer-swap proof
firmware/components/board/    Board support: I2C, IO expander, RGB panel, GT911 touch, backlight
firmware/bringup/             Bring-up firmware: test pattern, touch dot, performance benchmarks (hardware regression)
firmware/tests/host/          Host-side unit tests and PNG snapshot tool (no ESP32 needed)
companion/                    Windows companion app (C#, .NET 10, WinForms): DeskBuddy Control Center (activities, LED-sign messages, clock sync)
assets/buddy/                 Buddy's real pixel art exported for the companion (faces, can, moon, app icons); made by host-test -Icons
tools/common.ps1              Pinned ESP-IDF and .NET versions, tool paths and serial-port resolution
tools/setup-idf.ps1           One-time ESP-IDF install
tools/setup-dotnet.ps1        One-time per-user .NET SDK install (no administrator rights)
tools/idf.ps1                 Runs idf.py in the pinned environment (auto-detects the port)
tools/companion.ps1           Builds and starts the companion app; -Publish makes the double-clickable exe
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

- **Touch:** tap the robot for a surprise and happy hops. Everything else is in the **menu** in the control panel on the right (below).
- **Menu:** a pixel-art panel next to Buddy, built for a finger.
  - **Home** lists the categories: *Moods*, *Activities*, *Food & Drink*; *Games* and *Schedule* are planned and carry a SOON tag. *Back* (top left) goes up a level.
  - **Moods:** all seven as tiles with Buddy's face; a tap switches at once. The current mood has a cyan rim and a blinking light, also when it was set by the companion (and the companion follows changes made here).
  - **Activities:** *Surprise!* (the tap reaction) and *Clock* (Clock Mode). **Food & Drink:** *Monster* (he drinks a can through a cable plugged into his head, about 7 s); water, coffee, lunch and snacks are planned (SOON). Surprise! and Monster are dimmed while a Monster sequence runs.
  - **Using it:** a button sinks in under the finger and acts when the finger is lifted on it; sliding off cancels. Categories with more items than fit get pages: arrows in the footer, or swipe sideways. The header shows the page title and Buddy's head in his current mood; the footer says what he is up to.
  - **Adding things:** see [menu.h](firmware/components/menu/include/menu.h): one row in `menu_items.c` per item (label, category, icon, action), plus an icon in `menu_art.c` if it needs a new one.
- **Moods** (chosen in the menu, in the companion, or by any program over USB: `mood.set`, see [docs/protocol.md](docs/protocol.md)): how Buddy is, as a looping animation; he starts in *idle* after every power-up. Whichever side changes it, the other follows.
  - **Idle:** the original Buddy, breathing and blinking.
  - **Relaxed:** calm and content: a broad smile, half-closed eyes, breathing at half idle's pace, a long contented blink now and then, sometimes an unhurried look aside.
  - **Peukie:** a smoke break. He holds a cigarette in his telescopic hand, smoke curls up from the tip; now and then he brings it to his mouth for a long drag (the tip lights up, his eyes close), then blows the smoke out. It burns down over a few drags, and he lights a fresh one.
  - **Angry:** slanted brows and a frown, steam puffing from his head; now and then a fit of fury: red face, clenched teeth, shaking, an anger mark.
  - **Sleepy:** heavy lids and dimmed eyes, slow breathing; he nods off with floating Zs (and a little snore), jerks awake, and yawns.
  - **Working:** focused eyes scanning back and forth, a busy little bob, tongue out in concentration, sweat running down his head, moments of real effort, short pauses to look up.
  - **Happy:** bursts of hops with a big grin and sparkles, a happy dance in between.
  - Taps, messages and Monster interrupt any mood; he goes back to it afterwards. Clock Mode keeps it (a mood chosen meanwhile shows when he is back).
- **Clock Mode:** *Activities → Clock* in the menu (or *Clock* in the companion). Buddy notices, blinks, and comes toward you until his face fills the screen; he closes his eyes, his face screen goes dark, and the desk clock (24-hour time and date) is there. Tap anywhere: the clock sinks back into his dark face, his eyes open, he steps back to his place, stretches, and carries on with his mood; the menu comes back on the page you left. Each way takes about 1.5 s. The character is paused meanwhile; a running Monster sequence is dropped. The backlight keeps its brightness: the board's backlight controller did not apply brightness changes reliably.
  - **Its first name was Office Mode,** which stays in the code (`components/office`, `office_*`) and on the wire (`mode=office`), so existing integrations keep working.
  - **How the zoom works:** a camera on Buddy's own sprites (integer scales 8 → 75, crisp pixels), centred on his face screen; the face's last, dark frame is exactly the clock's background, so only the digits appear. Taps, Surprise!, Monster and new messages wait while he zooms. See `components/office/include/office_zoom.h`.
  - **If the screen stays dark or very dim** (seen after plugging in and after flashing): unplug the board for about 10 s. If it is dark right after plugging in, pressing RESET also helps.
  - **The clock is set by the Windows companion.** The board has no RTC and the ESP32 clock restarts at 1970 on every power-up, so until the companion has connected the clock shows `--:--` and *CLOCK NOT SET* rather than a guessed time. The time zone comes from Windows.
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
- **Host power settings:** disable Windows "USB battery saver" (stop USB devices when the screen turns off). The panel blanked once while the firmware kept running, possibly because of this (not confirmed); see [docs/rendering-results.md](docs/rendering-results.md).
- **Pinned versions:**
  - ESP-IDF is pinned in `tools/common.ps1`.
  - Managed components are pinned in `firmware/*/dependencies.lock`.
  - Build settings are in `sdkconfig.defaults`. `sdkconfig` is generated and not committed.

## Windows companion

```powershell
tools\setup-dotnet.ps1       # once: .NET 10 SDK -> %LOCALAPPDATA%\Microsoft\dotnet (no administrator rights)
tools\companion.ps1          # build and start
tools\companion.ps1 -Test    # unit tests (message encoding, frames)
tools\companion.ps1 -Publish # companion\publish\DeskBuddy.Companion.exe: one self-contained file (~47 MB) to double-click
```

- **Starting it by double-click:** use `companion\publish\DeskBuddy.Companion.exe` (make it with `-Publish`; pin it to the taskbar or copy it anywhere, also to another PC). It has .NET built in. The exe in `bin\Release` is a small development build that needs the .NET runtime: double-clicked, Windows says *You must install .NET Desktop Runtime*, because the SDK from `setup-dotnet.ps1` is installed per user and only `tools\companion.ps1` tells the app where it is. Re-run `-Publish` after changes (close the published app first).

- **Window:** Buddy's face in the header reacts (sleepy while disconnected, happy or surprised after an action); next to it a status pill. The pill or the gear opens the connection details (port, firmware, clock, *Sync clock*); *Diagnostics* at the bottom opens the log of commands, replies and the board's own log output (so a separate serial monitor is not needed). Both are folded away at start. All artwork is Buddy's own pixel art, scaled by whole numbers only.
- **Connection:** finds DeskBuddy's UART port (CH343, USB `1A86:55D3`) by itself and reconnects after unplugging. Opening the port does not reset the board. *Disconnect* keeps it disconnected until *Connect*.
- **Clock:** synced on connect, when the board restarts, every hour, and when Windows' time or time zone changes or the laptop wakes up. Daylight-saving switches happen on time even while the companion is not running.
- **Activities:** tiles for modes (Buddy, Clock; the one that is on is outlined), actions (Surprise!, Monster) and moods (Idle, Relaxed, Peukie, Angry, Sleepy, Working, Happy, with Buddy's face for each; the current one is outlined, and the header face shows it too). They come from the list in `companion/DeskBuddy.Companion/Activities.cs`: a new firmware command or mood is one entry there, and its tile is enabled only when the board announces it (`caps=`, `moods=`).
- **Messages:** type up to 100 characters and press Enter or *Send*: he jumps and the text scrolls across an amber LED sign above him (below the clock in Clock Mode, which stays on), one LED at a time, about 4 characters a second. Short messages repeat so they stay up at least 8 s; a full-length one takes about 28 s. A message sent during a Monster sequence or while another one is up waits its turn (one at a time). Accents show without accents, emoji as a box. The last 10 messages are listed under *Recent* (double-click or Enter to reuse); the list appears with the first message.
- **Flashing while it runs:** `tools\idf.ps1 ... flash` / `monitor` signals the companion to let go of the port and reconnects it afterwards. Other serial tools (for example PuTTY) need *Disconnect* first: Windows lets only one program open a COM port.
- **Without the companion** DeskBuddy works as before; only the clock stays *CLOCK NOT SET* after a power-up until the companion connects.
- Protocol: [docs/protocol.md](docs/protocol.md).

## Host tests (no hardware)

```powershell
tools\host-test.ps1              # gfx, buddy, office, layout, link (protocol), marquee and menu unit tests (MSVC, AddressSanitizer)
tools\host-test.ps1 -Snapshot    # also renders poses, Monster key frames, a Monster contact sheet, a mood contact sheet (mood_sheet.png), the menu (menu_*.png, and menu_sheet.png: the panel in every state), the LED marquee, the Clock Mode clock and the zoom into it (zoom_in_sheet.png, zoom_out_sheet.png) to firmware\tests\host\build\snapshots\*.png
tools\host-test.ps1 -Icons       # exports Buddy's art for the companion to assets\buddy\ (faces incl. the moods, can, moon, icon-16..256.png, buddy.ico)
```

- **Requirements:** Visual Studio Build Tools with the C++ workload. On Linux/macOS, plain `cmake` + `ctest` in `firmware/tests/host` works too.
- **Managed machines:** the script locates the Windows SDK itself, because group policy may block `reg.exe`, which Visual Studio's own environment script relies on.

## Factory firmware

The verified 16 MB factory dump is kept **outside Git**. Its filename, checksum, location and restore procedure are in [HANDOVER.md](HANDOVER.md#factory-firmware-backup).

```powershell
tools\flash-backup.ps1 -Action verify  -Image <dump>.bin    # read-only compare
tools\flash-backup.ps1 -Action restore -Image <dump>.bin    # write all 16 MB + verify
```
