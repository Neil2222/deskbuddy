// DeskBuddy firmware: the ESP32 platform around the shared core (components/deskbuddy_core).
//
// Tasks:  touch  (core 1, prio 10)  GT911 interrupt -> core touch events on a queue
//         render (core 1, prio 5)   events + time -> deskbuddy_core -> back buffer -> present
// The LCD scanout interrupts run on core 0 (board_init is called from app_main there).
// Everything about what is drawn and how the character behaves lives in the core, shared with the simulator; this
// file only provides the clock, the event queue, the frame buffers, the backlight and the statistics.
#include <inttypes.h>
#include <string.h>

#include "board.h"
#include "deskbuddy_core.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input.h"
#include "sdkconfig.h"

static const char *TAG = "deskbuddy";

#define W BOARD_LCD_H_RES
#define H BOARD_LCD_V_RES
#define STRIP_PX (16 * 1024) // 32 KB composition buffer in internal RAM
#define MAX_SLEEP_MS 1000    // wake at least this often (performance log)
#define RENDER_PRIO 5
#define RENDER_CORE 1

static board_t s_board;
static core_t s_core;

typedef struct {
    uint32_t renders, full_renders;
    int64_t render_us_sum, render_us_max;
    int64_t px_sum;
    int px_max;
    uint32_t touches;
    int64_t touch_us_sum, touch_us_max; // touch interrupt -> reaction frame starts scanning out
    uint32_t verified, mismatched;
} perf_t;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static int64_t max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

#ifdef CONFIG_DESKBUDDY_GFX_VERIFY
static gfx_color_t *s_verify_px;

static void verify_frame(const gfx_surface_t *frame, perf_t *perf)
{
    const gfx_surface_t scratch = {s_verify_px, W, H, W};
    int fx = -1, fy = -1;
    const uint32_t bad = core_verify(&s_core, frame, &scratch, &fx, &fy);
    perf->verified++;
    if (bad) {
        perf->mismatched++;
        ESP_LOGE(TAG, "VERIFY: %" PRIu32 " stale pixels, first at (%d,%d)", bad, fx, fy);
    }
}
#endif

static void perf_log(const perf_t *p, int64_t window_us)
{
    display_stats_t d;
    display_stats_take(&d);
    const double s = window_us / 1e6;
    const uint32_t n = p->renders ? p->renders : 1;
    const uint32_t presents = d.presents ? d.presents : 1;
    const uint32_t touches = p->touches ? p->touches : 1;
    ESP_LOGI(TAG,
             "PERF %.1fs: scanout %.2f Hz (resync %" PRIu32 ", fill max %" PRIu32 " us) | %" PRIu32 " frames (%.1f/s, %" PRIu32
             " full) | render avg %.2f max %.2f ms | redraw avg %" PRId64 " max %d px"
             " | swap wait avg %.1f max %.1f ms (slow %" PRIu32 ") | touch->scanout n=%" PRIu32 " avg %.1f max %.1f ms",
             s, d.frames / s, d.resyncs, d.fill_us_max, p->renders, p->renders / s, p->full_renders,
             p->render_us_sum / 1000.0 / n, p->render_us_max / 1000.0, p->px_sum / n, p->px_max,
             d.wait_us_sum / 1000.0 / presents, d.wait_us_max / 1000.0, d.slow_waits, p->touches,
             p->touch_us_sum / 1000.0 / touches, p->touch_us_max / 1000.0);
#ifdef CONFIG_DESKBUDDY_GFX_VERIFY
    ESP_LOGI(TAG, "VERIFY %.1fs: %" PRIu32 " frames checked, %" PRIu32 " mismatched", s, p->verified, p->mismatched);
#endif
}

static void render_task(void *arg)
{
    QueueHandle_t events = arg;
    perf_t perf = {0};
    int64_t perf_since = esp_timer_get_time();
    int64_t pending_touch_us = 0; // interrupt time of a touch whose reaction is not on screen yet
    uint32_t wait_ms = 0;

    for (;;) {
        input_event_t in;
        if (xQueueReceive(events, &in, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
            do {
                if (core_handle_event(&s_core, &in.ev) && !pending_touch_us) {
                    pending_touch_us = in.irq_us;
                }
            } while (xQueueReceive(events, &in, 0) == pdTRUE);
        }

        const uint32_t now = now_ms();
        const uint32_t next = core_update(&s_core, now);
        const gfx_dirty_t *redraw = core_collect(&s_core);
        if (redraw) {
            const gfx_surface_t back = {display_back_buffer(), W, H, W};
            const int64_t t0 = esp_timer_get_time();
            core_render(&s_core, redraw, &back);
            const int64_t render_us = esp_timer_get_time() - t0;
            if (s_core.count.frames == 1) {
                // Deterministic (idle pose, time 0 of every animation): must match the simulator's first frame.
                ESP_LOGI(TAG, "first full redraw %.1f ms, frame crc32 %08" PRIx32, render_us / 1000.0,
                         core_frame_crc32(&back));
            }
#ifdef CONFIG_DESKBUDDY_GFX_VERIFY
            verify_frame(&back, &perf);
#endif
            display_present();

            const int area = gfx_dirty_area(redraw);
            perf.renders++;
            perf.full_renders += area == W * H;
            perf.render_us_sum += render_us;
            perf.render_us_max = max64(perf.render_us_max, render_us);
            perf.px_sum += area;
            perf.px_max = area > perf.px_max ? area : perf.px_max;
            if (pending_touch_us) {
                const int64_t latency = esp_timer_get_time() - pending_touch_us;
                perf.touches++;
                perf.touch_us_sum += latency;
                perf.touch_us_max = max64(perf.touch_us_max, latency);
                pending_touch_us = 0;
            }
            // Both buffers hold a full frame now. The full redraws starve the scanout of PSRAM bandwidth (a resync
            // glitch), so they happen before the backlight comes on.
            if (s_core.count.frames == 2) {
                ESP_ERROR_CHECK(board_backlight_set(CONFIG_DESKBUDDY_BACKLIGHT_PERCENT));
                display_stats_t boot;
                display_stats_take(&boot);
                ESP_LOGI(TAG, "both buffers drawn, backlight on (while dark: %" PRIu32 " scanout resyncs, fill max %" PRIu32
                         " us)", boot.resyncs, boot.fill_us_max);
                memset(&perf, 0, sizeof(perf)); // performance windows cover the lit screen only
                perf_since = esp_timer_get_time();
            }
        }

        const int64_t t = esp_timer_get_time();
        if (CONFIG_DESKBUDDY_PERF_LOG_S > 0 && t - perf_since >= CONFIG_DESKBUDDY_PERF_LOG_S * 1000000LL) {
            perf_log(&perf, t - perf_since);
            memset(&perf, 0, sizeof(perf));
            perf_since = t;
        }

        // Sleep until the pose may change next, minus the time this iteration took.
        const uint32_t spent = now_ms() - now;
        wait_ms = next > spent ? next - spent : 0;
        if (wait_ms > MAX_SLEEP_MS) {
            wait_ms = MAX_SLEEP_MS;
        }
    }
}

void app_main(void)
{
    const int64_t t_boot = esp_timer_get_time();
    core_config_t cfg;
    core_config_defaults(&cfg);
    cfg.screen_w = W;
    cfg.screen_h = H;
    cfg.stage_percent = CONFIG_DESKBUDDY_STAGE_PERCENT;
#ifdef CONFIG_DESKBUDDY_PANEL_LEFT
    cfg.panel_left = true;
#endif
    cfg.seed = esp_random();
    cfg.demo_poke_ms = CONFIG_DESKBUDDY_DEMO_POKE_S * 1000u;

    gfx_color_t *strip = heap_caps_malloc(STRIP_PX * sizeof(gfx_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!strip || !core_init(&s_core, &cfg, strip, STRIP_PX, now_ms())) {
        ESP_LOGE(TAG, "core init failed (composition strip, layout or character)");
        abort();
    }
#ifdef CONFIG_DESKBUDDY_GFX_VERIFY
    s_verify_px = heap_caps_malloc(W * H * sizeof(gfx_color_t), MALLOC_CAP_SPIRAM);
    if (!s_verify_px) {
        ESP_LOGE(TAG, "no PSRAM for the verify buffer");
        abort();
    }
    ESP_LOGW(TAG, "GFX_VERIFY enabled: every frame is checked against a full redraw (slow)");
#endif
    ESP_ERROR_CHECK(display_init(W, H)); // before board_init: the scanout reads the buffers from its first frame

    const board_callbacks_t cbs = {.on_touch_irq = input_touch_irq, .on_bounce_fill = display_bounce_fill};
    ESP_ERROR_CHECK(board_init(&cbs, &s_board));

    QueueHandle_t events;
    ESP_ERROR_CHECK(input_start(s_board.touch, &events));
    if (xTaskCreatePinnedToCore(render_task, "render", 6144, events, RENDER_PRIO, NULL, RENDER_CORE) != pdPASS) {
        ESP_LOGE(TAG, "cannot start the render task");
        abort();
    }
    const layout_t *l = &s_core.layout;
    ESP_LOGI(TAG, "started in %.0f ms; free internal %u B, PSRAM %u B", (esp_timer_get_time() - t_boot) / 1000.0,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "stage %dx%d at x=%d, panel %dx%d at x=%d; character scale %d at (%d,%d)", l->stage.w, l->stage.h,
             l->stage.x, l->panel.w, l->panel.h, l->panel.x, s_core.view.scale, s_core.view.x0, s_core.view.y0);
}
