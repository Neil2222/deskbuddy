// Double-buffered display output with application-owned frame buffers.
//
// The RGB panel runs without driver frame buffers. display_bounce_fill() (registered as the board's
// on_bounce_fill) is the only code that reads a frame buffer: it copies the buffer selected for the current frame
// into the driver's internal-RAM bounce buffers. It selects a buffer only at the start of a frame (pos_px == 0,
// including after the driver's VSYNC resync), so each scanned frame comes from exactly one buffer: no tearing.
//
// display_present() hands the back buffer to the scanout and returns only once the previous front buffer is
// provably idle: the scanout has switched to the new buffer at a frame start AND no bounce fill that could still
// be reading the old buffer is in progress (fills may nest across the LCD and DMA interrupts). Both facts are read
// together under the scanout's spinlock. A timeout never counts as completion: it is logged and the wait goes on.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t presents;
    uint32_t frames;        // frames started by the scanout (~33.7 per second)
    uint32_t resyncs;       // frames restarted early by the driver's VSYNC resync (scanout underrun recovery)
    uint32_t fill_us_max;   // longest bounce-buffer fill; must stay below one bounce period (449 us at 10 lines)
    uint32_t slow_waits;    // presents that waited longer than DISPLAY_SLOW_WAIT_MS
    int64_t wait_us_sum;    // time present() spent waiting for the swap
    int64_t wait_us_max;
} display_stats_t;

#define DISPLAY_SLOW_WAIT_MS 100 // > 3 panel frames: something is wrong with the scanout

// Allocates two width x height RGB565 frame buffers in PSRAM (cleared to black). Buffer 0 is scanned first.
// Call before board_init() so the scanout has pixels from its first frame.
esp_err_t display_init(int width, int height);

// Board on_bounce_fill callback (IRAM, ISR context). ctx is unused.
bool display_bounce_fill(void *dst, int pos_px, int len_bytes, void *ctx);

// The buffer to draw the next frame into. Never read by the scanout until presented.
uint16_t *display_back_buffer(void);

// Shows the back buffer from the next frame start, then waits until the old front buffer is idle; it becomes
// the new back buffer. Blocks for up to about one panel frame (29.85 ms) in normal operation.
void display_present(void);

// Copies the statistics accumulated since the previous call and resets them. Call from the presenting task.
void display_stats_take(display_stats_t *out);

#ifdef __cplusplus
}
#endif
