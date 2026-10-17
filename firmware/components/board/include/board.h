// Board support for the Waveshare ESP32-S3-Touch-LCD-7B (1024x600 RGB565 panel, GT911 touch).
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_NAME "waveshare-esp32-s3-touch-lcd-7b" // info board= and the firmware package's board
#define BOARD_LCD_H_RES 1024
#define BOARD_LCD_V_RES 600
#define BOARD_LCD_NUM_FBS 2

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
    uint16_t *fb[BOARD_LCD_NUM_FBS]; // RGB565 frame buffers owned by the panel driver (PSRAM); NULL with on_bounce_fill
} board_t;

typedef struct {
    // Called from ISR context once per scanned-out frame, after the driver latched the frame buffer
    // that will be shown next. Return true if a higher-priority task was woken.
    bool (*on_frame_done)(void *ctx);
    // Called from ISR context on the GT911 INT falling edge.
    void (*on_touch_irq)(void *ctx);
    // Optional. When set, the panel runs without driver-owned frame buffers (board_t.fb stays NULL) and
    // this is the only code that supplies pixels: called from ISR context (must be IRAM) to copy
    // len_bytes of RGB565 data, starting at pixel pos_px of the frame, into dst. pos_px == 0 marks the
    // start of a frame, including after the driver's VSYNC resync. Return true if a task was woken.
    bool (*on_bounce_fill)(void *dst, int pos_px, int len_bytes, void *ctx);
    void *ctx;
} board_callbacks_t;

// Brings up I2C, IO expander (native USB kept connected), panel, and touch. Backlight stays off.
esp_err_t board_init(const board_callbacks_t *cbs, board_t *out);

// 0 turns the backlight off; 1..100 sets brightness in percent.
esp_err_t board_backlight_set(uint8_t percent);

// Re-sends the IO expander's output register (same value: nothing changes). Observed on the device: after some
// backlight writes the expander leaves the shared I2C bus in a state where every touch-controller read fails,
// until the expander is addressed again. Call this when a touch read fails.
esp_err_t board_i2c_recover(void);

#ifdef __cplusplus
}
#endif
