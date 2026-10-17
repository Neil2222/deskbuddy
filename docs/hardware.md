# Hardware: Waveshare ESP32-S3-Touch-LCD-7B

Verified on the dev unit (MAC 44:1b:f6:8d:11:78) unless marked otherwise.
Sources:
- [Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7B), [docs](https://docs.waveshare.com/ESP32-S3-Touch-LCD-7B/Instructions-For-Use), [examples](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7B)
- Schematic: `hardware/schematics` in the examples repo.

| Item | Value |
|---|---|
| Module | ESP32-S3-WROOM-1-N16R8, chip rev v0.2, 40 MHz crystal |
| Flash | 16 MB quad SPI (manufacturer ID 0x46, device ID 0x4018) |
| PSRAM | 8 MB octal (AP Memory, generation 3) |
| Display | 1024×600 IPS panel on a 16-bit RGB565 parallel bus driven by LCD_CAM. There is no SPI init interface. Waveshare lists an ST7262 timing controller (their FAQ's "ST7701" is wrong). |
| RGB timing | PCLK 30.85 MHz, falling edge. HSYNC pulse 162, back porch 152, front porch 48. VSYNC pulse 45, back porch 13, front porch 3. Measured refresh 33.50 Hz with the bring-up firmware, 33.7 Hz with the application (see the results docs). |
| Touch | GT911 at I2C 0x5D, firmware 1060, config version 91, 5 points |
| IO expander | Waveshare-programmed MCU at I2C 0x24. It provides PWM and ADC, which the CH422G named in the docs does not have. |
| Battery | CS8501 charger. Battery voltage is read through the expander ADC (register 0x06). Not used yet. |
| RTC | **None.** An I2C scan of 0x08–0x77 on the device (2026-10-14) finds only 0x24 (expander) and 0x5D (touch), as Waveshare's address map says. The `rtc_pcf85063` tag in the factory boot log comes from a test image shared with other boards. The HY2.0 header carries I2C if an RTC module is added later. |
| eFuses | No secure boot, no flash encryption, download mode enabled, USB-JTAG enabled |

## Pins

| Function | GPIO |
|---|---|
| RGB data B3..B7 | 14, 38, 18, 17, 10 |
| RGB data G2..G7 | 39, 0, 45, 48, 47, 21 |
| RGB data R3..R7 | 1, 2, 42, 41, 40 |
| HSYNC / VSYNC / DE / PCLK | 46 / 3 / 5 / 7 |
| I2C SDA / SCL (touch + expander) | 8 / 9 |
| Touch INT | 4 |
| Native USB D- / D+ (muxed with CAN RX/TX) | 19 / 20 |
| UART0 TX / RX (to CH343) | 43 / 44 |
| SD card MOSI / SCK / MISO (CS via expander EXIO4) | 11 / 12 / 13 |
| RS485 TX / RX | 15 / 16 |

GPIO0 is both the BOOT strap and RGB G3.

## IO expander (I2C 0x24)

Register map from Waveshare's `io_extension` driver:
- **0x02:** pin mode (1 = output).
- **0x03:** output latch.
- **0x04:** input.
- **0x05:** backlight PWM, 0–255. Inverted: 0 is brightest. Above about 97 % duty the panel goes fully dark. Changes after boot were not applied reliably on our board (see [rendering-results.md](rendering-results.md)), so the firmware sets it once at boot.
- **0x06:** ADC (16-bit).

| Pin | Function | Our default |
|---|---|---|
| EXIO1 | Touch reset (active low) | 1 |
| EXIO2 | Backlight enable | 0 until the first frame is drawn, then 1 |
| EXIO3 | LCD reset (active low) | 1 |
| EXIO4 | SD card CS (active low) | 1 |
| EXIO5 | USB_SEL: **0 = native USB**, 1 = CAN transceiver | **0** |
| EXIO6 | LCD_VDD_EN | 1 |
| EXIO0, EXIO7 | Unassigned | 1 (as in Waveshare's driver) |

## USB ports

- **UART Type-C:** CH343P USB-UART (`1A86:55D3`) on UART0, with DTR/RTS auto-reset into the bootloader. It goes through an FSUSB42 mux controlled by DIP switch #16. That switch must select the ESP32; the other position selects the external UART header.
- **USB Type-C:** ESP32-S3 USB-Serial/JTAG (`303A:1001`), through a second FSUSB42 mux controlled by EXIO5. Our firmware keeps it connected. Waveshare's examples set EXIO5 high, so the port disappears about 1 s after boot.
