# DeskBuddy handover

Snapshot at the end of board bring-up (2026-10-13). For a new machine, start with the README "Fresh Windows setup" section.

## Goal

A standalone desk gadget with animated pixel-art characters (5–15 FPS), responsive touch, and later WiFi integrations. Requirements:
- Full control over firmware and assets.
- No proprietary apps or cloud services.
- Maintainable C on ESP-IDF.

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
| Toolchain | ESP-IDF v5.5.5, pinned and installed with `tools/setup-idf.ps1`. Managed components are locked in `dependencies.lock`. |
| Board support (`firmware/components/board`) | Done: I2C bus, IO expander (native USB kept connected), RGB panel with double buffering and bounce buffers, GT911 with interrupt, backlight brightness |
| Bring-up firmware (`firmware/bringup`) | Done: test pattern, touch-following dot, benchmarks. Currently flashed on the dev board. |
| Factory restore round-trip | Done and verified (restore, then factory boot and touch, then reflash bring-up) |
| Rendering engine, asset pipeline, app, WiFi | **Not started** (intentionally) |

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

## Architecture decisions

| Decision | Reason |
|---|---|
| ESP-IDF, not Arduino or PlatformIO | Waveshare's current examples are IDF 5.5. Their Arduino route needs a custom offline-only board package. Performance tuning (PSRAM, cache, LCD bounce buffers, task pinning) lives in IDF settings. |
| ESP-IDF pinned to v5.5.5 | Latest 5.5 patch release, the line Waveshare's examples target. Upgrading is a deliberate change in `tools/common.ps1`. |
| Own `board` component instead of Waveshare's example code | Their code drives EXIO5 high (killing USB), uses a global I2C singleton, and mixes in LVGL. Ours exposes only `board_init()`, `board_backlight_set()`, panel and touch handles, and frame buffers. |
| `esp_lcd` RGB driver with 2 PSRAM frame buffers and 10-line bounce buffers | Bounce buffers keep scanout stable while PSRAM is busy, and they make CPU writes to the frame buffer coherent without cache flushes. Two buffers allow tear-free swaps. |
| GT911 handled by interrupt in a high-priority task on core 1 | Keeps touch latency independent of rendering. The LCD interrupt runs on core 0. |
| PSRAM at 80 MHz, code executed from PSRAM | Stable baseline. 120 MHz is experimental in IDF and is still to be evaluated. |
| No LVGL yet | Undecided. Pixel-art rendering will be a custom dirty-rectangle blitter. LVGL may still be used for menus and settings later. |
| Console on UART0, mirrored to USB-Serial/JTAG | Logs available on either port |

## Known issues and caveats
- **Full-screen redraws are expensive** (46 ms). The rendering engine must use dirty rectangles. See the results doc.
- **The touch dot is drawn straight into the visible frame buffer.** Tearing is possible but was not observed. The real renderer should draw into the back buffer.
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

## Next steps (not started; to be approved one at a time)
1. **Rendering core:**
   - Back-buffer dirty-rectangle compositor with swaps synchronised to frame completion.
   - Integer-scaled sprite blitter.
   - Measure at 80 MHz, then evaluate 120 MHz PSRAM.
2. **Asset pipeline:**
   - PC-side Python tool that converts PNG/Aseprite art to palettised RGB565 sprite sheets.
   - Stored in a dedicated flash partition.
   - Final partition layout: NVS, two 3 MB OTA slots, about 9 MB of assets.
3. **Input and UI layer:** gestures and hit-testing on the touch task. Decide on LVGL for menus.
4. **WiFi:** local provisioning without cloud, OTA updates, then integrations.
