# DeskBuddy handover

Snapshot at the end of the rendering-engine milestone (2026-10-13). For a new machine, start with the README "Fresh Windows setup" section.

## Goal

A physical desktop companion for a work laptop:
- An animated pixel-art character with personality, emotions and touch interactions. Later Tamagotchi-like: persistent needs, moods and time-based behavior.
- A configurable touchscreen control panel for internal workplace services, using explicitly configured and validated commands. The device never supplies arbitrary URLs.
- Notifications and events from applications on the laptop or internal servers.

Architecture rules:
- **USB serial is the primary channel**, bidirectional, to a Windows companion service that talks to internal APIs. The ESP32 needs no access to the corporate network. Wi-Fi is optional.
- **Keep character behavior, rendering, assets, external events and communication independent**, so a desktop simulator and firmware/asset deployment via GitHub and the companion app stay possible. Development happens mostly at home while the device stays at work.
- Full control over firmware and assets, no proprietary apps or cloud services, maintainable C on ESP-IDF.

## Hardware

Waveshare ESP32-S3-Touch-LCD-7B:
- ESP32-S3, 16 MB flash, 8 MB octal PSRAM.
- 1024×600 RGB565 parallel panel.
- GT911 touch.
- Waveshare IO expander MCU at 0x24 (backlight, resets, USB/CAN mux).

Full details and pinout are in [docs/hardware.md](docs/hardware.md).

Connect through the **UART** Type-C port (CH343) with DIP switch #16 on the ESP32 side. The **USB** Type-C port (native USB-Serial/JTAG) also works while our firmware runs.

## Status

| Area | State |
|---|---|
| Toolchain | ESP-IDF v5.5.5, pinned and installed with `tools/setup-idf.ps1`. Managed components are locked in `dependencies.lock`. Companion: .NET 10 SDK per user via `tools/setup-dotnet.ps1` (no admin); `tools/companion.ps1 -Publish` makes a self-contained, double-clickable `companion\publish\DeskBuddy.Companion.exe` (the per-user runtime is not found by a double-clicked development build). |
| Board support (`firmware/components/board`) | Done: I2C bus, IO expander (native USB kept connected), RGB panel with bounce buffers (driver-owned frame buffers, or app-owned via `on_bounce_fill`), GT911 with interrupt, backlight brightness |
| Bring-up firmware (`firmware/bringup`) | Done, kept as the hardware regression test. Re-run after the board change: benchmarks unchanged. |
| Rendering engine (`gfx`, `display`, `layout`) | Done: dirty rectangles, indexed sprites with transparency, integer scaling, flip, frame animation, tear-free double buffering with a proven swap. 26 host unit tests. See [docs/rendering-results.md](docs/rendering-results.md). |
| Placeholder character (`buddy`, `firmware/deskbuddy`) | Done: idle breathing, blinking, antenna light, tap reaction, happy hops. |
| Moods (`buddy_mood`, `mood.set`) | Done, visually approved by the user on the device, on branch `companion`: idle, relaxed (smiling, half-closed eyes, calmer than idle), peukie (a smoke break with the telescopic hand), angry, sleepy, working, happy as looping animations in cycles of varying length. Reactions (tap, message, Monster) play on top and hand back; Office Mode keeps the mood; `secs=` makes a mood temporary. Built from shared pieces: new expressions in the face slots, a head nod, the cigarette in the existing hand rig, and six reusable effects (smoke, steam, Z, sweat, sparkle, anger mark) drawn by five particle sprites on the normal sprite/dirty-rectangle/writeback path. Companion: a Mood group with Buddy's faces. 8 host tests incl. a full-redraw comparison over every mood. |
| Monster Energy animation (`buddy_monster`) | Done, committed on `monster-charging` (`ee425b9`): ~7 s sequence started from the Monster can in the control panel; `BUDDY_EVENT_DRINK_MONSTER` for future triggers. The long bounce fills during the sequence (up to 638 µs) are fixed by the per-row cache writeback on `companion`: at most 296 µs now (see the results doc). |
| Office Mode (`office`) | Done, committed on `monster-charging` (`ee425b9`): moon button, 24-hour desk clock, character paused. No dimming: the backlight controller did not apply brightness changes reliably. The clock is set by the companion. |
| Windows companion (`companion/`, `link`, `serial_link.c`) | Done, verified on the device by the user, on branch `companion` (`b90967f`; redesign `cb32ce1`, approved by the user): serial protocol v1 ([docs/protocol.md](docs/protocol.md)) with checksums, replies and events; "DeskBuddy Control Center" (C#, .NET 10 WinForms, no extra packages): dark custom-painted controls in `UI/` (navy, Buddy's cyan, the sign's amber), Buddy's reacting face, data-driven activity tiles (`Activities.cs`), LED-sign message box with a recent list, connection details and diagnostics folded away; auto-detect and reconnect, clock sync including the next DST switch; steps aside while `tools/idf.ps1` flashes. Checked at 100 % display scaling only; 125–150 % is still to be looked at. |
| Messages on an LED marquee (`marquee`, `msg.show`) | Done, verified on the device by the user, on branch `companion`: text from the companion scrolls across an amber dot-matrix sign above the robot (below the clock in Office Mode), one LED column per 40 ms (25 redraws a second, about 4 characters a second); he jumps when it arrives; one message can wait (during Monster or another message); repeated ids are not shown twice; `style=` is ready for speech bubbles and notifications later. The sign's wide redraws exposed the cache-writeback problem behind the scanout artifacts (fixed; smooth and glitch-free at 25 fps per the user, see the results doc). **Currently flashed on the dev board.** |
| Factory restore round-trip | Done and verified (restore, then factory boot and touch, then reflash bring-up) |
| Buddy art export (`host-test -Icons`, `assets/buddy/`) | Done: faces, can, moon and app icons rendered from the firmware's own sprite data (nearest-neighbour; 16/20 px icons are crops of the face), embedded in the companion |
| Workplace integrations (AI agents, builds, downloads → moods), control panel, needs, persistence, Wi-Fi | **Not started** (intentionally); `mood.set` is the hook for the integrations (see the protocol doc) |

## Completed work
1. **Research:** hardware discovery from the Waveshare wiki and docs, schematic and example repo. Mistakes found in their docs: "ST7701" is wrong (Waveshare lists an ST7262), and the expander is a programmed MCU, not a CH422G.
2. **Factory backup:** read the full 16 MB, verified it on-chip, and stored a copy outside the project (see below).
3. **Factory demo analysis:** from the boot log it is a factory test image (`rtc_pcf85063`, IDF v5.4-dev).
   - It runs CAN, RS485, WiFi and SD tests next to an LVGL UI.
   - It drives EXIO5 high, which disconnects native USB.
4. **Minimal board support and bring-up firmware:**
   - Pattern, colors and touch mapping verified visually.
   - Native USB verified with `esptool` over COM5 while the app runs.
5. **Measurements** in [docs/bringup-results.md](docs/bringup-results.md):
   - Panel refreshes at 33.5 Hz.
   - Full-screen double-buffered redraw is limited to 16.8 FPS by PSRAM bandwidth.
   - A 128×128 sprite blit takes 1.6 ms.
   - Touch reports at about 110 Hz, and the MCU path from touch interrupt to frame buffer takes about 1.2 ms.
6. **Restore round-trip:** factory restore verified (flash digest matches, UI and touch confirmed), then bring-up reflashed and verified again.
7. **Laptop migration:** new work laptop set up (ESP-IDF v5.5.5 user-level install, reproducible build verified, board detected without reflashing). `setup-idf.ps1` now reports the real ESP-IDF version.
8. **Rendering engine and placeholder robot:**
   - Architecture reviewed before implementation.
   - Implemented as host-testable components with an on-device verify mode.
   - Verified on hardware: 0 stale pixels over 256 checked frames, 0 scanout resyncs over 10 minutes with the screen lit.
   - Touch interrupt to reaction frame: 8–45 ms (average about 21.5 ms).
   - Visually confirmed by the user.

## Architecture decisions

| Decision | Reason |
|---|---|
| ESP-IDF, not Arduino or PlatformIO | Waveshare's current examples are IDF 5.5. Their Arduino route needs a custom offline-only board package. Performance tuning (PSRAM, cache, LCD bounce buffers, task pinning) lives in IDF settings. |
| ESP-IDF pinned to v5.5.5 | Latest 5.5 patch release, the line Waveshare's examples target. Upgrading is a deliberate change in `tools/common.ps1`. |
| Own `board` component instead of Waveshare's example code | Their code drives EXIO5 high (killing USB), uses a global I2C singleton, and mixes in LVGL. Ours exposes only `board_init()`, `board_backlight_set()`, panel and touch handles, and frame buffers. |
| `esp_lcd` RGB driver with 10-line bounce buffers; frame buffers owned by `display` (driver `no_fb` mode) | Bounce buffers keep scanout stable while PSRAM is busy and make CPU writes coherent without cache flushes (the fill reads through the same cache). Owning the buffers lets `display` prove a buffer is no longer scanned before reuse: the driver's frame-done callback alone cannot (it can race a swap request). |
| GT911 handled by interrupt in a high-priority task on core 1 | Keeps touch latency independent of rendering. The LCD interrupt runs on core 0. |
| PSRAM at 80 MHz, code executed from PSRAM | Stable baseline. 120 MHz is experimental in IDF and is still to be evaluated. |
| Custom dirty-rectangle compositor; no LVGL yet | Pixel art needs integer scaling and palette sprites, and full redraws are too expensive (43–47 ms). Composition happens in a 32 KB internal-RAM strip, so PSRAM is only written; each copied row is written back from the cache at once (`esp_cache_msync`), because drawn pixels left in the cache were evicted inside the bounce fills and pushed them past their 449 µs budget. LVGL may still be used for the control panel later, presenting through the same `display` path, and must keep that rule. |
| Redraw the union of this and the previous frame's dirty rectangles (no copy-forward) | Sources live in internal RAM, so redrawing writes PSRAM only. Copying from the front buffer would read and write PSRAM (17.6 MB/s). |
| Behavior → pose → view → sprites | The brain knows no pixels, and the renderer knows no behavior. Future needs/moods and external events drive the brain, while the real art replaces `buddy_art`. |
| Layout in its own component, configured via Kconfig (stage 65 %, panel side) | Keeps the screen split out of the engine. The 35 % control-panel area is reserved and empty. |
| Host tests with MSVC (Visual Studio Build Tools) and plain CMake | No ESP32 needed. Linux/CI can build the same tests with gcc. The snapshot tool is the seed of a desktop simulator. |
| Console on UART0, mirrored to USB-Serial/JTAG | Logs available on either port |
| Companion protocol: text lines on the console UART, `>DB1 … #crc` / `<DB1 … #crc` | Readable in any serial monitor and coexists with the log on the only connected port. CRC-16 plus a strict format means log text, typing and esptool traffic can never trigger an action. Capabilities in `hello` let firmware and companion grow independently. |
| Companion in C# / .NET 10 WinForms | Built-in serial port access, easy HTTP/JSON/Graph later; per-user SDK install, no admin. One NuGet package (`System.IO.Ports`); `companion/nuget.config` names nuget.org because the machine's NuGet config lists no sources. |
| Time zone from Windows, not Kconfig | `time.set` carries the UTC offset and the next DST switch, so any Windows zone works and the clock switches on time while the companion is closed. |

## Known issues and caveats
- **Full-screen redraws starve the scanout of PSRAM bandwidth.** They take 43 ms, and bounce fills then exceed their 449 µs budget, causing a one-frame resync glitch. Boot redraws happen before the backlight comes on. Office Mode switches redraw the whole screen with the light on (2–4 resyncs per switch); the user saw nothing wrong. See the results doc.
- **Backlight brightness cannot be changed reliably at run time.** The IO expander (a CH32V003 microcontroller with Waveshare firmware) acknowledges every write, but brightness changes came out random (levels never requested), sometimes stuck until the board lost power, and writing only the PWM register changed nothing. ESPHome users report the same. The firmware now sets the brightness once at boot. See the results doc.
- **Black screen: two different failures, neither fixed.** (1) *Dark after power-up, board running:* COM5 present, firmware renders, the screen stays black until RESET. Seen after plugging in and after the laptop's battery shutdown; not reproduced by a sleep test or an unplug/replug test. Suspected: the backlight command at power-up arrives before the IO expander MCU is ready, or a supply dip resets only the expander. If it becomes frequent, read the uptime from the log without resetting before choosing a fix. (2) *Board lost USB power or connection:* on 2026-10-14 at 10:51:14 Windows logged the board's USB-serial chip as removed while it was plugged in; afterwards RESET, BOOT and taps did nothing. Not a firmware issue: check the cable and port, or power the board from a charger or a powered hub. Details in the results doc.
- **The clock needs the companion after every power-up.** The board has no RTC (an I2C scan finds only the IO expander at 0x24 and the touch controller at 0x5D), so the ESP32 clock restarts at 1970 and Office Mode shows *CLOCK NOT SET* until the companion connects. An RTC module with a coin cell (DS3231 or PCF85063) on the HY2.0 I2C header would keep time through power loss, if that ever matters.
- **Windows lets one program open a COM port at a time.** `tools/idf.ps1` (flash, monitor) makes the companion let go automatically (named mutex `Local\DeskBuddy.PortHold`); other serial tools need the companion's *Disconnect* first.
- **The companion runs on the per-user .NET runtime** through `tools/companion.ps1` (it sets `DOTNET_ROOT`). Starting the exe directly needs the .NET 10 Desktop Runtime installed machine-wide (administrator), or a self-contained publish.
- **The board stays at work while development happens mostly at home.** Future firmware and asset updates must therefore be deployable without physical access: build and publish via GitHub, deliver through the Windows companion app over USB, with a recovery path (OTA slots) if an update fails. Until then, flashing needs someone at the board.
- **The UART path depends on DIP switch #16.** In the wrong position the CH343 enumerates but esptool reports "No serial data received".
- **Native USB disappears whenever any firmware sets EXIO5 high,** for example the factory image or Waveshare's examples. Use the UART port in that case.
- **The GT911 driver logs "I2C address initialization procedure skipped".** This is expected: reset and address selection go through the IO expander and are done in `board.c`.
- **The standalone `esptool` 5.5 from PyPI crashed** with `bitstring` 5 (`espefuse` ImportError). Use the esptool bundled with ESP-IDF, as the scripts do.
- **Not yet tested:** 120 MHz PSRAM, WiFi load, flash writes (NVS/OTA), sleep, battery ADC, SD card.

## Factory firmware backup

| | |
|---|---|
| File | `factory_full_16MB.bin`. The off-project copy is named `ESP32-S3-Touch-LCD-7B_441bf68d1178_factory_full_16MB.bin`. |
| Size | 16,777,216 bytes (full flash: bootloader, partition table, NVS, app) |
| SHA-256 | `fc48538dcb848ff87a321baaa90f073457cbedba8884b9784e65bf0dfdc18874` |
| Source device | MAC 44:1b:f6:8d:11:78. The dump includes that unit's NVS/PHY calibration. |
| Locations on the dev PC | `<repo>\backup\factory_full_16MB.bin` (gitignored) and `%USERPROFILE%\Documents\DeskBuddy-backups\` (with `SHA256SUMS.txt`) |
| Uniqueness | It matches no binary in Waveshare's repo. This dump is the only way to get the factory demo back. |

It is **not in Git**. To use it on another machine, copy it over manually (USB stick or private storage) and check the hash:

```powershell
(Get-FileHash -Algorithm SHA256 <dump>.bin).Hash.ToLower()
```

**Restore procedure:**
1. Connect the UART Type-C port with DIP switch #16 on the ESP32 side.
2. Run:

```powershell
tools\flash-backup.ps1 -Action verify  -Image <dump>.bin   # optional, read-only: is it already on the board?
tools\flash-backup.ps1 -Action restore -Image <dump>.bin   # writes 16 MB (~1 min), then verifies the digest
```

3. Press RESET or power-cycle the board. The factory UI appears, and native USB disappears about 1 s after boot (expected).

**If auto-reset fails:** hold BOOT, tap RESET, release BOOT, then retry.

## Next steps (to be approved one at a time)
1. **Companion, next rounds:** tray icon and autostart, notifications/events from the laptop to DeskBuddy, firmware updates over USB. The protocol is ready for new commands and events ([docs/protocol.md](docs/protocol.md#adding-a-command)).
2. **Control panel UI** in the reserved 35 % area: configurable buttons whose actions are IDs validated by the companion service, never URLs. Decide whether LVGL or our own widgets render it; either way it presents through `display`.
3. **Character life:** needs, moods, personality and time-based behavior in `buddy_brain`, with persistence (NVS) designed together with the flash layout.
4. **Asset pipeline and real character art:**
   - A PC tool that converts PNG/Aseprite art to the palette-index format `gfx_image_t` already uses.
   - Stored in a dedicated flash partition.
   - Final partition layout: NVS, two 3 MB OTA slots, about 9 MB of assets.
   - Deployed via GitHub releases and the companion app, so updates can be prepared at home while the board stays at work.
5. **Desktop simulator** built on the host-test project: SDL window plus mouse as touch, reusing `gfx`, `buddy` and `layout` unchanged.
6. **Robustness:** restore IO-expander state after a supply dropout, evaluate larger bounce buffers and 120 MHz PSRAM for scanout margin, flash writes (NVS/OTA) while scanning out.
7. **Wi-Fi:** optional, later.
