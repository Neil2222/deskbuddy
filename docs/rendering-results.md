# Rendering engine results (2026-10-13)

Firmware: `firmware/deskbuddy`, built with ESP-IDF v5.5.5 and the same settings as bring-up (CPU 240 MHz, octal PSRAM at 80 MHz, code and read-only data executed from PSRAM, `-O2`).
All numbers come from the device's own `PERF` logs (5-second windows). The bring-up benchmarks were re-run with the modified `board` component and are unchanged (33.51 Hz, 47.19 ms fill, 1589 µs blit, 16.8 FPS).

## Architecture

| Module | Role | Hardware dependent |
|---|---|---|
| `components/gfx` | Rectangles, dirty-rectangle set, indexed sprites with transparency, integer scaling and horizontal flip, frame animation, scene compositor, double-buffer repair | No (host-tested) |
| `components/buddy` | Placeholder robot: behavior state machine (`buddy_brain`), ASCII programmer art (`buddy_art`), pose-to-sprite mapping (`buddy_view`) | No (host-tested) |
| `components/layout` | Character stage (default 65 % of the width, rounded to 672 px) and the reserved control-panel area; background painting | No (host-tested) |
| `components/display` | Application-owned frame buffers, the scanout fill, and the buffer-swap proof | Yes |
| `deskbuddy/main` | Touch task (GT911 → events), render loop, layout configuration (Kconfig), performance log, verify mode | Yes |

Behavior, artwork, layout and rendering only meet in the render loop. Behavior produces a *pose*; the view maps it to sprites; the compositor only sees sprites and a background callback.

### Frame pipeline
1. The render task (core 1, priority 5) sleeps until the next pose change or a touch event.
2. The behavior state machine produces a pose; the view updates sprite fields.
3. `gfx_scene_collect()` compares every sprite with what is on screen and reports old and new bounds of whatever changed.
4. `gfx_repair_next()` adds the previous frame's changes, which the back buffer has not seen yet.
5. `gfx_scene_render()` composes each dirty rectangle in 32 KB internal-RAM strips (background, then sprites in z order) and copies finished rows to the back buffer. PSRAM is only written, never read back.
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
- **Budget:** each bounce fill copies 20 KB from PSRAM and must finish within one bounce period: 10 lines at 1386 px / 30.85 MHz = **449 µs**. It takes 280–400 µs during normal animation.
- **What breaks it:** full-frame PSRAM traffic. Full-screen redraws, and verify mode's compare, push fills to about 940 µs, the scanout underruns, and the driver resyncs at the next VSYNC. That is a visible one-frame glitch, counted as `resync` in the log.
- **Mitigation now:** both boot-time full redraws happen before the backlight comes on (`while dark: 3 scanout resyncs`).
- **Rule for the next phases:** keep redraws incremental while the screen is lit. Scene changes that need a full redraw should hide it (backlight fade) or spread it over frames. Larger bounce buffers and 120 MHz PSRAM are untested options for more margin.

### Swap timing
Presenting waits 0–30 ms for the next frame start (about 15–18 ms on average). Rendering at more than 33.7 FPS is impossible by design. The pixel-art frame rates (8–15 FPS for sprite frames) are far below it.

### Panel went dark while the firmware kept running (cause not confirmed)
- **Observed once:** after a few minutes the screen went black while the firmware kept rendering and scanning out normally (continuous uptime, 33.71 Hz, 0 resyncs). Only a board reset brought the picture back.
- **Possible cause (not confirmed):** host USB power management. Windows "USB battery saver" (*USB-batterijbesparing*: stop USB devices when the screen turns off) was enabled on the host laptop, and the laptop had just turned its screens off. The user has since disabled that setting.
- **Hypothesis:** the setting cut one of the two USB connections. The ESP32 stayed powered through the other, but the backlight or IO-expander supply did not, and the expander MCU lost its outputs. A board reset re-initialises the expander.
- **Status:** not reproduced and not handled in firmware. To confirm, re-enable the setting temporarily and let the laptop turn its screens off. A later robustness item: detect a lost expander state (read back its registers) and restore it, so the panel recovers without a reset.

## Touch

Measured over 15 taps on the robot by the user, during a 10-minute run that had 0 resyncs and 0 slow swaps:

| Metric | Value |
|---|---|
| Touch interrupt → reaction frame starts scanning out | 8.2–45.1 ms, average about 21.5 ms |
| Worst case | Rapid repeated taps while a reaction was already animating (render plus a full frame of swap wait) |
| Peak presented frame rate during rapid taps | 21.9 frames/s |

Add up to one more panel frame (0–30 ms) until the scanline reaches the robot, plus the GT911's internal scan time (not measurable from the MCU).

The touch task (core 1, priority 10) only reads the GT911 and queues events. It never touches a frame buffer, and rendering never blocks it. The render loop is woken directly by the event queue, so a tap does not wait for the next animation deadline.
