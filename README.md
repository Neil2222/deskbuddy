# DeskBuddy

A standalone desk gadget with animated pixel-art characters, built on the Waveshare ESP32-S3-Touch-LCD-7B.

## Hardware (verified on the connected unit)

| Item | Value | Source |
|---|---|---|
| Module | ESP32-S3-WROOM-1-N16R8, chip rev v0.2, MAC 44:1b:f6:8d:11:78 | esptool |
| Flash | 16 MB, manufacturer 0x46 / device 0x4018, quad SPI | esptool `flash_id` |
| PSRAM | 8 MB octal, AP Memory gen 3, runs at 120 MHz in the factory firmware | factory boot log |
| Display | 1024×600 IPS panel on a 16-bit RGB565 parallel bus driven by the S3 LCD_CAM peripheral. The panel has no SPI command interface, and Waveshare lists an ST7262 as its timing controller. | schematic, Waveshare repo |
| RGB timing | PCLK 30.85 MHz, falling edge; HSYNC pulse 162, back porch 152, front porch 48; VSYNC pulse 45, back porch 13, front porch 3. That gives about 33.7 Hz refresh. | `13_lvgl_v8_demo/rgb_lcd_port` |
| Touch | GT911 at I2C 0x5D on SDA GPIO8 / SCL GPIO9 at 400 kHz. INT is GPIO4 (falling edge). RST goes through the IO expander (EXIO1). Firmware 1060, config version 91, report rate 8 ms. | factory boot log |
| IO expander | I2C 0x24. It is a Waveshare-programmed MCU, not the CH422G named in the docs, and it provides PWM and ADC. EXIO1 = TP_RST, EXIO2 = backlight enable, EXIO3 = LCD reset, EXIO4 = SD_CS, EXIO5 = USB_SEL, EXIO6 = LCD_VDD_EN. Register 0x05 sets backlight PWM and register 0x06 reads battery ADC. | schematic, Waveshare driver |
| USB, native port | USB-Serial/JTAG (303A:1001) on GPIO19/20, routed through an FSUSB42 mux. **EXIO5 high switches those pins to the CAN transceiver and the USB device disappears.** | schematic, observed |
| USB, UART port | CH343P (1A86:55D3) on UART0 (GPIO43/44) with DTR/RTS auto-reset. DIP switch #16 must select the ESP32 rather than the external UART header. | schematic, observed |
| eFuses | No secure boot, no flash encryption, download mode enabled, USB-JTAG enabled | `espefuse summary` |

## Toolchain setup (Windows)

ESP-IDF is pinned to **v5.5.5**. Waveshare's current LVGL examples require IDF 5.5 or later.

```powershell
git clone -b v5.5.5 --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git C:\esp\v5.5.5\esp-idf
C:\esp\v5.5.5\esp-idf\install.ps1 esp32s3
```

Set `DESKBUDDY_IDF_PATH` if IDF lives somewhere else. Vendor reference code is cloned into `_ref/` and is gitignored:

```powershell
git clone --depth 1 https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7B.git _ref/ESP32-S3-Touch-LCD-7B
```

## Build / flash

```powershell
tools/idf.ps1 -C <project> build
tools/idf.ps1 -C <project> -p COM6 flash monitor
```

Flash over the **UART** Type-C port (COM6 on the dev PC). Firmware that drives EXIO5 high, including the factory image, removes the native USB port at runtime.

## Factory firmware backup / restore

The full 16 MB dump is in `backup/factory_full_16MB.bin`. It is not in git.

- SHA-256: `fc48538dcb848ff87a321baaa90f073457cbedba8884b9784e65bf0dfdc18874`
- Factory app: project `rtc_pcf85063`, built with ESP-IDF v5.4-dev on 2025-06-20. It is a single 8 MB factory partition at 0x10000. It does **not** match any binary in Waveshare's repo, so this dump is the only way to restore it.

```powershell
tools/flash-backup.ps1 -Port COM6 -Action verify  -Image backup/factory_full_16MB.bin   # read-only check
tools/flash-backup.ps1 -Port COM6 -Action restore -Image backup/factory_full_16MB.bin   # rewrite + verify
```

Second copy (same SHA-256, with a `SHA256SUMS.txt` next to it): `%USERPROFILE%\Documents\DeskBuddy-backups\ESP32-S3-Touch-LCD-7B_441bf68d1178_factory_full_16MB.bin`.
