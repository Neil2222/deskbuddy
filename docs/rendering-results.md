# Rendering engine results (2026-10-13)

Firmware: `firmware/deskbuddy`, built with ESP-IDF v5.5.5 and the same settings as bring-up (CPU 240 MHz, octal PSRAM at 80 MHz, code and read-only data executed from PSRAM, `-O2`).
All numbers come from the device's own `PERF` logs (5-second windows). The bring-up benchmarks were re-run with the modified `board` component and are unchanged (33.51 Hz, 47.19 ms fill, 1589 µs blit, 16.8 FPS).

## Architecture

| Module | Role | Hardware dependent |
|---|---|---|
| `components/gfx` | Rectangles, dirty-rectangle set, indexed sprites with transparency, integer scaling and horizontal flip, frame animation, scene compositor, double-buffer repair | No (host-tested) |
| `components/buddy` | Placeholder robot: behavior state machine and events (`buddy_brain`), the Monster Energy sequence (`buddy_monster`), ASCII programmer art (`buddy_art`), pose-to-sprite mapping including the procedural arm and cable (`buddy_view`) | No (host-tested) |
| `components/layout` | Character stage (default 65 % of the width, rounded to 672 px) and the reserved control-panel area; background painting | No (host-tested) |
| `components/office` | Office Mode: antialiased stroke font rasterized into indexed images, the desk-clock face, the moon button, and the scene switch (`office_switch`) | No (host-tested) |
| `components/display` | Application-owned frame buffers, the scanout fill, and the buffer-swap proof | Yes |
| `deskbuddy/main` | Touch task (GT911 → events), serial link to the Windows companion (commands → events, replies), render loop, Office Mode switching, backlight at boot, wall-clock time with the companion's time zone, layout configuration (Kconfig), performance log, verify mode | Yes |

Behavior, artwork, layout and rendering only meet in the render loop. Behavior produces a *pose*; the view maps it to sprites; the compositor only sees sprites and a background callback.

### Frame pipeline
1. The render task (core 1, priority 5) sleeps until the next pose change or a touch event.
2. The behavior state machine produces a pose; the view updates sprite fields.
3. `gfx_scene_collect()` compares every sprite with what is on screen and reports old and new bounds of whatever changed.
4. `gfx_repair_next()` adds the previous frame's changes, which the back buffer has not seen yet.
5. `gfx_scene_render()` composes each dirty rectangle in 32 KB internal-RAM strips (background, then sprites in z order) and copies finished rows to the back buffer. Each row is written back from the data cache to PSRAM right after its copy (the surface's `flush` callback, `esp_cache_msync`; see "Drawn rows must leave the cache at once"). PSRAM is only written, never read back.
6. `display_present()` swaps the buffers (below).

Nothing is drawn while nothing changes.

### Buffers and swap proof
- Two 1024×600 RGB565 frame buffers in PSRAM (2 × 1.2 MB), allocated by `display`. The RGB panel driver runs in `no_fb` mode with its two 10-line bounce buffers in internal RAM.
- `display_bounce_fill()` is the **only** code that reads a frame buffer. It selects a buffer only when asked for pixel 0 (the start of a frame, including after the driver's VSYNC resync), so each scanned frame comes from one buffer and tearing is impossible.
- `display_present()` returns only when, under the scanout spinlock, the scanout has switched to the new buffer **and** no fill that started earlier is still copying from the old one. Fills can nest (the GDMA end-of-frame and LCD VSYNC interrupts are both allocated `LOWMED` and may sit on different levels), so the ISR keeps a per-buffer reader count.
- Wake-ups and timeouts never count as completion: a timeout logs a warning and the wait continues.
- Dirty rectangles are widened to 32-pixel columns (64-byte PSRAM cache lines).

### Debugging aids
- `CONFIG_DESKBUDDY_GFX_VERIFY`: after each incremental redraw, the whole scene is drawn into a scratch buffer and compared with the frame about to be shown.
- `CONFIG_DESKBUDDY_DEMO_POKE_S`: pokes the character every N seconds for hands-off measurements.
- `tools/host-test.ps1 -Snapshot`: renders the character's poses to PNG with the firmware's own code.

## Measurements

| Scenario | Frames/s | Render avg / max | Redraw avg / max | Swap wait avg / max | Scanout resyncs | Longest bounce fill |
|---|---|---|---|---|---|---|
| Boot: two full redraws (backlight still off) | – | 43.4 ms each | 614,400 px | – | 3 | 942 µs |
| Idle (breathing, blinks, antenna) | 3.0–3.9 | 3.4–4.5 / 5.6 ms | 40–56 k / 64.5 k px | 14–16 / 29.4 ms | 0 | 281–399 µs |
| Reactions every 3 s (surprise + happy hops) | 13–16 | 5.2–5.4 / 6.2 ms | 64–66 k / 70 k px | 16–18 / 29.7 ms | 0 | 341–394 µs |
| Verify mode + reactions | 5.2–7.1 | 5.3–5.5 / 6.3 ms (excl. check) | 67–69 k / 74 k px | 15–17 / 29.7 ms | ~12/s | – |

- **Correctness:** verify mode checked 256 consecutive frames over 42 s, including reactions: **0 mismatched pixels**. Host tests check the same property over 500 randomized frames.
- **Scanout:** 33.71 Hz. The panel timings give 30.85 MHz / (1386 × 661) = 33.67 Hz. Bring-up's 33.5 Hz came from a 2-second window (±0.5 frame resolution).
- **Frame budget:** the worst render (6.2 ms) uses 21 % of a 29.85 ms panel frame. The swap wait (average half a frame) dominates the per-frame time.
- **Memory after start:** internal RAM 241 KB free, PSRAM 5.74 MB free (4.49 MB with verify mode).

## Findings

### PSRAM bandwidth decides scanout stability
- **Budget:** each bounce fill copies 20 KB from PSRAM and must finish within one bounce period: 10 lines at 1386 px / 30.85 MHz = **449 µs**. Since the per-row cache writeback it takes 130–310 µs (before: 280–400 µs idle, up to 713 µs with the message sign).
- **What breaks it:** full-frame PSRAM traffic. Full-screen redraws, and verify mode's compare, push fills to about 940 µs, the scanout underruns, and the driver resyncs at the next VSYNC. That is a visible one-frame glitch, counted as `resync` in the log.
- **Mitigation now:** both boot-time full redraws happen before the backlight comes on (`while dark: 3 scanout resyncs`).
- **Rule for the next phases:** keep redraws incremental while the screen is lit. Rare full redraws on a user action (the Office Mode switch) were acceptable in a user test; the backlight cannot be used to hide them (it does not change reliably at run time). Larger bounce buffers and 120 MHz PSRAM are untested options for more margin.

### Swap timing
Presenting waits 0–30 ms for the next frame start (about 15–18 ms on average). Rendering at more than 33.7 FPS is impossible by design. The pixel-art frame rates (8–15 FPS for sprite frames) are far below it.

### Panel went dark while the firmware kept running (cause not confirmed)
- **Observed once:** after a few minutes the screen went black while the firmware kept rendering and scanning out normally (continuous uptime, 33.71 Hz, 0 resyncs). Only a board reset brought the picture back.
- **Possible cause (not confirmed):** host USB power management. Windows "USB battery saver" (*USB-batterijbesparing*: stop USB devices when the screen turns off) was enabled on the host laptop, and the laptop had just turned its screens off. The user has since disabled that setting.
- **Hypothesis:** the setting cut one of the two USB connections. The ESP32 stayed powered through the other, but the backlight or IO-expander supply did not, and the expander MCU lost its outputs. A board reset re-initialises the expander.
- **Status:** not reproduced and not handled in firmware. To confirm, re-enable the setting temporarily and let the laptop turn its screens off. A later robustness item: detect a lost expander state (read back its registers) and restore it, so the panel recovers without a reset.
- **Second occurrence (2026-10-14):** the laptop ran out of battery and shut down. After reconnecting, the firmware booted and rendered normally (touches were handled) but the screen stayed black. An ESP32 reset over the UART port (which re-initialises the expander) brought the picture back.
- **Third occurrence (2026-10-14):** the board was plugged into the laptop (UART port) and the screen stayed black. A reset over the UART port brought the picture back; the boot log after that reset was normal (backlight on 0.94 s after start). The power-up itself could not be logged: the serial port only appears after plug-in. Possible cause (not confirmed): at power-up the ESP32 sends the backlight command (about 0.7 s after power) before the expander MCU is ready; after a reset only the ESP32 restarts, so the command arrives. To check: replug several times and count how often it stays dark.
- **Reproduction attempts (2026-10-14, evening, laptop on its charger):** (1) laptop sleep for 66 s (20:48:19–20:49:25) with the board running: the screen stayed on, COM5 stayed connected, and the board's uptime continued (no restart). (2) Unplugged for 10 s and replugged: the board restarted (uptime 21.8 s at 20:51:05) and the backlight came on normally. Neither reproduced the black screen.
- **Suspected cause (not confirmed):** all four occurrences coincide with the host laptop changing its power state or the board getting power (screens off, battery shutdown, a day at work with several standby periods and a Windows restart at 16:28, plugging in). Either the backlight command at power-up reaches the expander MCU before it is ready, or a short supply dip resets only the expander while the ESP32 keeps running; both leave the backlight off with nothing re-sending it. **Workaround:** press RESET (or unplug for 10 s). **If it becomes frequent:** connect the log without resetting and read the uptime (short: the board restarted, so delay or repeat the backlight command at boot; long: the expander lost its state, so re-send the expander state periodically).
- **The afternoon at work was a different failure (2026-10-14):** Windows' device log (*Kernel-PnP/Device Management*, event 1010) shows the board's USB-serial chip (CH343, `VID_1A86&PID_55D3`) *"surprise removed as it is reported as missing on the bus"* at 10:51:14, while it was plugged in and 3 minutes after a normal 15-minute log ended; the laptop's first standby came later (10:58). Afterwards the screen stayed dark and RESET, BOOT and taps did nothing, and COM5 could not be opened. The CH343 does not depend on the ESP32 or the expander, so the board most likely lost USB power or its connection (loose cable or connector, or the laptop switching off the port; a high backlight current could trigger that, not confirmed). This cannot be fixed in firmware. Check: the cable and connector, another USB port, or power the board from a charger or a powered hub. To tell the two failures apart: if COM5 is missing, it is the connection; if COM5 is present and RESET brings the picture back, it is the power-up problem above.

### Monster Energy animation (2026-10-14)
Measured with `DEMO_MONSTER_S` (a sequence every 12–13 s) and compared with the tap reaction on the same board and build (`DEMO_POKE_S=3`).

| | Frames/s | Redraw avg / max | Longest bounce fill | Scanout resyncs |
|---|---|---|---|---|
| Idle | 3.1–3.3 | 50 k / 64.5 k px | 336–391 µs | 0 |
| Tap reaction (surprise + hops) | 13–16 | 64–65 k / 70 k px | 362–394 µs | 0 |
| Monster sequence, first version (every 20 ms tick, whole body trembling) | 20–32 | 47–99 k / 137 k px | 528–628 µs | 0 |
| Monster sequence, current (50 ms frame clock, head-only wobble, split cable sprite) | 15–27 | 34–69 k / 101 k px | 433–638 µs | 0 |

- **Correctness:** host tests replay the whole sequence on two alternating buffers; every presented frame equals a full redraw, and no frame redraws the full screen.
- **Resolved (2026-10-14):** the fills above the 449 µs budget showed a few stale lines for one frame (the known intermittent horizontal artifact). The cause was drawn pixels left in the data cache, not the redraw size; see "Drawn rows must leave the cache at once". With the fix, a Monster sequence during a scrolling message keeps every fill at or below 296 µs.

### Office Mode (2026-10-14)
The switch between DeskBuddy and the clock repaints the whole screen: the scene is swapped and both buffers are redrawn (one full frame, then a one-pixel change that makes the double-buffer repair redraw the other buffer). The backlight is not touched (see the brightness finding below). The first versions switched in the dark (backlight faded out, redraw, faded in to 25 % of normal for the clock); the measurements below are from those versions with `DEMO_OFFICE_S=10` and `DEMO_MONSTER_S=13`, 8 switches including one in the middle of a Monster sequence.

| | Value |
|---|---|
| Redraw of both buffers | two frames, about 110–130 ms (logged as `clock drawn` / `DeskBuddy drawn`) |
| Scanout resyncs during the redraw | 2–4 per switch |
| Resyncs and full redraws outside a switch | 0 in every window |
| Clock without a valid time (current state) | 0 frames redrawn; longest bounce fill 57 µs |
| Clock with a valid time (host tests) | colon glows twice per second (about 11 k px); a minute change redraws one or two numerals |

- **Lit switches look fine:** in a user test where the "off" during the switch did not take effect, 31 switches (9 within 2.5 s, 3 during a Monster sequence) were redrawn with the backlight on, 2–4 resyncs each, and the user saw no glitch. So the dark switch was dropped.
- **Found and fixed during testing:** invalidating the whole screen for both redraw frames made the double-buffer repair redraw a third full frame after the switch. A host test runs the whole loop (scene, character, clock, switch, repair) and fails if any full-screen frame is presented outside booting or switching.
- **Touch stopped working in Office Mode (found on the device, fixed):** after some backlight writes, the IO expander leaves the shared I2C bus in a state where every GT911 read fails, until the expander is addressed again. One streak lasted 9.5 s and ended 30 ms after the next expander write. Because a failed read never clears the GT911's status register, the GT911 stops interrupting, and the touch task used to wait for an interrupt forever. So after the fade-in to the clock (the last expander write) touch was dead and the clock could not be dismissed. Now the touch task retries every 10 ms after a failed read and re-sends the expander's output register first (same value). Over 11 hands-off switches every failure streak ended after 1–3 attempts. Without run-time backlight writes, no touch read failed in the later tests.
- **Backlight brightness is not applied reliably (found on the device):** the IO expander (a CH32V003 microcontroller with Waveshare firmware) acknowledges every write. Three write patterns were tried, each tested by the user:
  1. Fades (PWM register, then output register with the enable bit, every 20 ms; about 25 writes per switch): brightness after a switch was random, and eventually stuck at about 5 % until the board was unplugged for 10 s. RESET did not clear it. The same stuck-dim state appeared right after flashing (three times).
  2. No fades (backlight off, redraw, on at 20 % or 80 %; the level re-sent 200 ms later): still random, including levels never requested (about 60 %, full, fully dark, unchanged).
  3. PWM register only, as in Waveshare's brightness-slider example (enable bit set once): the brightness never changed at all, and "off" did not go dark.

  An ESPHome user on the same board reports dimming that works a couple of times after a reboot and then stops. The expander's registers do not read back reliably (values lag or come from another register), so its state cannot be checked from firmware. Decision: the backlight is set once at boot (PWM register, then the enable bit, as in the original bring-up) and never changed afterwards; Office Mode is not dimmed.

### Message marquee (2026-10-14)
The LED sign (`components/marquee`) is one sprite of 636x84 px; each update redraws its rectangle (56,448 px after alignment, plus the robot's own animation). It moves one LED column per 80 ms (12.5 redraws a second, about 2 characters a second).

Lowering the update rate only made the artifacts rarer. These rates were measured before the cache fix below:

| Update rate | Frames presented while scrolling | Longest bounce fill | Scanout resyncs | Seen by the user |
|---|---|---|---|---|
| 25 per second (one LED column per 40 ms) | 27–29/s | 600–700 µs | 0 | frequent horizontal artifacts across the blue background: not acceptable |
| 12.5 per second (two columns per 80 ms, same scroll speed) | 13.6–14.9/s | 613–679 µs | 0 | no artifacts in one short test, but the two-column jumps look choppy |
| 16.7 per second (one column per 60 ms) | 19.6–20.1/s | 608–676 µs | 0 | first test: no artifacts. A later test with consecutive messages while Buddy jumped: strong artifacts |
| 12.5 per second (one column per 80 ms) | 15–31/s | 611–713 µs | 0 | severe artifacts, worst during a Monster sequence |

### Drawn rows must leave the cache at once (2026-10-14)
**Isolation on the device** (each step flashed and judged by the user on the physical screen):

| Build | Longest bounce fill while a message runs | Seen by the user |
|---|---|---|
| Normal (12.5 sign updates a second) | 611–713 µs | severe broad artifacts across the background |
| Buddy frozen while the sign scrolls (only the sign redrawn, 56,448 px per frame) | 563–687 µs | fewer, still clearly present |
| Sign never drawn (lifecycle, events and Buddy's jump unchanged) | 347–365 µs | broad artifacts gone; only the older thin lines on the left |
| Sign drawn, each drawn row written back from the cache at once | 238–306 µs (idle: 129–132 µs) | **no glitches at all**, including Surprise and Monster during scrolling |

**Cause:** frame-buffer writes go through the 64 KB, 8-way data cache (64-byte lines, 128 sets; the pattern repeats every 8 KB, so every 4 frame rows of 2048 B land on the same sets). Nothing wrote drawn pixels back explicitly, so they stayed in the cache until something else needed those lines, mostly the bounce fill itself, which reads the front buffer through the same cache. Each dirty line it evicted cost an extra PSRAM write inside the time-critical fill. The width of a redraw decides how much it leaves behind: Buddy's 224-px-wide rectangle can occupy only 28 sets (at most 14 KB stays cached), the sign's 672-px-wide one 84 sets (up to 42 KB). That is why the sign overran the budget at a lower pixel count and frame rate than the tap reaction (64–70 k px at 13–16 frames a second, 362–394 µs).

**Fix:** the surface has an optional `flush` callback that `gfx_scene_render()` calls for every row right after copying it into the frame buffer. On the device it is `esp_cache_msync(..., ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA)`: a writeback without invalidation. Rows start and end on 64-byte lines (64-byte aligned buffers, 32-px aligned dirty rectangles), as the API requires without `ESP_CACHE_MSYNC_FLAG_UNALIGNED`; chunked msync is off, so each call writes back at most one row (≤ 21 lines). No driver, bounce-buffer, cache, PSRAM or backlight setting changed.

**Cost:** the render core now waits for its own PSRAM writes: render time while scrolling went from 8.1 / 13.6 ms to 10.5 / 17.5 ms (avg / max); the first frame of a Monster sequence (299 k px) took 33 ms. Swap waits and slow swaps are unchanged.

**Rule:** every write into a PSRAM frame buffer goes through a surface with `flush`, so no drawn pixels are left for the scanout to evict.

## Touch

Measured over 15 taps on the robot by the user, during a 10-minute run that had 0 resyncs and 0 slow swaps:

| Metric | Value |
|---|---|
| Touch interrupt → reaction frame starts scanning out | 8.2–45.1 ms, average about 21.5 ms |
| Worst case | Rapid repeated taps while a reaction was already animating (render plus a full frame of swap wait) |
| Peak presented frame rate during rapid taps | 21.9 frames/s |

Add up to one more panel frame (0–30 ms) until the scanline reaches the robot, plus the GT911's internal scan time (not measurable from the MCU).

The touch task (core 1, priority 10) only reads the GT911 and queues events. It never touches a frame buffer, and rendering never blocks it. The render loop is woken directly by the event queue, so a tap does not wait for the next animation deadline.
