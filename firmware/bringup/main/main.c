// DeskBuddy board bring-up: test pattern, touch-following dot, and performance measurements.
// All results are logged on UART0 (COM via CH343) and mirrored to native USB-Serial/JTAG.
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <sys/param.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "bringup";

#define W BOARD_LCD_H_RES
#define H BOARD_LCD_V_RES
#define FB_BYTES (W * H * 2)
#define DOT_R 20

static board_t s_board;
static SemaphoreHandle_t s_frame_sem;
static volatile uint32_t s_frames;
static TaskHandle_t s_touch_task;
static volatile int64_t s_irq_us;

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// ---------------------------------------------------------------------------------------------
// ISR callbacks

static IRAM_ATTR bool on_frame_done(void *ctx)
{
    BaseType_t woken = pdFALSE;
    s_frames++;
    xSemaphoreGiveFromISR(s_frame_sem, &woken);
    return woken == pdTRUE;
}

static IRAM_ATTR void on_touch_irq(void *ctx)
{
    s_irq_us = esp_timer_get_time();
    if (s_touch_task) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_touch_task, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

// ---------------------------------------------------------------------------------------------
// Test pattern: color bars, RGB ramps, 1-px checkerboard, gray ramp, border and corner markers.
// Corners: top-left red, top-right green, bottom-left blue, bottom-right magenta.

static const uint16_t k_bars[8] = {
    0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000, // W Y C G M R B K
};

static uint16_t pattern_px(int x, int y)
{
    if (x == 0 || y == 0 || x == W - 1 || y == H - 1) {
        return 0xFFFF;
    }
    if (y < 32 && x < 32) return 0xF800;
    if (y < 32 && x >= W - 32) return 0x07E0;
    if (y >= H - 32 && x < 32) return 0x001F;
    if (y >= H - 32 && x >= W - 32) return 0xF81F;
    if (y < 300) return k_bars[x / (W / 8)];
    if (y < 350) return (uint16_t)((x * 32 / W) << 11);
    if (y < 400) return (uint16_t)((x * 64 / W) << 5);
    if (y < 450) return (uint16_t)(x * 32 / W);
    if (x < W / 2) return ((x ^ y) & 1) ? 0xFFFF : 0x0000;
    const uint8_t v = (uint8_t)((x - W / 2) * 255 / (W / 2 - 1));
    return rgb565(v, v, v);
}

static void pattern_rect(uint16_t *fb, int x0, int y0, int w, int h)
{
    for (int y = y0; y < y0 + h; y++) {
        uint16_t *row = fb + y * W;
        for (int x = x0; x < x0 + w; x++) {
            row[x] = pattern_px(x, y);
        }
    }
}

static void fill32(uint16_t *fb, uint16_t c, size_t px)
{
    uint32_t *p = (uint32_t *)fb;
    const uint32_t v = c | ((uint32_t)c << 16);
    for (size_t i = 0; i < px / 2; i++) {
        p[i] = v;
    }
}

// ---------------------------------------------------------------------------------------------
// Touch dot

typedef struct {
    int x, y;
    bool visible;
} dot_t;

static void clip_box(int cx, int cy, int *x0, int *y0, int *w, int *h)
{
    int a = cx - DOT_R, b = cy - DOT_R, c = cx + DOT_R + 1, d = cy + DOT_R + 1;
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (c > W) c = W;
    if (d > H) d = H;
    *x0 = a; *y0 = b; *w = c - a; *h = d - b;
}

static void dot_erase(uint16_t *fb, const dot_t *d)
{
    int x0, y0, w, h;
    clip_box(d->x, d->y, &x0, &y0, &w, &h);
    pattern_rect(fb, x0, y0, w, h);
}

static void dot_draw(uint16_t *fb, const dot_t *d)
{
    int x0, y0, w, h;
    clip_box(d->x, d->y, &x0, &y0, &w, &h);
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            const int dx = x - d->x, dy = y - d->y, r2 = dx * dx + dy * dy;
            if (r2 <= DOT_R * DOT_R) {
                fb[y * W + x] = r2 >= (DOT_R - 3) * (DOT_R - 3) ? 0x0000 : 0xFFE0;
            }
        }
    }
}

typedef struct {
    uint32_t n, n_interval;
    int64_t read_sum, read_max;
    int64_t draw_sum, draw_max;
    int64_t lat_sum, lat_max;
    int64_t interval_sum, interval_max;
    int step_max;        // largest pixel jump between consecutive reports
    uint32_t gaps_20ms;  // report intervals > 20 ms while the finger stays down
} touch_stats_t;

static void touch_stats_log(touch_stats_t *s)
{
    if (s->n == 0) {
        return;
    }
    const double rate = s->n_interval ? 1e6 * s->n_interval / (double)s->interval_sum : 0;
    ESP_LOGI(TAG, "TOUCH n=%" PRIu32 " report=%.1f Hz (max gap %" PRId64 " us, >20ms: %" PRIu32 ") | max jump %d px"
             " | i2c read avg %" PRId64 " max %" PRId64 " us | dot redraw avg %" PRId64 " max %" PRId64
             " us | IRQ->pixels in FB avg %" PRId64 " max %" PRId64 " us",
             s->n, rate, s->interval_max, s->gaps_20ms, s->step_max, s->read_sum / s->n, s->read_max,
             s->draw_sum / s->n, s->draw_max, s->lat_sum / s->n, s->lat_max);
    memset(s, 0, sizeof(*s));
}

static void touch_task(void *arg)
{
    uint16_t *fb = arg; // the frame buffer currently on screen
    dot_t dot = {0};
    touch_stats_t st = {0};
    int64_t prev_irq = 0;
    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0) {
            touch_stats_log(&st);
            continue;
        }
        const int64_t t_irq = s_irq_us;
        const int64_t t0 = esp_timer_get_time();
        esp_lcd_touch_read_data(s_board.touch);
        const int64_t t1 = esp_timer_get_time();
        esp_lcd_touch_point_data_t pt[5];
        uint8_t cnt = 0;
        esp_lcd_touch_get_data(s_board.touch, pt, &cnt, 5);

        if (cnt == 0) {
            if (dot.visible) {
                dot_erase(fb, &dot);
                dot.visible = false;
                ESP_LOGI(TAG, "touch up   at (%d,%d)", dot.x, dot.y);
            }
            continue;
        }
        const bool dot_was_down = dot.visible;
        if (!dot.visible) {
            ESP_LOGI(TAG, "touch down at (%u,%u) points=%u", pt[0].x, pt[0].y, cnt);
        } else {
            const int dx = abs((int)pt[0].x - dot.x), dy = abs((int)pt[0].y - dot.y);
            st.step_max = MAX(st.step_max, MAX(dx, dy));
            dot_erase(fb, &dot);
        }
        dot.x = pt[0].x;
        dot.y = pt[0].y;
        dot.visible = true;
        dot_draw(fb, &dot);
        const int64_t t2 = esp_timer_get_time();

        st.n++;
        st.read_sum += t1 - t0;
        st.read_max = MAX(st.read_max, t1 - t0);
        st.draw_sum += t2 - t1;
        st.draw_max = MAX(st.draw_max, t2 - t1);
        st.lat_sum += t2 - t_irq;
        st.lat_max = MAX(st.lat_max, t2 - t_irq);
        if (prev_irq && dot_was_down && t_irq - prev_irq < 500000) {
            const int64_t gap = t_irq - prev_irq;
            st.interval_sum += gap;
            st.interval_max = MAX(st.interval_max, gap);
            st.gaps_20ms += gap > 20000;
            st.n_interval++;
        }
        prev_irq = t_irq;
        if (st.n == 200) {
            touch_stats_log(&st);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Benchmarks

static void wait_frame(void)
{
    xSemaphoreTake(s_frame_sem, 0); // drop a stale completion
    xSemaphoreTake(s_frame_sem, pdMS_TO_TICKS(200));
}

static void show(uint16_t *fb)
{
    esp_lcd_panel_draw_bitmap(s_board.panel, 0, 0, W, H, fb);
    wait_frame();
}

static void bench_refresh(void)
{
    const uint32_t f0 = s_frames;
    const int64_t t0 = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(2000));
    const double hz = (s_frames - f0) * 1e6 / (double)(esp_timer_get_time() - t0);
    ESP_LOGI(TAG, "BENCH panel refresh: %.2f Hz (%.2f ms/frame)", hz, 1000.0 / hz);
}

static void bench_fullframe(uint16_t *back, const uint16_t *front)
{
    int64_t t = esp_timer_get_time();
    pattern_rect(back, 0, 0, W, H);
    int64_t dt = esp_timer_get_time() - t;
    ESP_LOGI(TAG, "BENCH full-frame test pattern (per-pixel fn): %.1f ms", dt / 1000.0);

    t = esp_timer_get_time();
    for (int i = 0; i < 10; i++) {
        fill32(back, (uint16_t)(i * 0x1111), W * H);
    }
    dt = (esp_timer_get_time() - t) / 10;
    ESP_LOGI(TAG, "BENCH full-frame solid fill (PSRAM): %.2f ms, %.1f MB/s", dt / 1000.0, FB_BYTES / (double)dt);

    t = esp_timer_get_time();
    for (int i = 0; i < 10; i++) {
        memcpy(back, front, FB_BYTES);
    }
    dt = (esp_timer_get_time() - t) / 10;
    ESP_LOGI(TAG, "BENCH full-frame memcpy PSRAM->PSRAM: %.2f ms, %.1f MB/s", dt / 1000.0, FB_BYTES / (double)dt);
}

// Double-buffered full-screen animation (scrolling bars), swap synchronised to frame completion.
static void bench_animation(void)
{
    uint16_t *line = heap_caps_malloc(W * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int back = 1;
    int64_t render_sum = 0, render_max = 0;
    uint32_t frames = 0;
    const int64_t t_end = esp_timer_get_time() + 5000000;
    const int64_t t_start = esp_timer_get_time();
    while (esp_timer_get_time() < t_end) {
        const int64_t t0 = esp_timer_get_time();
        for (int x = 0; x < W; x++) {
            line[x] = k_bars[((x + frames * 8) / (W / 8)) & 7];
        }
        uint16_t *fb = s_board.fb[back];
        for (int y = 0; y < H; y++) {
            memcpy(fb + y * W, line, W * 2);
        }
        const int64_t dt = esp_timer_get_time() - t0;
        render_sum += dt;
        render_max = MAX(render_max, dt);
        show(fb);
        back ^= 1;
        frames++;
    }
    const double fps = frames * 1e6 / (double)(esp_timer_get_time() - t_start);
    ESP_LOGI(TAG, "BENCH double-buffered full-screen animation: %.1f FPS, render avg %.1f ms max %.1f ms",
             fps, render_sum / 1000.0 / frames, render_max / 1000.0);
    free(line);
}

// Small-region costs relevant to sprites: 128x128 copy and 32x32 -> 128x128 nearest-neighbour 4x.
static void bench_sprites(uint16_t *fb)
{
    uint16_t *src = heap_caps_malloc(128 * 128 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint16_t line[128];
    for (int i = 0; i < 128 * 128; i++) {
        src[i] = (uint16_t)(i * 37);
    }
    const int iters = 200;
    int64_t t = esp_timer_get_time();
    for (int i = 0; i < iters; i++) {
        for (int y = 0; y < 128; y++) {
            memcpy(fb + (100 + y) * W + 100, src + y * 128, 256);
        }
    }
    const int64_t copy_us = (esp_timer_get_time() - t) / iters;

    t = esp_timer_get_time();
    for (int i = 0; i < iters; i++) {
        for (int sy = 0; sy < 32; sy++) {
            const uint16_t *s = src + sy * 32;
            for (int x = 0; x < 128; x++) {
                line[x] = s[x >> 2];
            }
            for (int r = 0; r < 4; r++) {
                memcpy(fb + (300 + sy * 4 + r) * W + 300, line, 256);
            }
        }
    }
    const int64_t scale_us = (esp_timer_get_time() - t) / iters;
    ESP_LOGI(TAG, "BENCH 128x128 blit: %" PRId64 " us | 32x32 sprite at 4x -> 128x128: %" PRId64 " us", copy_us, scale_us);
    free(src);
}

// Relative CPU available per core while the panel scans out (bounce-buffer copies run in an ISR).
static volatile bool s_spin_run;
static volatile uint32_t s_spin_count[2];

static void spin_task(void *arg)
{
    const int core = (int)arg;
    uint32_t n = 0;
    while (s_spin_run) {
        n++;
    }
    s_spin_count[core] = n;
    vTaskDelete(NULL);
}

static void bench_cpu(void)
{
    s_spin_run = true;
    xTaskCreatePinnedToCore(spin_task, "spin0", 2048, (void *)0, 1, NULL, 0);
    xTaskCreatePinnedToCore(spin_task, "spin1", 2048, (void *)1, 1, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(2000));
    s_spin_run = false;
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "BENCH spin loop core0=%" PRIu32 " core1=%" PRIu32 " -> core0 has %.1f%% of core1's free CPU",
             s_spin_count[0], s_spin_count[1], 100.0 * s_spin_count[0] / s_spin_count[1]);
}

// ---------------------------------------------------------------------------------------------

void app_main(void)
{
    const int64_t t_boot = esp_timer_get_time();
    s_frame_sem = xSemaphoreCreateBinary();
    const board_callbacks_t cbs = {.on_frame_done = on_frame_done, .on_touch_irq = on_touch_irq};
    ESP_ERROR_CHECK(board_init(&cbs, &s_board));

    uint16_t *fb0 = s_board.fb[0], *fb1 = s_board.fb[1];
    pattern_rect(fb0, 0, 0, W, H);
    show(fb0);
    ESP_ERROR_CHECK(board_backlight_set(80));
    ESP_LOGI(TAG, "pattern on screen %.0f ms after app start; free internal %u B, PSRAM %u B",
             (esp_timer_get_time() - t_boot) / 1000.0,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    bench_refresh();
    bench_cpu();
    bench_fullframe(fb1, fb0);
    bench_sprites(fb1);
    bench_animation();

    // Interactive phase: static pattern on fb0, dot drawn directly into the visible buffer.
    pattern_rect(fb0, 0, 0, W, H);
    show(fb0);
    xTaskCreatePinnedToCore(touch_task, "touch", 4096, fb0, 10, &s_touch_task, 1);
    ESP_LOGI(TAG, "READY: touch the screen");
}
