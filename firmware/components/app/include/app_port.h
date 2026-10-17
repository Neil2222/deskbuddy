// What the application (app.h) needs from the platform underneath: implemented once by the board firmware
// (firmware/deskbuddy/main/port.c) and once by the desktop simulator (firmware/simulator/sim_port.c).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Microseconds since start-up (monotonic).
int64_t app_port_time_us(void);

// A random number (seeds Buddy's behaviour and the first PDC click id).
uint32_t app_port_random(void);

// Random bytes from a true entropy source, for ids that must never repeat (the boot id). Called once per start.
void app_port_random_fill(void *buf, size_t len);

// Wall-clock time, UTC: seconds since 1970 and microseconds into the second. Starts near 1970 after power-up (no
// RTC); the companion sets it (time.set).
void app_port_wall_get(int64_t *sec, uint32_t *usec);
void app_port_wall_set(int64_t sec, uint32_t usec);

// Firmware version, as the companion sees it (hello fw=, evt boot fw=).
const char *app_port_version(void);

// A stable id of this device, the same after every restart (hello dev=): lower-case letters, digits and '-', at most
// 23 characters. The board: its MAC; the simulator: one per machine.
const char *app_port_device_id(void);

// What `info` reports after fw= (docs/protocol.md): space-separated key=value pairs, the same from start-up on, safe
// from any thread. build= (the Git commit, "-dirty" if built with uncommitted changes) and board= come first; the
// board adds the hardware it found and hw=ok or hw=mismatch (its self-check at start-up).
const char *app_port_info(void);

// Sends one complete line to the companion (a frame from link_format, newline included), in one piece: log lines
// must never end up inside it. Safe from any thread.
void app_port_send(const char *line, size_t len);

typedef enum {
    APP_MEM_INTERNAL, // fast internal RAM: may fail when full (the caller then falls back to APP_MEM_LARGE)
    APP_MEM_LARGE,    // PSRAM
} app_mem_t;

// Allocates memory that is never freed. NULL if there is none.
void *app_port_alloc(size_t size, app_mem_t kind);

// The frame buffer to draw the next frame into (width x height RGB565). Never shown until presented.
gfx_color_t *app_port_back_buffer(void);

// Writes rows just drawn into the back buffer out to where the display reads them (gfx_surface_t.flush).
void app_port_flush(const gfx_color_t *px, int count);

// Shows the back buffer and returns once the previous front buffer may be drawn into: it becomes the back buffer.
void app_port_present(void);

// Turns the backlight on (once both buffers are drawn). false: it failed; it is asked again shortly.
bool app_port_backlight_on(void);

typedef struct {
    uint32_t presents;
    uint32_t frames;      // frames shown by the panel
    uint32_t resyncs;     // frames restarted by a scanout underrun
    uint32_t fill_us_max; // longest scanout bounce fill
    uint32_t slow_waits;  // presents that waited unusually long
    int64_t wait_us_sum;  // time app_port_present() spent waiting
    int64_t wait_us_max;
    // Strip-copy timing (the board's bounce fills; zero where there is no scanout): fills counted, the latest start
    // (overall and while a frame was being drawn), the least time left before a strip was needed, fills that came
    // within 50 us of that (and how many of them while drawing), and fills estimated to have missed it.
    uint32_t fills_checked;
    uint32_t late_us_max, late_us_max_drawing;
    int32_t margin_us_min;
    uint32_t close, close_drawing, missed;
} app_scanout_stats_t;

// The scanout's statistics since the previous call (then reset). Called from the application's thread.
void app_port_scanout_stats(app_scanout_stats_t *out);

#ifndef ESP_PLATFORM
// Log output where ESP-IDF's log is not available (level: 'E', 'W', 'I'); see app_log.h.
void app_port_log(char level, const char *tag, const char *fmt, ...);
#endif

#ifdef __cplusplus
}
#endif
