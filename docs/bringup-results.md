# Board bring-up results (2026-10-13)

Firmware: `firmware/bringup`, built with ESP-IDF v5.5.5. Settings: CPU 240 MHz, flash QIO at 80 MHz, octal PSRAM at **80 MHz**, code and rodata executed from PSRAM, `-O2`.
Panel: 1024×600 RGB565, two PSRAM frame buffers, 10-line bounce buffers in internal RAM.
All numbers are from the device's own logs. Two separate boots gave identical results.

## Functional checks

| Check | Result |
|---|---|
| Test pattern | Colors, bar order, 1-px checkerboard, border and corner markers all correct (confirmed visually). RGB565 bit order is right. |
| Backlight | On at 80 % through IO expander EXIO2 plus PWM register 0x05 (inverted duty). |
| Touch mapping | Corners reported at (26,20), (993,23), (25,591) and (1018,565). Touch maps 1:1 to the display with no swap or mirror. |
| Touch feel | Dot tracks the finger. Visible trailing only during very fast swipes (user observation). |
| Native USB | While the firmware runs, `303A:1001` stays enumerated (COM5). `esptool chip_id` over COM5 resets the board into the bootloader and back. |
| Factory restore | 16 MB written and the flash digest matches. Boot log is identical to the original. Factory UI and touch work (confirmed visually). |
| Reflash bring-up | Benchmarks identical to the first run. Pattern and touch work. |

## Display and rendering

| Metric | Value |
|---|---|
| Panel refresh, measured from frame-complete interrupts | **33.50 Hz** (29.85 ms per frame) |
| CPU left on core 0 (LCD interrupt core) vs core 1, spin-loop ratio | 84–86 %, so the bounce-buffer interrupt takes about 15 % of core 0 |
| Full-frame solid fill into PSRAM (1.2 MB) | 47.2 ms, 26.0 MB/s |
| Full-frame memcpy, PSRAM to PSRAM | 69.7 ms, 17.6 MB/s |
| Full-frame test pattern, per-pixel function | 156.8 ms |
| Double-buffered full-screen animation, swap synchronised to frame end | **16.8 FPS** (render 46.0 ms, so every other refresh) |
| 128×128 RGB565 blit from internal RAM into the frame buffer | 1.59 ms |
| 32×32 sprite scaled 4× to 128×128 (nearest neighbour) | 1.60 ms |

**What this means:**
- **Full-screen redraws are limited by PSRAM bandwidth.** Scanout alone reads about 62 MB/s from PSRAM, and CPU writes reach only about 26 MB/s.
- **Full-screen redraw tops out at half the refresh rate (16.8 FPS).** That is still within the 5–15 FPS target.
- **Partial updates are cheap.** A 128×128 sprite costs about 1.6 ms, so many sprites per frame fit in the budget.
- **Scaling is free.** The 4× scale-up costs the same as a plain copy, because frame-buffer writes dominate.
- **Implication for the rendering engine:** use dirty rectangles and partial redraws. Full-frame redraws should be rare (scene changes).

## Touch (GT911, interrupt-driven)

Measured over about 5,000 reports while dragging, tapping and holding:

| Metric | Value |
|---|---|
| Report rate while touched | 105–114 Hz (about 9.2 ms; worst gap 10.1 ms; never more than 20 ms) |
| I2C read of touch data at 400 kHz | 0.68 ms average, 0.95 ms max |
| Dot erase and redraw in the frame buffer | 0.5 ms average, 0.7 ms max |
| Touch interrupt to pixels written to the frame buffer | **1.2 ms average**, 1.9 ms typical max, 7.7 ms worst case (2 outliers) |
| Largest jump between consecutive reports | 55 px (fast swipe) |

**End-to-end latency:**
- **What adds up:** visible latency is GT911 internal scan/filter time, plus about 1.2 ms on the MCU, plus 0–30 ms until scanout reaches the pixel, plus LCD pixel response.
- **Not measured:** the GT911 internal time and the LCD response need a camera or photodiode.
- **Where the delay is:** MCU-side handling is not a bottleneck. The 33.5 Hz refresh dominates.

**Factory demo comparison:** it uses the same GT911 configuration (version 91), so its sluggishness comes from its rendering pipeline: LVGL full redraws plus background WiFi/CAN/SD test tasks. The touch hardware is not the cause.

## Not yet evaluated
- PSRAM at 120 MHz, which Waveshare uses. It is an experimental IDF feature and timing depends on temperature.
- Behaviour while WiFi is active, and during flash writes (NVS/OTA).
- Tearing while drawing directly into the visible frame buffer. None was observed, but it was not tested systematically.
