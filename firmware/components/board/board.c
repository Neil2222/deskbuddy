#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "board";

// ---- Pins (schematic ESP32-S3-Touch-LCD-7B) ----
#define PIN_I2C_SDA GPIO_NUM_8
#define PIN_I2C_SCL GPIO_NUM_9
#define PIN_TP_INT  GPIO_NUM_4
#define I2C_HZ      (400 * 1000)

// ---- IO expander (Waveshare MCU at 0x24; register map from Waveshare's io_extension driver) ----
#define EXIO_ADDR     0x24
#define EXIO_REG_MODE 0x02 // bit = 1: output
#define EXIO_REG_OUT  0x03
#define EXIO_REG_PWM  0x05 // backlight PWM duty, 0..255, inverted (0 = brightest)

#define EXIO_TP_RST     (1u << 1)
#define EXIO_BL_EN      (1u << 2)
#define EXIO_LCD_RST    (1u << 3)
#define EXIO_SD_CS      (1u << 4)
#define EXIO_USB_SEL    (1u << 5) // 1 = GPIO19/20 to CAN transceiver, 0 = native USB. Must stay 0.
#define EXIO_LCD_VDD_EN (1u << 6)

// EXIO0/EXIO7 are unassigned on this board; keep them high like Waveshare's driver does.
#define EXIO_DEFAULT (0x01u | 0x80u | EXIO_TP_RST | EXIO_LCD_RST | EXIO_SD_CS | EXIO_LCD_VDD_EN)

// ---- RGB panel timing (Waveshare 13_lvgl_v8_demo) ----
#define LCD_PCLK_HZ        30850000
#define LCD_BOUNCE_LINES   10

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_exio;
static uint8_t s_exio_out = EXIO_DEFAULT;
static board_callbacks_t s_cbs;
// Expander writes come from the render task (backlight) and the touch task (board_i2c_recover).
static SemaphoreHandle_t s_exio_lock;
static StaticSemaphore_t s_exio_lock_buf;

// The expander shares the bus with the touch controller, whose task reads it about 110 times a second while a
// finger is down; an occasional transaction fails. Writes are idempotent, so retry a few times.
static esp_err_t exio_write(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = {reg, val};
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3 && err != ESP_OK; attempt++) {
        err = i2c_master_transmit(s_exio, buf, sizeof(buf), 50);
    }
    return err;
}

// Callers hold s_exio_lock, or run during board_init before any other task touches the expander.
static esp_err_t exio_update(uint8_t set, uint8_t clear)
{
    s_exio_out = (s_exio_out | set) & (uint8_t)~clear;
    return exio_write(EXIO_REG_OUT, s_exio_out);
}

static IRAM_ATTR bool on_frame_buf_complete(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *ctx)
{
    return s_cbs.on_frame_done ? s_cbs.on_frame_done(s_cbs.ctx) : false;
}

static IRAM_ATTR bool on_bounce_empty(esp_lcd_panel_handle_t panel, void *bounce_buf, int pos_px, int len_bytes, void *ctx)
{
    return s_cbs.on_bounce_fill(bounce_buf, pos_px, len_bytes, s_cbs.ctx);
}

static IRAM_ATTR void on_touch_irq(esp_lcd_touch_handle_t tp)
{
    if (s_cbs.on_touch_irq) {
        s_cbs.on_touch_irq(s_cbs.ctx);
    }
}

static esp_err_t init_io_expander(void)
{
    const i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = EXIO_ADDR,
        .scl_speed_hz = I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &dev, &s_exio), TAG, "add expander");
    // Latch the output value before switching pins to output mode so USB_SEL never glitches high.
    ESP_RETURN_ON_ERROR(exio_write(EXIO_REG_OUT, s_exio_out), TAG, "expander out");
    ESP_RETURN_ON_ERROR(exio_write(EXIO_REG_MODE, 0xFF), TAG, "expander mode");
    return exio_write(EXIO_REG_OUT, s_exio_out);
}

// GT911 address select: INT held low while RST rises -> 0x5D.
static esp_err_t reset_touch(void)
{
    const gpio_config_t out = {.pin_bit_mask = BIT64(PIN_TP_INT), .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&out), TAG, "tp int out");
    ESP_RETURN_ON_ERROR(gpio_set_level(PIN_TP_INT, 0), TAG, "tp int low");
    ESP_RETURN_ON_ERROR(exio_update(0, EXIO_TP_RST), TAG, "tp rst low");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(exio_update(EXIO_TP_RST, 0), TAG, "tp rst high");
    vTaskDelay(pdMS_TO_TICKS(10));
    // The GT911 driver reconfigures INT as an interrupt input; wait for its firmware to boot first.
    const gpio_config_t in = {.pin_bit_mask = BIT64(PIN_TP_INT), .mode = GPIO_MODE_INPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&in), TAG, "tp int in");
    vTaskDelay(pdMS_TO_TICKS(60));
    return ESP_OK;
}

static esp_err_t init_panel(board_t *b)
{
    const bool app_fb = s_cbs.on_bounce_fill != NULL;
    const esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_pulse_width = 162,
            .hsync_back_porch = 152,
            .hsync_front_porch = 48,
            .vsync_pulse_width = 45,
            .vsync_back_porch = 13,
            .vsync_front_porch = 3,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = app_fb ? 0 : BOARD_LCD_NUM_FBS,
        .bounce_buffer_size_px = BOARD_LCD_H_RES * LCD_BOUNCE_LINES,
        .dma_burst_size = 64,
        .hsync_gpio_num = GPIO_NUM_46,
        .vsync_gpio_num = GPIO_NUM_3,
        .de_gpio_num = GPIO_NUM_5,
        .pclk_gpio_num = GPIO_NUM_7,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {
            GPIO_NUM_14, GPIO_NUM_38, GPIO_NUM_18, GPIO_NUM_17, GPIO_NUM_10,              // B3..B7
            GPIO_NUM_39, GPIO_NUM_0, GPIO_NUM_45, GPIO_NUM_48, GPIO_NUM_47, GPIO_NUM_21,  // G2..G7
            GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_40,                // R3..R7
        },
        .flags.fb_in_psram = !app_fb,
        .flags.no_fb = app_fb,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &b->panel), TAG, "new rgb panel");
    const esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_frame_buf_complete = on_frame_buf_complete,
        .on_bounce_empty = app_fb ? on_bounce_empty : NULL,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_register_event_callbacks(b->panel, &cbs, NULL), TAG, "panel cbs");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(b->panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(b->panel), TAG, "panel init");
    if (app_fb) {
        b->fb[0] = b->fb[1] = NULL;
        return ESP_OK;
    }
    void *fb0, *fb1;
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_get_frame_buffer(b->panel, 2, &fb0, &fb1), TAG, "get fbs");
    b->fb[0] = fb0;
    b->fb[1] = fb1;
    return ESP_OK;
}

static esp_err_t init_touch(board_t *b)
{
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = I2C_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c, &io_cfg, &io), TAG, "touch io");
    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC, // reset is on the IO expander, handled by reset_touch()
        .int_gpio_num = PIN_TP_INT,
        .levels = {.reset = 0, .interrupt = 0},
        .interrupt_callback = on_touch_irq,
    };
    return esp_lcd_touch_new_i2c_gt911(io, &cfg, &b->touch);
}

esp_err_t board_init(const board_callbacks_t *cbs, board_t *out)
{
    ESP_RETURN_ON_FALSE(cbs && out, ESP_ERR_INVALID_ARG, TAG, "null arg");
    s_cbs = *cbs;
    s_exio_lock = xSemaphoreCreateMutexStatic(&s_exio_lock_buf);

    const i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_i2c), TAG, "i2c bus");
    ESP_RETURN_ON_ERROR(init_io_expander(), TAG, "io expander");
    ESP_LOGI(TAG, "IO expander out=0x%02x (USB_SEL=0: native USB kept)", s_exio_out);
    ESP_RETURN_ON_ERROR(reset_touch(), TAG, "touch reset");
    ESP_RETURN_ON_ERROR(init_panel(out), TAG, "panel");
    ESP_RETURN_ON_ERROR(init_touch(out), TAG, "touch");
    return ESP_OK;
}

esp_err_t board_backlight_set(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    if (percent > 0 && percent < 3) {
        percent = 3; // Waveshare: duty >= 97 % (inverted) turns the panel fully dark
    }
    xSemaphoreTake(s_exio_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (percent == 0) {
        err = exio_update(0, EXIO_BL_EN);
    } else {
        err = exio_write(EXIO_REG_PWM, (uint8_t)((100 - percent) * 255 / 100));
        if (err == ESP_OK) {
            err = exio_update(EXIO_BL_EN, 0);
        }
    }
    xSemaphoreGive(s_exio_lock);
    return err;
}

esp_err_t board_i2c_recover(void)
{
    xSemaphoreTake(s_exio_lock, portMAX_DELAY);
    const esp_err_t err = exio_write(EXIO_REG_OUT, s_exio_out); // same value: the outputs do not change
    xSemaphoreGive(s_exio_lock);
    return err;
}
