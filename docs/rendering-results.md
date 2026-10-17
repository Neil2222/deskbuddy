# Rendering engine results (2026-10-13)

Firmware: `firmware/deskbuddy`, built with ESP-IDF v5.5.5 and the same settings as bring-up (CPU 240 MHz, octal PSRAM at 80 MHz, code and read-only data executed from PSRAM, `-O2`).
All numbers come from the device's own `PERF` logs (5-second windows). The bring-up benchmarks were re-run with the modified `board` component and are unchanged (33.51 Hz, 47.19 ms fill, 1589 µs blit, 16.8 FPS).

## Architecture

| Module | Role | Hardware dependent |
|---|---|---|
| `components/gfx` | Rectangles, dirty-rectangle set, indexed sprites with transparency, integer scaling and horizontal flip, frame animation, scene compositor, double-buffer repair | No (host-tested) |
| `components/buddy` | Placeholder robot: behavior state machine and events (`buddy_brain`), the Monster Energy sequence (`buddy_monster`), ASCII programmer art (`buddy_art`), pose-to-sprite mapping including the procedural arm and cable (`buddy_view`) | No (host-tested) |
| `components/layout` | Character stage (default 65 % of the width, rounded to 672 px) and the control panel (352 px); background painting | No (host-tested) |
| `components/menu` | The touchscreen menu in the control panel: item table, navigation and touch rules (`menu`), and its view: one sprite per button at 2x, redrawn in place only when it changes, tiles flipping in one by one on a page change (`menu_view`) | No (host-tested) |
| `components/office` | Clock Mode (first called Office Mode): antialiased stroke font rasterized into indexed images, the desk-clock face, the moon art, the scene switch (`office_switch`), and Buddy's zoom into the clock and back (`office_zoom`: a camera and a pose timeline on top of the switch) | No (host-tested) |
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

### Zooming into Clock Mode (2026-10-15; then still called Office Mode)
Buddy's head comes toward the viewer until his face screen fills the display, goes dark and becomes the clock; leaving reverses it with a wake-up. The zoom is a camera on the view: every sprite is drawn at a larger integer scale (8 → 75) by the compositor, which fills scaled sprites in runs, so a frame costs its area and no image is scaled or allocated. The large frames are full-screen redraws, which the measurements below show to be as safe as the scene switch's own.

Measured on the device over 12 transitions (6 in, 6 out; `ZOOM` log lines), including one during a Monster sequence and a burst of contrary requests:

| | Value |
|---|---|
| Duration | in 1.40–1.50 s (notice 280 ms, zoom 660 ms, eyes closing and dark, then the clock's two-frame redraw); out 1.50–1.58 s (eyes opening, zoom 660 ms, stretch and blink) |
| Frames | 24–29 per transition, 15–20 per second |
| Render time | average 27–37 ms, largest frames (full screen) 61–65 ms |
| Redraw | average 270–320 k px, largest 614,400 px (the whole screen) |
| Scanout resyncs | 0 in every transition |
| Longest bounce fill | 427–430 µs per transition (the scene switch's two full frames alone: 421–430 µs; budget 449 µs) |

- **Why it holds:** the frame rate limits itself (a full frame takes about 60 ms, two scanout periods), and the per-row cache writeback keeps PSRAM writes out of the bounce fills. The average redraw (about half the screen over 24–29 frames) suggests roughly half of a transition's frames are full-screen.
- **No seam:** the "screen off" tint of the face is exactly the clock's background, so the swap to the clock changes only the digits (17 k px, host test).

### Message marquee (2026-10-14)
The LED sign (`components/marquee`) is one sprite of 636x84 px; each update redraws its rectangle (56,448 px after alignment, plus the robot's own animation). It moves one LED column per 40 ms (25 redraws a second, about 4 characters a second).

Before the cache fix below, lowering the update rate only made the artifacts rarer:

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

**Back to 25 sign updates a second** (one LED column per 40 ms), same stress sequence (two messages back to back, Surprise and Monster during scrolling, a message sent during Monster): longest fill 280–300 µs, 21–32 frames a second while scrolling, 0 resyncs, 0 slow swaps. The user saw smooth scrolling and no glitches at all, also during Buddy's animations.

**Rule:** every write into a PSRAM frame buffer goes through a surface with `flush`, so no drawn pixels are left for the scanout to evict.

### Moods (2026-10-15)
Looping animations (`components/buddy/src/buddy_mood.c`). They use the same path as everything else: sprites in the scene (5 particle sprites added, so the scene limit went from 32 to 40), dirty rectangles, double-buffer repair and the per-row writeback. Measured on the device through the companion (each mood 12–22 s, then Surprise during Working, Monster during the smoke break, a message during Sleepy, a mood change in Office Mode, rapid changes). The smoke break was first called Relaxed; Relaxed is now a calm, smiling mood that redraws less than idle (not measured separately).

| | Frames/s | Redraw avg / max | Longest bounce fill | Resyncs |
|---|---|---|---|---|
| Idle (for comparison) | 3.1–3.9 | 41–54 k / 64.5 k px | 131–237 µs | 0 |
| Peukie (smoke break) | 11.5–13.1 | 15–21 k / 92 k px | 132–236 µs | 0 |
| Angry | 13.0–17.2 | 21–62 k / 106 k px | 155–236 µs | 0 |
| Sleepy | 2.2–8.0 | 23–44 k / 84 k px | 96–235 µs | 0 |
| Working | 12.9–15.2 | 42–56 k / 82 k px | 142–236 µs | 0 |
| Happy | 16.3–18.2 | 47–53 k / 91 k px | 150–236 µs | 0 |
| A message scrolling during Sleepy | 26.9–27.6 | 68–82 k / 284 k px | 290–306 µs | 0 |

Slow swaps stayed at 0 throughout. The one large redraw (284 k px, 30.7 ms to render) came when a message arrived during Sleepy: the sign appearing, Buddy jumping and three Zs vanishing in one frame, plus the previous frame's rectangles, probably overflowed the 16-entry dirty list, which then merges into larger boxes [INFERENCE]. It costs one late frame, not a glitch, and is not chased further. Host test `mood_redraws_match_full_redraw`: 2,338 frames over every mood with rapid switching, taps and a Monster sequence, all equal to a full redraw, largest redraw 107,520 px (17 % of the screen).

### KITT (2026-10-17; passed the first board test)

**Board test of `72a02d9` (2026-10-17, 13:00–13:16): passed.** Flashed over COM6 (`Hash of data verified`, `verify_flash` digests matched; `App version: 72a02d9`) after a fresh full-flash backup of the stable board (`Documents\DeskBuddy-backups\fw-fa4455d-before-kitt-20261017-125606`, verified against the board). The user watched every step: the turn in started on the touchscreen looked clean; the sweep is **smooth**, its brightness **right as it is**, and there were **no left-edge lines, tearing, flicker or stale pixels**; a tap on him turned him back cleanly into the mood he had (relaxed); a mood tile during KITT (happy) turned him back into that mood; CLOCK during KITT zoomed in and out as before, and he came back as himself. No watchdog warning, crash or allocation failure. `72a02d9` stays on the board for evaluation.

| | Measured on the board |
|---|---|
| Boot | faces ready in 68.3 ms; after `app_init` free internal 77,259 B (`fa4455d`: 77,379 B), PSRAM 4,922,236 B; render task stack 2,908 of 6,144 B never used (unchanged through the test) |
| Sweeping (5.2 min, then 7.5 min) | 32.7–33.5 frames/s (the panel's rate), render avg 1.85 / max 2.64 ms, redraw avg 10.3 k / max 13.3 k px, 0 resyncs, 0 slow swaps, fill max 236 µs; STRIPS (about 1.6 million fills): start late max 7 µs, margin min 324 µs, 0 close, 0 missed |
| Tap to turn back | touch → scanout 45 ms; `evt act … result=done`; mood back to relaxed |
| Clock Mode during KITT | zoom in 29 frames, out 28 frames, 0 resyncs, fill max 427–428 µs (as before); `result=interrupted`; back as himself, happy |

Buddy turns into a Knight Rider scanner (`components/buddy/src/buddy_kitt.c`): an overlay sprite exactly over his face screen (128 x 64 px at his stage scale), drawn by code each frame at half his art pixels (32 x 16 overlay pixels, 4 x 4 screen pixels each) with its own 9-colour palette, into the Monster rig's two existing pixel buffers (KITT's poses never carry the props the rig draws). In: eyes close (0.3 s), the glow dims, three red traces race across at staggered speeds leaving the screen black (0.6–1.3 s), one flicker, the dark-red housing comes up and the light fades in at the left end (to 2.0 s). Scan: a sinusoidal sweep, one round trip per 1.6 s (easing at both ends), the light's tail from its last 8 positions 28 ms apart, a faint glow on the housing's rim; antenna steady red. Out (1.8 s): the sweep slows to the middle and fades, the housing goes dark, the traces race back and bring his face back, the eyes open.

| | Measured off the board |
|---|---|
| Redraw while sweeping | 10,240 px per frame (the face screen 8,192 px, widened to 32-px cache-line columns); simulator avg 10.3 k, max 12.9 k px (the menu header with it) |
| Frame rate while sweeping | 29–30 frames/s (host test: 584 frames in 20 s; simulator 30.4/s) |
| Every frame vs a full redraw | equal (host test `kitt_redraws_match_full_redraw`, with his peukie hand before and after, since KITT borrows the rig's buffers) |
| Firmware | image +4,064 B (526,027 B); code +3.9 KB; DIRAM +120 B (the sprite, two image headers, the brain's field, the palette); no heap, no PSRAM, no image assets; scene sprites 65 → 66 of 72 |
| Long running | the scanner's clock is kept within 30 minutes past the turn (whole sweeps), so it never wraps; nothing accumulates (host test: 3 hours) |

### Clock faces: Mechanical Skeleton (2026-10-17; failed on the device, repaired, passed the second device test)

**Second device test, `fa4455d` (2026-10-17, about 11:55–12:00): passed.** Flashed over COM6 (`Hash of data verified`, `verify_flash` digests matched for bootloader, partition table and app; `App version: fa4455d`). The user watched every step and saw **no left-edge glitch lines, tearing, flicker or stale pixels**: the stationary Skeleton (clock not yet set, so nothing moved), the Skeleton ticking for over 2 minutes (with a minute roll), the hour's whir (clock set to xx:59:30 over the serial link, then back to the real time), the zoom in, and the tap out (Buddy zoomed out and carried on happy, the menu back). No watchdog warning, crash or allocation failure in the log. `fa4455d` stays on the board. Which of the repairs removed the lines is not known (not tested one at a time).

| | Measured on the board (`fa4455d`) |
|---|---|
| Boot | `clock faces ready in 68.3 ms` (`b4ddf91`: 8,395 ms); `frame 25081 B in internal RAM` (not the fallback); `started in 147 ms; free internal 201,159 B, PSRAM 5,438,224 B`; after `app_init`: free internal **77,379 B** (`b4ddf91`: 101,455 B), PSRAM 4,922,372 B (4,895,732 B); first full redraw 62.2 ms; backlight on at 1.06 s |
| Render task stack | 2,900 of 6,144 B never used (3,244 B used); this high-water mark did not change through the whole test (it is logged whenever it drops) |
| Classic (60 s, ticking) | 2.0 frames/s, render max 2.3 ms, 11.4 k px; fill max 77–235 µs; STRIPS: start late max 5–6 µs, margin min 368–372 µs, close 0, missed 0 |
| Skeleton stationary (clock not set) | 0 frames; fill max 57 µs; STRIPS: late max 5–6 µs, margin min 387 µs, close 0, missed 0 |
| Skeleton ticking (> 2 min) | 1.0 frames/s, render avg 15–28 / max 39.5 ms, redraw max 193 k px; fill max 110–235 µs; STRIPS: late max 5–7 µs (while drawing 4–7), margin min 235–339 µs, close 0, missed 0 |
| Skeleton: the hour's whir | 25 frames in the 6 s window, render avg 55 / max 83 ms (`b4ddf91`: 101 ms), redraw max 469 k px, fill max 281 µs; STRIPS: late max 8 µs, margin min 168 µs, close 0, missed 0 |
| Zoom in / out, face changes (both faces) | 25–29 frames, 0 resyncs, fill max 427–430 µs (as `29f5c80`); STRIPS: late max 8–10 µs, margin min 19–23 µs, close 4–8 per zoom, all while drawing (the full-screen frames), missed 0 |
| While dark at boot (backlight off) | fill max 468 µs, 0 resyncs; STRIPS: margin min −40 µs, close 9, missed 3 (estimated) — the full redraws before the backlight comes on, not visible (`29f5c80` measured fill max 544 µs there) |

Read with care: strip copies never started more than 10 µs late, on either face, drawing or not, so the PSRAM traffic of the Skeleton's redraws does not delay the copies' start as far as the interrupt can see. The STRIPS figures exclude the interrupt's own entry latency, so zero estimated misses does not by itself prove a stable picture; the user's visual check is the evidence. The only close copies on the lit screen are those of full-screen frames (zoom, face change), as with Classic before this work.

**Device test of `b4ddf91` (2026-10-17): failed the display-stability test; the board went back to `29f5c80` until the second test.** The user saw the old left-edge glitch lines again, only on the clock face (the Skeleton showed for most of the test; whether Classic showed them too is not known), and none after restoring `29f5c80`. Start-up also regressed: drawing the faces took **8,395 ms** (35 ms on the PC, about 240 times slower), the task watchdog fired once (`IDLE1` starved by the render task; a warning, no reset), and the screen stayed dark about 9 s longer. What the logs showed (they do not explain the glitch):

| | Measured on the board |
|---|---|
| Boot | `clock faces ready in 8395 ms`; `started in 147 ms; free internal 199,631 B, PSRAM 5,575,424 B` (`29f5c80`: 203,151 B / 5,577,520 B); after `app_init`: free internal 101,455 B, PSRAM 4,895,732 B; while dark: 1 resync, fill max 475 µs (`29f5c80`: 0, 544 µs) |
| Render task stack | 3,004 of 6,144 B never used after start-up (3,140 B used, including the face rendering) |
| Classic: zoom in / out | 28 / 26 frames, 0 resyncs, fill max 427 / 429 µs; the clock 2 frames/s, 11.4 k px (as before) |
| Skeleton: zoom in | 26 frames, render max 118 ms (the face's first full frame), 0 resyncs, fill max 428 µs |
| Skeleton: ticking (60 s) | 1.0 frames/s, render avg 18–33 / max 39.5 ms, redraw avg 79–145 k / max 170 k px, 0 resyncs, fill max 109–235 µs |
| Skeleton: the hour's whir (time set to 11:59:50 over the serial link) | 14 frames in the 5 s window, render avg 64 / max 101 ms, redraw max 469 k px, 0 resyncs, fill max 180 µs |
| Live face change (Skeleton ↔ Classic) | `clock face drawn`, 0 resyncs, fill max 426–427 µs |

The measured fills stay well inside the 449 µs budget, so whatever draws the lines is not the fill's duration as measured (from the start of the fill interrupt to its end). New compared with every earlier screen: the Skeleton's sprite pixels are read from PSRAM on every redraw (the full-screen frame image and the wheels, tens of KB per tick); everything else that redraws often keeps its pixels in internal RAM (menu, thought, Buddy's art, palettes).

**Off-board repair and diagnostics (2026-10-17, before the second device test):**

| Change | Result (measured off the board unless marked) |
|---|---|
| The artwork is drawn on the PC (`office_skeleton_art.c`, host only) and built in as PackBits-packed data (`office_skeleton_assets.c`, generated by `tools\host-test.ps1 -Assets`, checked by the host test `office_skeleton_assets_match_the_drawing`); the board only unpacks it | Start-up on this PC 35 ms → 0.5 ms. All 15 Skeleton snapshots and all 103 earlier ones byte-identical. On the board: 68.3 ms (measured in the second device test, above) |
| The full-screen frame sprite became runs of visible pixels drawn by the face's background (23,877 B of runs + 1,204 B of row offsets, copied into internal RAM at start) | Sprite pixels read from PSRAM per redraw (host model, 64-byte lines): escapement tick 46 → 28 KB, train step 157 → 96 KB, the hour's first frames 364 → 214 KB; 8.3 → 5.1 MB over the 20 s test (−39 %). Scene sprites 66 → 65 |
| Pixels the face writes into PSRAM (the unpacked images at start, a rolling digit, the date) are written out of the cache at once (`app_port_flush`), as the frame buffers' rows already are | [HYPOTHESIS] removes one way stale cache lines could reach a bounce fill (the earlier glitch mechanism); not measurable off the board |
| Firmware | Image 393,903 → 521,963 B (packed images 111,913 B and frame runs 25,081 B in flash; the drawing code, 9.4 KB, is gone); DIRAM 152,095 → 150,483 B; PSRAM for the face 539 → 389 KB; internal heap: +25 KB for the frame's runs (if there is no room they are read from flash-mapped constant data instead) |
| Strip-copy timing in the bounce-fill interrupt: start lateness (each fill against the previous one plus one 449 µs bounce period, accumulated within a frame), duration, margin (period − lateness − duration), fills within 50 µs of the deadline and estimated misses, split by whether the renderer was drawing a frame | Counters and maxima only; reported outside the interrupt as a `STRIPS` line after every `PERF`, `ZOOM` and redraw line. The interrupt grew from 250 to 422 bytes of code: no extra timer reads, about 40 more instructions per fill [INFERENCE: well under 1 µs against the 449 µs period]. Measured on the board: see the second device test above. The margin does not include the interrupt's own entry latency |

Answered by the second device test: fills did not start late (≤ 10 µs on either face, drawing or not); close fills on the lit screen came only from full-screen frames (zoom, face change), as with Classic. Not answered: which repair removed the lines (they were not tested one at a time), and whether the remaining sprite reads (wheels, cock, timeplate: 28 KB per tick) matter at all.

**Remaining limitations (accepted for now):** the Skeleton keeps 25,081 B of internal RAM for its frame for as long as the firmware runs (free internal heap after start 77 KB, was 101 KB), so a further face must justify its memory and should not add permanent internal RAM; full-screen frames (the zoom, a face change) still come within 19–23 µs of a strip's deadline by the STRIPS estimate, as Classic's did before this work (0 resyncs, nothing seen); the device forgets its face at a restart (the Companion puts it back); the board's log has a `STRIPS` line after every `PERF` line (every 5 s).

The rest of this section is from the host and the simulator, before the device test.

A second Clock Mode face (`components/office/src/office_skeleton.c`): procedural pixel art at scale 2, rendered once at start into PSRAM. Each wheel has 4 images (quarter-tooth steps; meshing wheels share the tooth movement, so 4 steps bring every wheel back to the same picture). Moving parts change sprite images only; the background is the clock's near-black with the wheels' shadows, computed per row.

| | Value |
|---|---|
| Scene sprites | 50 before + 16 for the Skeleton (frame, 7 wheels, anchor, cock, timeplate, 4 digits, date) = 66; the limit went from 56 to 72 (Classic keeps its own 6) |
| PSRAM | 539 KB of the face's 560 KB block (all its images, including a full-screen frame and double-buffered roll and date images), plus Classic's 96 KB as before |
| Internal RAM (static) | DIRAM 152,095 B used vs 148,515 B on `master` (+3.6 KB, mostly `.bss`: the face's sprites and images); image +17 KB |
| Render task stack (static, `-fstack-usage`, xtensa GCC) | `render_rect` 1,600 B (1,280 B at 56 sprites), `app_frame` 464, `render_task` 352: about 2.5 KB deepest on the frame path; the Skeleton's start-up rendering about 2.3 KB (`office_skeleton_init` 784, `works_row` 560, `render_image` 176); stack 6,144 B. The board now logs the real high-water mark (`render task stack: … never used`) whenever it reaches a new depth |
| Start-up rendering | 35 ms on this PC (simulator, Release). On the board (`b4ddf91`): 8,395 ms and a watchdog warning: the reason for building the artwork in (`fa4455d`: 68.3 ms) |

Host test `office_skeleton_redraws_match_full_redraw` (19:59:44 → 20:00:04: both trains, a minute roll, the hour's whir), all frames equal to a full redraw, none full-screen:

| Event | Largest redraw |
|---|---|
| Escapement tick (every second) | 36,672 px |
| A train step (left every 5 s, right every 15 s, never in the same second) | 169,728 px |
| The hour: whir (12.5 steps a second for 1.6 s) with all four digits rolling | 529,728 px (the rectangles of the wheels, the plate's digits and the trains merge) |

Simulator (`--port 47802`, a script as the companion): 1.0 frames/s in the Skeleton (avg 80–140 k px), 4.5 frames/s over the 6 s window with the hour's whir (avg 300 k px, max 469 k px), zoom in 42 frames / out 43 frames as with Classic, live face changes `clock face drawn` (two full frames, like the switch), a mouse tap leaves Clock Mode and the mood set meanwhile (happy) shows afterwards. The simulator has no scanout, so bounce fills and resyncs need the board: the whir's frames are the size of the zoom's (which measured 0 resyncs and fills ≤ 430 µs), the rest is smaller than idle Buddy.

## Touch

Measured over 15 taps on the robot by the user, during a 10-minute run that had 0 resyncs and 0 slow swaps:

| Metric | Value |
|---|---|
| Touch interrupt → reaction frame starts scanning out | 8.2–45.1 ms, average about 21.5 ms |
| Worst case | Rapid repeated taps while a reaction was already animating (render plus a full frame of swap wait) |
| Peak presented frame rate during rapid taps | 21.9 frames/s |

Add up to one more panel frame (0–30 ms) until the scanline reaches the robot, plus the GT911's internal scan time (not measurable from the MCU).

The touch task (core 1, priority 10) only reads the GT911 and queues events. It never touches a frame buffer, and rendering never blocks it. The render loop is woken directly by the event queue, so a tap does not wait for the next animation deadline.
