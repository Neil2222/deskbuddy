// app_port.h on the board: ESP-IDF timer and clock, internal RAM and PSRAM, the display's frame buffers, the IO
// expander's backlight, and the console UART to the companion.
#include <stdio.h>
#include <sys/time.h>

#include "app_port.h"
#include "board.h"
#include "bootloader_random.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_cache.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static const char *TAG = "deskbuddy";

int64_t app_port_time_us(void)
{
    return esp_timer_get_time();
}

uint32_t app_port_random(void)
{
    return esp_random();
}

// Without Wi-Fi or Bluetooth the hardware RNG only gets true noise while the SAR ADC entropy source is on; nothing
// else uses the ADC (or the radio), so it can be switched on just for this.
void app_port_random_fill(void *buf, size_t len)
{
    bootloader_random_enable();
    esp_fill_random(buf, len);
    bootloader_random_disable();
}

void app_port_wall_get(int64_t *sec, uint32_t *usec)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *sec = tv.tv_sec;
    *usec = (uint32_t)tv.tv_usec;
}

void app_port_wall_set(int64_t sec, uint32_t usec)
{
    const struct timeval tv = {.tv_sec = (time_t)sec, .tv_usec = (suseconds_t)usec};
    settimeofday(&tv, NULL);
}

const char *app_port_version(void)
{
    return esp_app_get_description()->version;
}

const char *app_port_device_id(void)
{
    static char id[13];
    if (!id[0]) {
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac); // the factory MAC: the same on every boot
        snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    return id;
}

void app_port_send(const char *line, size_t len)
{
    // One write() of the whole line: the console driver holds its lock for the whole buffer, so log lines from
    // other tasks cannot end up in the middle of a frame.
    fwrite(line, 1, len, stdout);
    fflush(stdout);
}

void *app_port_alloc(size_t size, app_mem_t kind)
{
    return heap_caps_malloc(size, kind == APP_MEM_INTERNAL ? MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
                                                           : MALLOC_CAP_SPIRAM);
}

gfx_color_t *app_port_back_buffer(void)
{
    display_set_drawing(true); // the renderer asks for the buffer when it starts drawing a frame
    return display_back_buffer();
}

// Writes the rows the renderer just drew from the data cache to PSRAM on the render core. Left in the cache, they
// would be written out whenever something evicts those lines, mostly the scanout's bounce fills (which read through
// the same cache), pushing fills past their time budget. Rows start and end on cache lines (32-px aligned dirty
// rectangles in 64-byte aligned buffers); the outward rounding only guards that, and writing back a neighbouring
// line is harmless.
void app_port_flush(const gfx_color_t *px, int count)
{
    const uintptr_t line = CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE;
    const uintptr_t start = (uintptr_t)px & ~(line - 1);
    const uintptr_t end = ((uintptr_t)(px + count) + line - 1) & ~(line - 1);
    ESP_ERROR_CHECK(esp_cache_msync((void *)start, end - start,
                                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA));
}

void app_port_present(void)
{
    display_set_drawing(false);
    display_present();
}

// At the configured brightness. A failed write (the I2C bus is shared with the touch controller) is logged; the
// application asks again.
bool app_port_backlight_on(void)
{
    const esp_err_t err = board_backlight_set(CONFIG_DESKBUDDY_BACKLIGHT_PERCENT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "backlight on: %s, retrying", esp_err_to_name(err));
        return false;
    }
    return true;
}

void app_port_scanout_stats(app_scanout_stats_t *out)
{
    display_stats_t d;
    display_stats_take(&d);
    *out = (app_scanout_stats_t){d.presents, d.frames, d.resyncs, d.fill_us_max, d.slow_waits, d.wait_us_sum,
                                 d.wait_us_max, d.fills_checked, d.late_us_max, d.late_us_max_drawing,
                                 d.margin_us_min, d.close, d.close_drawing, d.missed};
}
