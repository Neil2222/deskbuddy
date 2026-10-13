#include "display.h"

#include <string.h>

#include "esp32s3/rom/cache.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "display";

typedef struct {
    uint16_t *fb[2];
    int px; // pixels per buffer
    int back; // presenting task only: the buffer being drawn

    // Shared with the scanout ISR; guarded by lock.
    int next;       // buffer to scan from the next frame start
    int scan;       // buffer the current frame is read from
    int readers[2]; // bounce fills currently copying from each buffer
    uint32_t frames;
    uint32_t resyncs; // frame starts that did not follow the previous frame's last chunk
    int last_end;     // pixel position right after the previous fill
    uint32_t fill_us_max;
    portMUX_TYPE lock;

    SemaphoreHandle_t idle; // given by the ISR when a buffer that is no longer scanned has no readers left

    // Presenting task only.
    display_stats_t stats;
    uint32_t frames_taken, resyncs_taken;
} display_state_t;

static DRAM_ATTR display_state_t s = {.lock = portMUX_INITIALIZER_UNLOCKED};

esp_err_t display_init(int width, int height)
{
    s.px = width * height;
    for (int i = 0; i < 2; i++) {
        // Cache-line aligned so dirty-rectangle rows (32-px aligned) map onto whole 64-byte lines.
        s.fb[i] = heap_caps_aligned_calloc(64, 1, (size_t)s.px * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s.fb[i]) {
            ESP_LOGE(TAG, "no PSRAM for frame buffer %d", i);
            return ESP_ERR_NO_MEM;
        }
    }
    s.idle = xSemaphoreCreateBinary();
    if (!s.idle) {
        return ESP_ERR_NO_MEM;
    }
    s.scan = s.next = 0;
    s.back = 1;
    return ESP_OK;
}

IRAM_ATTR bool display_bounce_fill(void *dst, int pos_px, int len_bytes, void *ctx)
{
    (void)ctx;
    const int64_t t0 = esp_timer_get_time();
    bool signal = false;
    const int n_px = len_bytes / (int)sizeof(uint16_t);

    portENTER_CRITICAL_ISR(&s.lock);
    if (pos_px == 0) { // frame start: the only point where the source buffer may change
        s.frames++;
        // Normally the previous fill ended the frame. Anything else is the driver's VSYNC resync restarting the
        // frame early after a PSRAM-starved underrun: still safe here, but a visible one-frame glitch.
        if (s.last_end != 0 && s.last_end != s.px) {
            s.resyncs++;
        }
        if (s.scan != s.next) {
            const int old = s.scan;
            s.scan = s.next;
            signal = s.readers[old] == 0; // otherwise the interrupted fill signals when it finishes
        }
    }
    s.last_end = pos_px + n_px;
    const int src = s.scan;
    s.readers[src]++;
    portEXIT_CRITICAL_ISR(&s.lock);

    memcpy(dst, s.fb[src] + pos_px, (size_t)len_bytes);
    // Warm the data cache for the next fill: the rest of this frame, or the start of the next one (a hint only).
    const uint16_t *ahead = pos_px + n_px < s.px ? s.fb[src] + pos_px + n_px : s.fb[s.next];
    Cache_Start_DCache_Preload((uint32_t)ahead, (uint32_t)len_bytes, 0);

    portENTER_CRITICAL_ISR(&s.lock);
    s.readers[src]--;
    if (s.readers[src] == 0 && src != s.scan) {
        signal = true; // a fill that started before the switch has finished with the old buffer
    }
    const uint32_t fill_us = (uint32_t)(esp_timer_get_time() - t0);
    if (fill_us > s.fill_us_max) {
        s.fill_us_max = fill_us;
    }
    portEXIT_CRITICAL_ISR(&s.lock);

    BaseType_t woken = pdFALSE;
    if (signal) {
        xSemaphoreGiveFromISR(s.idle, &woken);
    }
    return woken == pdTRUE;
}

uint16_t *display_back_buffer(void)
{
    return s.fb[s.back];
}

void display_present(void)
{
    const int target = s.back;
    const int old = target ^ 1;
    const int64_t t0 = esp_timer_get_time();

    portENTER_CRITICAL(&s.lock);
    s.next = target;
    portEXIT_CRITICAL(&s.lock);

    // The semaphore only says "check again": stale gives and timeouts are both harmless because completion is
    // decided by the state below, never by the wake-up itself.
    int64_t warned_at = 0;
    for (;;) {
        portENTER_CRITICAL(&s.lock);
        const bool done = s.scan == target && s.readers[old] == 0;
        portEXIT_CRITICAL(&s.lock);
        if (done) {
            break;
        }
        if (xSemaphoreTake(s.idle, pdMS_TO_TICKS(DISPLAY_SLOW_WAIT_MS)) != pdTRUE) {
            const int64_t waited_ms = (esp_timer_get_time() - t0) / 1000;
            if (waited_ms - warned_at >= 1000 || warned_at == 0) {
                ESP_LOGW(TAG, "scanout has not released the front buffer after %lld ms; still waiting",
                         (long long)waited_ms);
                warned_at = waited_ms;
            }
        }
    }

    const int64_t waited = esp_timer_get_time() - t0;
    s.stats.presents++;
    s.stats.wait_us_sum += waited;
    if (waited > s.stats.wait_us_max) {
        s.stats.wait_us_max = waited;
    }
    if (waited > DISPLAY_SLOW_WAIT_MS * 1000) {
        s.stats.slow_waits++;
    }
    s.back = old;
}

void display_stats_take(display_stats_t *out)
{
    portENTER_CRITICAL(&s.lock);
    const uint32_t frames = s.frames, resyncs = s.resyncs, fill_us_max = s.fill_us_max;
    s.fill_us_max = 0;
    portEXIT_CRITICAL(&s.lock);
    *out = s.stats;
    out->frames = frames - s.frames_taken;
    out->resyncs = resyncs - s.resyncs_taken;
    s.resyncs_taken = resyncs;
    s.frames_taken = frames;
    out->fill_us_max = fill_us_max;
    memset(&s.stats, 0, sizeof(s.stats));
}
