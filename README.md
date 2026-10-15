# DeskBuddy

A standalone desk gadget with animated pixel-art characters, built on the Waveshare ESP32-S3-Touch-LCD-7B.

Status: board bring-up done; rendering engine and a placeholder robot character running on the device, with personality: seven moods (idle, relaxed, a smoke break, angry, sleepy, working, happy), a Monster Energy animation (drinking a can through a charging cable), a touchscreen menu, and Clock Mode: Buddy zooms in until his face becomes a desk clock. A Windows companion app connects over USB: it sets the clock, chooses his mood, triggers actions and sends messages that scroll across a retro LED sign. No workplace integrations or persistent state yet.
See [HANDOVER.md](HANDOVER.md) for project state and next steps. Measurements: [docs/bringup-results.md](docs/bringup-results.md) and [docs/rendering-results.md](docs/rendering-results.md). A desktop simulator runs the same application code on a Windows PC ([below](#desktop-simulator)).

## Layout

```
firmware/deskbuddy/           Board firmware: tasks, touch, UART link, the hardware under the application (port.c), options (menuconfig "DeskBuddy")
firmware/components/app/      The application, shared by board and simulator: menu, companion commands, Clock Mode, messages, PDC (app.h, app_port.h)
firmware/simulator/           Desktop simulator (Windows): the application in a window, mouse as touch, TCP link for the companion
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
companion/                    Windows companion app (C#, .NET 10, WinForms): DeskBuddy Control Center with pages for Buddy, activities, LED-sign messages, PDC Manager (ZDMW) and the device (connection, simulator start, diagnostics)
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
docs/                         Hardware findings, measured results, serial protocol, schedule design (schedule.md)
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
  - **Home** lists the categories: *Moods*, *Activities*, *Food & Drink*, *My Day*; *Games* is planned and carries a SOON tag. *PDC* appears after *My Day* only while the companion offers its PDC Manager integration (then *Games* moves to a second page). *Back* (top left) goes up a level.
  - **Moods:** all seven as tiles with Buddy's face; a tap switches at once. The current mood has a cyan rim and a blinking light, also when it was set by the companion (and the companion follows changes made here).
  - **Activities:** *Surprise!* (the tap reaction), *Clock* (Clock Mode), *Sneeze*, *Stare*, *Meditate*, *Focus* and *Peukie?*. **Food & Drink:** *Monster* (he drinks a can through a cable plugged into his head, about 7 s) and *Water*; coffee, lunch and snacks are planned (SOON). While an activity plays, the others are dimmed and the footer says what he is doing.
  - **My Day:** you tell Buddy what you are doing: *Work*, *Meeting*, *Coffee*, *Water*, *Smoke*, *Stretch*, *Lunch*. He hops and cheers on his sign ("ENJOY YOUR COFFEE!"), and the companion gets `evt did` to record it (recording comes with the activity history). Unlike the shows in *Activities* and *Food & Drink*, which never count as something you did. Works while he is busy with a show.
  - **Using it:** a button sinks in under the finger and acts when the finger is lifted on it; sliding off cancels. Categories with more items than fit get pages: arrows in the footer, or swipe sideways. The header shows the page title and Buddy's head in his current mood; the footer says what he is up to.
  - **Adding things:** see [menu.h](firmware/components/menu/include/menu.h): one row in `menu_items.c` per item (label, category, icon, action), plus an icon in `menu_art.c` if it needs a new one. A new activity: its timeline in `components/buddy/src/buddy_activity.c` (see `buddy_activity.h`).
- **Activities** (short shows on top of his mood, which resumes afterwards; taps wait until one has finished):
  - **Water:** he proudly raises a glass, gulps it down, looks at the empty glass... and remembers he is a robot. Short circuit: sparks, bolts, electrocuted flashes, smoke from both ears, a cough. The sign: *NOTE TO SELF: ROBOTS + WATER = NO*. About 5 s.
  - **Sneeze:** an itch, "a... a...", ACHOO: his head flies off his neck, hangs in the air, drops back with a clunk and a puff of dust. A dazed wobble, a sheepish smile. About 2.7 s.
  - **Stare:** a staring contest. The camera pushes in until his head fills the stage (the menu stays as it is); he stares, completely still. An eye twitches, a drop of sweat, eyes forced wide... he blinks. Red with fury, back to his place, fuming. The sign: *BEST OF THREE?* About 5 s.
  - **Meditate:** eyes closed, he floats up and hovers, sparkles drifting by; the sign: *OMMMM...*. A notification jolts him (wide eyes, strobing antenna) and he drops to the ground with a thud. About 7.4 s.
  - **Focus** (after MonkeyUser's comic "Focus"): eyes narrowed, he builds a plan in the air above him in chalk lines (A → B → C → D, a flowchart, E), tongue out, then a smug smirk. *HEY! DO YOU HAVE 1 SEC?* from off-stage: he glares... *POOF*, the plan goes up in smoke. *NEVERMIND*. Then he floats, blank and dimmed, among the drifting pieces: *WHAT WAS I DOING?* About 9 s.
  - **Peukie?:** he asks *PEUKIE ROKEN?* in a speech bubble, holding out a cigarette, and looks hopefully from *NEE* (bottom left of the stage) to *JA* (bottom right); a bar between them runs down (red in the last 10 s) and towards the end he gets impatient. *JA*: happy hops, then a smoke break (the *peukie* mood for 2 minutes, then back to the mood he had). *NEE*, or no answer within a minute: he takes the cigarette back, hangs his head and a tear rolls down (3 s). Answered on the touchscreen only.
  - His sign lines only go up when the sign is free; a message from the companion takes the sign over.
- **PDC → Ze doen maar wat!** (only while the companion's optional PDC Manager integration is on, logged in and allowed to click; the counter shows below the label): a tap makes Buddy ask *ZE DOEN MAAR WAT? / TEAMS-BERICHT VERSTUREN?* with *NEE* and *JA* (15 s, then as if NEE). Only *JA* sends anything: the click goes over USB to the companion, which posts it to PDC Manager (every click is a real Teams message). When PDC Manager confirms it, Buddy fumes for about 2.6 s (angry eyes, a red face, a waving hand, anger marks) and the sign shows *ZE DOEN MAAR WAT!!! #123* with the real count; otherwise the sign says what went wrong, and never "not sent" when it may have been sent. One click at a time, then 5 s before the next. The board never holds a login: see the companion below and [docs/protocol.md](docs/protocol.md).
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
tools\companion.ps1 -Test    # unit tests (message encoding, frames, PDC Manager integration against a fake server)
tools\companion.ps1 -Publish # companion\publish\DeskBuddy.Companion.exe: one self-contained file (~47 MB) to double-click
```

- **Starting it by double-click:** use `companion\publish\DeskBuddy.Companion.exe` (make it with `-Publish`; pin it to the taskbar or copy it anywhere, also to another PC). It has .NET built in. The exe in `bin\Release` is a small development build that needs the .NET runtime: double-clicked, Windows says *You must install .NET Desktop Runtime*, because the SDK from `setup-dotnet.ps1` is installed per user and only `tools\companion.ps1` tells the app where it is. Re-run `-Publish` after changes (close the published app first).

- **Window:** a sidebar with Buddy's face (it reacts: sleepy while disconnected, happy or surprised after an action), the pages and, at the bottom, the connection status (click it for the *Device* page). One page at a time: *Buddy* (mode and mood), *Activities*, *Message*, *PDC Manager*, *Device* (connection, the desktop simulator, diagnostics). The *Device* and *PDC Manager* entries carry a status dot. All artwork is Buddy's own pixel art, scaled by whole numbers only.
- **Connection** (*Device*): finds DeskBuddy's UART port (CH343, USB `1A86:55D3`) by itself and reconnects after unplugging; without a board it connects to the [desktop simulator](#desktop-simulator) if that is running. *Simulator* in the port list chooses the simulator explicitly, also while a board is plugged in. Opening the port does not reset the board. *Disconnect* keeps it disconnected until *Connect*. Below it, the firmware, the clock (*Sync clock*), and *Diagnostics*: the log of commands, replies and the board's own log output (so a separate serial monitor is not needed).
- **Desktop simulator** (*Device*): *Build and start* builds the latest code of the checkout the companion runs from (`tools\simulator.ps1`), starts it and switches the connection to it; build problems show in *Diagnostics*. *Rebuild and restart* after a code change; *Close* closes it and goes back to auto-detect (the board, if plugged in). Only offered when the companion runs from a DeskBuddy checkout (a development build or `companion\publish`), with the host-test toolchain installed.
- **Clock:** synced on connect, when the board restarts, every hour, and when Windows' time or time zone changes or the laptop wakes up. Daylight-saving switches happen on time even while the companion is not running.
- **Buddy and Activities:** tiles for modes (Buddy, Clock; the one that is on is outlined) and moods (Idle, Relaxed, Peukie, Angry, Sleepy, Working, Happy, with Buddy's face for each; the current one is outlined, and the sidebar face shows it too) on *Buddy*; actions (Surprise!, Sneeze, Stare, Meditate, Focus) and Food & Drink (Monster, Water) on *Activities*. After an action the sidebar face shows Buddy's face for it for a moment. They come from the list in `companion/DeskBuddy.Companion/Activities.cs`: a new firmware command, mood or activity is one entry there, and its tile is enabled only when the board announces it (`caps=`, `moods=`, `acts=`).
- **Schedule** (editor only for now: it is not applied to Buddy yet; design and roadmap in [docs/schedule.md](docs/schedule.md)): Buddy's week, Monday to Sunday, as blocks from a start to an end time to the minute (00:00–24:00). A block may set a **mood**, name a **planned activity** (an intention: nothing is ever logged because of it), both, or neither, and can be switched off. Pick a day in the chips (each shows its blocks as a small bar; today has a cyan dot), select a block on the timeline and edit it on the right: name, times (type them, or ↑/↓ for a minute and Shift+↑/↓ for 15), mood, plan, on/off, *Duplicate*, *Delete* (with *Undo delete*). **On the timeline,** drag a block's body to move it (its length stays) or its top or bottom edge to change its start or end: 5-minute steps, Shift for single minutes, a label shows the new times and the editor follows; release to keep it, Escape to cancel; dragging near the top or bottom scrolls; a block stays within its day. A small movement first, so a click never moves a block; the block on top is the one you drag. *New block* adds an hour after the selected block; double-clicking empty time adds one there. Blocks may **overlap**: the one that started last is on top (equal starts: the shorter one), and the one below applies again when it ends. A block cannot cross midnight: an end before the start offers *Split over two days*. *Copy … to* copies the day to other days (replacing them; you are asked first if they have blocks). *Schedule on/off* keeps the blocks but asks Buddy for nothing. Stored in `%APPDATA%\DeskBuddy\schedule.json` (readable JSON; written atomically, the previous version kept as `.bak`, a damaged file set aside and the backup used). The rules for applying it later (`Schedule/MoodDirector.cs`): a scheduled mood is sent only when the schedule starts asking for a different mood; when no block asks for one, Buddy keeps the mood he has (no forced idle); your own mood choice stays until the next such change or *Resume schedule*.
- **History** (recorded, no page yet; design in [docs/schedule.md](docs/schedule.md) §3 and §4b): what Buddy really did and what you told him on MY DAY, kept in `%APPDATA%\DeskBuddy\device-events.jsonl` (append-only, one line per device event `(dev, boot, seq)`; the simulator's in `device-events.sim.jsonl`). After every connect the companion asks DeskBuddy for what happened while it was away (`log.since`), so a MY DAY tap or an answered question while the companion was closed is not lost (as long as the board kept power); duplicates are impossible by the key, and anything the board could no longer supply is written down as a gap rather than guessed. Questions and shows become interactions (yes, no, timeout, interrupted, unknown), MY DAY taps and a JA to *Peukie?* become activities; moods and shows never do.
- **Messages:** type up to 100 characters and press Enter or *Send*: he jumps and the text scrolls across an amber LED sign above him (below the clock in Clock Mode, which stays on), one LED at a time, about 4 characters a second. Short messages repeat so they stay up at least 8 s; a full-length one takes about 28 s. A message sent during a Monster sequence or while another one is up waits its turn (one at a time). Accents show without accents, emoji as a box. The last 10 messages are listed under *Recent* (double-click or Enter to reuse); the list appears with the first message.
- **PDC Manager (Ze Doen Maar Wat), optional, off by default:** the *PDC Manager* section switches it *On*; it then shows whether `https://pdcmanager` is reachable (off the company network it just says so and checks again every minute), who is logged in, the ZDMW permission and count, a login form when needed (the password is never stored; the session is kept DPAPI-encrypted in `%APPDATA%\DeskBuddy\pdc-session.bin`, settings in `settings.json`), *Log out*, and the *Ze doen maar wat!* button: click, then click again within 4 s to post the ZDMW Teams message. The same click can come from Buddy's touchscreen menu (firmware with the `pdc` capability). One click at a time, at least 5 s apart, never retried; a result without an answer says *maybe sent*. Off means no network traffic at all. PDC Manager's own certificate is pinned (`Pdc/PDCManager.cer`). To try it without Teams: `python tools\pdc-fake-server.py` and start the companion with `DESKBUDDY_PDC_URL=http://127.0.0.1:8765` (only loopback addresses are taken); details in the script. Protocol side: [docs/protocol.md](docs/protocol.md#pdc-manager-zdmw).
- **Flashing while it runs:** `tools\idf.ps1 ... flash` / `monitor` signals the companion to let go of the port and reconnects it afterwards. Other serial tools (for example PuTTY) need *Disconnect* first: Windows lets only one program open a COM port.
- **Without the companion** DeskBuddy works as before; only the clock stays *CLOCK NOT SET* after a power-up until the companion connects.
- Protocol: [docs/protocol.md](docs/protocol.md).

## Desktop simulator

```powershell
tools\simulator.ps1                  # build and start: a 1024x600 window, the log in the console
tools\simulator.ps1 -Scale 2         # every panel pixel as 2x2 screen pixels
tools\simulator.ps1 -Stage 70 -PanelLeft -Verify   # menuconfig's layout options and GFX_VERIFY
```

- **Same code as the board:** everything above the hardware lives in `firmware/components/app` and is compiled unchanged into both: the character, menu, companion commands, Clock Mode, messages, PDC. Each side implements `app_port.h` (time, memory, frame buffers, backlight, the link): `firmware/deskbuddy/main/port.c` on the board, `firmware/simulator/sim_port.c` here. Behaviour changes go into `app` and show up in both.
- **Touch:** the left mouse button is a finger: press, drag, release, like the GT911 reports it.
- **Companion:** the simulator listens on `127.0.0.1:47801` and speaks the board's protocol byte for byte, log lines included (they show in the companion's *Diagnostics*). Easiest: *Device → Build and start* in the companion builds, starts and connects in one go. A simulator started from a terminal is found by auto-detect when no board is plugged in, or chosen as *Simulator* in the port list. Clock, moods, activities, messages and PDC work as with the board.
- **Like the board:** the clock starts at 1970 and shows *CLOCK NOT SET* until the companion sets it; the screen stays black until both buffers are drawn; the firmware version is `git describe`, as ESP-IDF's.
- **Not simulated:** the panel scanout and PSRAM timing (`PERF` lines show render times but `scanout 0.00 Hz`), the backlight controller and the IO expander; measure those on the board.
- **Build:** MSVC like the host tests, in `firmware/tests/host/build-sim` (Release, no sanitizer). The host tests build it too, so a change in `app` that breaks the simulator fails `tools\host-test.ps1`.

## Host tests (no hardware)

```powershell
tools\host-test.ps1              # gfx, buddy, office, layout, link (protocol), marquee and menu unit tests (MSVC, AddressSanitizer)
tools\host-test.ps1 -Snapshot    # also renders poses, Monster key frames, a Monster contact sheet, a mood contact sheet (mood_sheet.png), the menu (menu_*.png, and menu_sheet.png: the panel in every state), the LED marquee, the Clock Mode clock and the zoom into it (zoom_in_sheet.png, zoom_out_sheet.png) to firmware\tests\host\build\snapshots\*.png
tools\host-test.ps1 -Icons       # exports Buddy's art for the companion to assets\buddy\ (faces incl. the moods, can, moon, icon-16..256.png, buddy.ico)
```

- **Requirements:** Visual Studio Build Tools with the C++ workload, including *C++ AddressSanitizer* (the tests build with it). CMake and Ninja come from Visual Studio's *C++ CMake tools* or, without them, from ESP-IDF's tools. On Linux/macOS, plain `cmake` + `ctest` in `firmware/tests/host` works too (tests only; the simulator is Windows-only).
- **Managed machines:** the script locates the Windows SDK itself, because group policy may block `reg.exe`, which Visual Studio's own environment script relies on.

## Factory firmware

The verified 16 MB factory dump is kept **outside Git**. Its filename, checksum, location and restore procedure are in [HANDOVER.md](HANDOVER.md#factory-firmware-backup).

```powershell
tools\flash-backup.ps1 -Action verify  -Image <dump>.bin    # read-only compare
tools\flash-backup.ps1 -Action restore -Image <dump>.bin    # write all 16 MB + verify
```
