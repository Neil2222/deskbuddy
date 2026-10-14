// DeskBuddy: placeholder character on the rendering engine, the Office Mode desk clock, and the serial link to the
// Windows companion.
//
// Tasks:  touch  (core 1, prio 10)  GT911 interrupt -> input events
//         render (core 1, prio 5)   events + time -> character pose or clock -> dirty rectangles -> back buffer
//                                   -> present; runs companion commands and replies
//         link   (core 0, prio 4)   console UART -> companion commands -> input events
// The LCD scanout interrupts run on core 0 (board_init is called from app_main there).
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "board.h"
#include "buddy_brain.h"
#include "buddy_view.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx_repair.h"
#include "gfx_scene.h"
#include "input.h"
#include "layout.h"
#include "office_clock.h"
#include "office_switch.h"
#include "serial_link.h"
#include "sdkconfig.h"

static const char *TAG = "deskbuddy";

#define W BOARD_LCD_H_RES
#define H BOARD_LCD_V_RES
#define X_ALIGN 32           // dirty rectangles start/end on 64-byte PSRAM cache lines
#define STRIP_PX (16 * 1024) // 32 KB composition buffer in internal RAM
#define MAX_SLEEP_MS 1000    // wake at least this often (performance log)
#define RENDER_PRIO 5
#define RENDER_CORE 1
#define MOON_SCALE 5         // the Office Mode button: a small moon, 65 px

#ifdef CONFIG_DESKBUDDY_PANEL_LEFT
#define PANEL_LEFT true
#else
#define PANEL_LEFT false
#endif

static board_t s_board;
static layout_t s_layout;
static gfx_scene_t s_scene;

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

// Draws the whole scene into a scratch buffer and compares it with the incrementally repaired frame.
static void verify_frame(const gfx_surface_t *frame, perf_t *perf)
{
    gfx_dirty_t all;
    gfx_dirty_init(&all, W, H, X_ALIGN);
    gfx_dirty_add(&all, s_layout.screen);
    const gfx_surface_t ref = {s_verify_px, W, H, W};
    gfx_scene_render(&s_scene, &all, &ref);
    uint32_t bad = 0;
    int fx = -1, fy = -1;
    for (int y = 0; y < H; y++) {
        const gfx_color_t *a = frame->px + (size_t)y * W, *b = ref.px + (size_t)y * W;
        if (memcmp(a, b, W * sizeof(gfx_color_t)) == 0) {
            continue;
        }
        for (int x = 0; x < W; x++) {
            if (a[x] != b[x]) {
                if (bad++ == 0) {
                    fx = x;
                    fy = y;
                }
            }
        }
    }
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

// Turns a touch-down into a character event: the Monster button in the control panel, or a poke on the robot.
static bool touch_event(const buddy_view_t *view, const buddy_button_t *button, bool has_button,
                        const input_event_t *ev, buddy_event_t *out)
{
    if (has_button && buddy_button_hit(button, ev->x, ev->y)) {
        *out = BUDDY_EVENT_DRINK_MONSTER;
        return true;
    }
    if (buddy_view_hit(view, ev->x, ev->y)) {
        *out = BUDDY_EVENT_POKE;
        return true;
    }
    return false;
}

// The time zone arrives with the time (the companion's time.set); until then the clock counts as not set.
static link_tz_t s_tz;
static bool s_tz_known;

// Local wall-clock time, if the companion has set the clock (see office_time_valid()). ms: into the current second.
static bool wall_clock(struct tm *tm, uint32_t *ms)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (!s_tz_known || !office_time_valid(tv.tv_sec)) {
        return false;
    }
    const time_t local = tv.tv_sec + (time_t)link_tz_offset_min(&s_tz, tv.tv_sec) * 60;
    gmtime_r(&local, tm);
    *ms = (uint32_t)(tv.tv_usec / 1000);
    return true;
}

// What the companion shows as the device mode.
static const char *mode_name(const office_switch_t *screen, const buddy_brain_t *brain)
{
    if (screen->office) {
        return "office";
    }
    return brain->state == BUDDY_STATE_MONSTER ? "monster" : "normal";
}

// Runs a companion command (on the render task, which owns the character and the scene) and replies.
// lit: the screen is up (touches are only taken from then on, too).
static void run_command(const link_command_t *c, buddy_brain_t *brain, office_switch_t *screen, bool has_office,
                        bool lit, uint32_t now)
{
    char body[96] = "ok";
    switch (c->cmd) {
    case LINK_CMD_STATUS: {
        struct tm tm;
        uint32_t ms;
        snprintf(body, sizeof(body), "ok mode=%s time=%s uptime=%" PRIu32, mode_name(screen, brain),
                 wall_clock(&tm, &ms) ? "set" : "unset", now / 1000);
        break;
    }
    case LINK_CMD_POKE:
    case LINK_CMD_MONSTER: {
        const buddy_event_t be = c->cmd == LINK_CMD_POKE ? BUDDY_EVENT_POKE : BUDDY_EVENT_DRINK_MONSTER;
        const bool switching = office_switch_busy(screen) || screen->want_office != screen->office;
        if (!lit || screen->office || switching || !buddy_brain_event(brain, be, now)) {
            snprintf(body, sizeof(body), "err busy mode=%s", mode_name(screen, brain));
        } else {
            ESP_LOGI(TAG, "%s (companion)", c->cmd == LINK_CMD_POKE ? "Poke" : "Monster time");
        }
        break;
    }
    case LINK_CMD_MODE_SET:
        if (!has_office) {
            strcpy(body, "err unavailable");
        } else if (!lit) {
            snprintf(body, sizeof(body), "err busy mode=%s", mode_name(screen, brain));
        } else {
            office_switch_request(screen, c->u.office);
            ESP_LOGI(TAG, "Office Mode %s (companion)", c->u.office ? "on" : "off");
        }
        break;
    case LINK_CMD_TIME_SET: {
        const struct timeval tv = {.tv_sec = (time_t)c->u.time.utc, .tv_usec = c->u.time.ms * 1000};
        settimeofday(&tv, NULL);
        s_tz = c->u.time.tz;
        s_tz_known = true;
        struct tm tm;
        uint32_t ms;
        if (wall_clock(&tm, &ms)) {
            ESP_LOGI(TAG, "clock set by the companion: %04d-%02d-%02d %02d:%02d:%02d (UTC%+" PRId32 " min)",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                     link_tz_offset_min(&s_tz, tv.tv_sec));
        }
        break;
    }
    default: // hello and ping are answered by the serial task
        strcpy(body, "err unknown_cmd");
        break;
    }
    serial_link_send(c->seq, body);
}

#define BACKLIGHT_RETRY_MS 20

// Turns the backlight on at the configured brightness, once both buffers are drawn. A failed write (the I2C bus is
// shared with the touch controller) is logged and left for the next call instead of aborting: the caller keeps
// calling until it returns true. The backlight is never changed after that: the IO expander that drives it did not
// apply brightness changes reliably (see docs/rendering-results.md).
static bool backlight_on(bool *on)
{
    if (*on) {
        return true;
    }
    const esp_err_t err = board_backlight_set(CONFIG_DESKBUDDY_BACKLIGHT_PERCENT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "backlight on: %s, retrying", esp_err_to_name(err));
        return false;
    }
    *on = true;
    return true;
}

static void render_task(void *arg)
{
    QueueHandle_t events = arg;
    static gfx_dirty_t changes, redraw;
    static gfx_repair_t repair;
    static buddy_brain_t brain;
    static buddy_view_t view;
    static buddy_button_t button;
    static office_clock_t clock;
    static office_button_t moon;
    static office_switch_t screen;
    gfx_dirty_init(&changes, W, H, X_ALIGN);
    gfx_dirty_init(&redraw, W, H, X_ALIGN);
    gfx_repair_init(&repair, W, H, X_ALIGN);

    const uint32_t t_start = now_ms();
    if (!buddy_view_init(&view, &s_scene, s_layout.stage, 10, t_start)) {
        ESP_LOGE(TAG, "character does not fit the stage or art is invalid");
        vTaskDelete(NULL);
        return;
    }
    const bool has_button = buddy_button_init(&button, &s_scene, s_layout.panel, view.scale, 10);
    if (!has_button) {
        ESP_LOGW(TAG, "no room for the Monster button in the control panel");
    }
    // The moon sits at the bottom of the control panel, below the Monster can.
    const int moon_top = s_layout.panel.h * 78 / 100;
    const gfx_rect_t moon_area = {s_layout.panel.x, s_layout.panel.y + moon_top, s_layout.panel.w,
                                  s_layout.panel.h - moon_top};
    const bool has_moon = office_button_init(&moon, &s_scene, moon_area, MOON_SCALE, 10);
    // The glyph cache is only read when the clock redraws (rarely): PSRAM.
    uint8_t *clock_mem = heap_caps_malloc(OFFICE_CLOCK_MEMORY, MALLOC_CAP_SPIRAM);
    const bool has_office =
        has_moon && office_clock_init(&clock, &s_scene, s_layout.screen, clock_mem, OFFICE_CLOCK_MEMORY, 20);
    if (!has_office) {
        ESP_LOGW(TAG, "Office Mode unavailable (no room for the moon button, or no memory for the clock)");
        office_button_show(&moon, false);
    }
    office_switch_init(&screen);

    buddy_brain_init(&brain, t_start, esp_random());
    gfx_scene_invalidate(&s_scene, s_layout.screen); // both buffers start black: first two frames are full
    ESP_LOGI(TAG, "stage %dx%d at x=%d, panel %dx%d at x=%d; character scale %d at (%d,%d)", s_layout.stage.w,
             s_layout.stage.h, s_layout.stage.x, s_layout.panel.w, s_layout.panel.h, s_layout.panel.x, view.scale,
             view.x0, view.y0);

    perf_t perf = {0};
    int64_t perf_since = esp_timer_get_time();
    int64_t pending_touch_us = 0; // interrupt time of a touch whose reaction is not on screen yet
    uint32_t presented = 0;
    bool backlight = false; // board_init leaves the backlight off
    uint32_t wait_ms = 0;
    const char *mode = NULL; // last mode reported to the companion
#if CONFIG_DESKBUDDY_DEMO_POKE_S > 0
    uint32_t next_demo = t_start + CONFIG_DESKBUDDY_DEMO_POKE_S * 1000u;
#endif
#if CONFIG_DESKBUDDY_DEMO_MONSTER_S > 0
    uint32_t next_monster = t_start + CONFIG_DESKBUDDY_DEMO_MONSTER_S * 1000u;
#endif
#if CONFIG_DESKBUDDY_DEMO_OFFICE_S > 0
    uint32_t next_office = t_start + CONFIG_DESKBUDDY_DEMO_OFFICE_S * 1000u;
#endif

    for (;;) {
        input_event_t ev;
        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
            do {
                if (ev.type == INPUT_COMMAND) {
                    run_command(&ev.command, &brain, &screen, has_office, presented >= 2, now_ms());
                    continue;
                }
                // Touches count once the screen is lit and no scene switch is under way or requested.
                if (presented < 2 || office_switch_busy(&screen) || screen.want_office != screen.office) {
                    continue;
                }
                // Office Mode toggles on release (finger lifted off the moon or the clock); the robot and the can
                // react on touch-down.
                buddy_event_t be;
                if (screen.office) {
                    if (ev.type == INPUT_UP) {
                        office_switch_request(&screen, false); // any tap brings DeskBuddy back
                        ESP_LOGI(TAG, "Office Mode off");
                    }
                } else if (ev.type == INPUT_UP) {
                    if (has_office && office_button_hit(&moon, ev.x, ev.y)) {
                        office_switch_request(&screen, true);
                        ESP_LOGI(TAG, "Office Mode on");
                    }
                } else if (ev.type == INPUT_DOWN && touch_event(&view, &button, has_button, &ev, &be) &&
                           buddy_brain_event(&brain, be, now_ms())) {
                    if (be == BUDDY_EVENT_DRINK_MONSTER) {
                        ESP_LOGI(TAG, "Monster time (touch)");
                    }
                    if (!pending_touch_us) {
                        pending_touch_us = ev.irq_us;
                    }
                }
            } while (xQueueReceive(events, &ev, 0) == pdTRUE);
        }

        const uint32_t now = now_ms();
#if CONFIG_DESKBUDDY_DEMO_POKE_S > 0
        if ((int32_t)(now - next_demo) >= 0) {
            buddy_brain_event(&brain, BUDDY_EVENT_POKE, now);
            next_demo = now + CONFIG_DESKBUDDY_DEMO_POKE_S * 1000u;
        }
#endif
#if CONFIG_DESKBUDDY_DEMO_MONSTER_S > 0
        if ((int32_t)(now - next_monster) >= 0) {
            if (!screen.office && buddy_brain_event(&brain, BUDDY_EVENT_DRINK_MONSTER, now)) {
                ESP_LOGI(TAG, "Monster time (demo)");
            }
            next_monster = now + CONFIG_DESKBUDDY_DEMO_MONSTER_S * 1000u;
        }
#endif
#if CONFIG_DESKBUDDY_DEMO_OFFICE_S > 0
        if ((int32_t)(now - next_office) >= 0 && has_office && presented >= 2) {
            office_switch_request(&screen, !screen.want_office);
            ESP_LOGI(TAG, "Office Mode %s (demo)", screen.want_office ? "on" : "off");
            next_office = now + CONFIG_DESKBUDDY_DEMO_OFFICE_S * 1000u;
        }
#endif

        // Scene switch: the scene changes and both buffers are redrawn.
        const office_step_t step = office_switch_step(&screen);
        const bool light_ok = presented < 2 || backlight_on(&backlight);
        if (step.swap) {
            if (screen.office) {
                buddy_view_hide(&view); // the character pauses; a running Monster sequence is dropped on return
                buddy_button_apply(&button, false, false);
                office_button_show(&moon, false);
                office_clock_show(&clock, true);
                s_scene.background = office_background;
                s_scene.background_ctx = NULL;
            } else {
                office_clock_show(&clock, false);
                office_button_show(&moon, true);
                s_scene.background = layout_background;
                s_scene.background_ctx = &s_layout;
                buddy_brain_rest(&brain, now);
            }
        }
        if (step.redraw_all) {
            gfx_scene_invalidate(&s_scene, s_layout.screen);
        } else if (step.redraw_kick) {
            gfx_scene_invalidate(&s_scene, (gfx_rect_t){0, 0, 1, 1}); // the repair redraws the other buffer fully
        }
        if (step.redrawn) {
            // Like at boot: report the full redraws separately, so the performance windows cover normal frames.
            display_stats_t sw;
            display_stats_take(&sw);
            ESP_LOGI(TAG, "%s drawn (%" PRIu32 " scanout resyncs, fill max %" PRIu32 " us)",
                     screen.office ? "clock" : "DeskBuddy", sw.resyncs, sw.fill_us_max);
            memset(&perf, 0, sizeof(perf));
            perf_since = esp_timer_get_time();
        }

        uint32_t next;
        if (screen.office) {
            struct tm tm;
            uint32_t ms = 0;
            const bool known = wall_clock(&tm, &ms);
            next = office_clock_apply(&clock, known ? &tm : NULL, ms);
        } else {
            buddy_pose_t pose;
            next = buddy_brain_tick(&brain, now, &pose);
            const uint32_t view_next = buddy_view_apply(&view, &pose, now);
            next = view_next < next ? view_next : next;
            if (has_button) {
                buddy_button_apply(&button, true, pose.state != BUDDY_STATE_MONSTER);
            }
        }
        next = step.next_ms < next ? step.next_ms : next;
        next = !light_ok && next > BACKLIGHT_RETRY_MS ? BACKLIGHT_RETRY_MS : next;

        const char *new_mode = mode_name(&screen, &brain);
        if (new_mode != mode) {
            if (mode) {
                char body[32];
                snprintf(body, sizeof(body), "evt mode mode=%s", new_mode);
                serial_link_send(0, body);
            }
            mode = new_mode;
        }

        gfx_dirty_clear(&changes);
        gfx_scene_collect(&s_scene, &changes);
        if (changes.count > 0) {
            gfx_repair_next(&repair, &changes, &redraw);
            const gfx_surface_t back = {display_back_buffer(), W, H, W};
            const int64_t t0 = esp_timer_get_time();
            gfx_scene_render(&s_scene, &redraw, &back);
            const int64_t render_us = esp_timer_get_time() - t0;
#ifdef CONFIG_DESKBUDDY_GFX_VERIFY
            verify_frame(&back, &perf);
#endif
            display_present();
            office_switch_presented(&screen);

            const int area = gfx_dirty_area(&redraw);
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
            // Bring both buffers up to date before the backlight comes on. The first frame was a full redraw and
            // is in the repair history, so any change makes the second buffer redraw fully too: request one now.
            // Full redraws starve the scanout of PSRAM bandwidth (a resync glitch); this keeps them in the dark.
            if (++presented == 1) {
                ESP_LOGI(TAG, "first full redraw %.1f ms", render_us / 1000.0);
                gfx_scene_invalidate(&s_scene, (gfx_rect_t){0, 0, 1, 1});
            } else if (presented == 2) {
                backlight_on(&backlight); // retried by the loop if it fails
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
        wait_ms = next > spent && presented != 1 ? next - spent : 0;
#if CONFIG_DESKBUDDY_DEMO_POKE_S > 0
        const int32_t to_demo = (int32_t)(next_demo - now_ms());
        wait_ms = to_demo <= 0 ? 0 : ((uint32_t)to_demo < wait_ms ? (uint32_t)to_demo : wait_ms);
#endif
#if CONFIG_DESKBUDDY_DEMO_MONSTER_S > 0
        const int32_t to_monster = (int32_t)(next_monster - now_ms());
        wait_ms = to_monster <= 0 ? 0 : ((uint32_t)to_monster < wait_ms ? (uint32_t)to_monster : wait_ms);
#endif
        if (wait_ms > MAX_SLEEP_MS) {
            wait_ms = MAX_SLEEP_MS;
        }
    }
}

void app_main(void)
{
    const int64_t t_boot = esp_timer_get_time();
    // Wall-clock time is kept in UTC; the companion sets it and the local offset (time.set). The board has no RTC:
    // until the companion has sent the time, Office Mode says "CLOCK NOT SET" instead of showing a made-up time.
    s_layout = layout_compute(W, H, CONFIG_DESKBUDDY_STAGE_PERCENT, PANEL_LEFT, X_ALIGN);

    gfx_color_t *strip = heap_caps_malloc(STRIP_PX * sizeof(gfx_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!strip || !gfx_scene_init(&s_scene, W, H, layout_background, &s_layout, strip, STRIP_PX, X_ALIGN)) {
        ESP_LOGE(TAG, "no internal RAM for the composition strip");
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
    ESP_ERROR_CHECK(serial_link_start(events));
    if (xTaskCreatePinnedToCore(render_task, "render", 6144, events, RENDER_PRIO, NULL, RENDER_CORE) != pdPASS) {
        ESP_LOGE(TAG, "cannot start the render task");
        abort();
    }
    ESP_LOGI(TAG, "started in %.0f ms; free internal %u B, PSRAM %u B", (esp_timer_get_time() - t_boot) / 1000.0,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
