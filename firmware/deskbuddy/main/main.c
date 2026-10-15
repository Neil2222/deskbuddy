// DeskBuddy: placeholder character on the rendering engine, the touchscreen menu in the control panel, the Office
// Mode desk clock, and the serial link to the Windows companion.
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
#include "buddy_activity.h"
#include "buddy_brain.h"
#include "buddy_view.h"
#include "display.h"
#include "esp_cache.h"
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
#include "marquee.h"
#include "menu.h"
#include "menu_ask.h"
#include "menu_view.h"
#include "office_clock.h"
#include "office_switch.h"
#include "office_zoom.h"
#include "pdc.h"
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

// Surface flush for the PSRAM frame buffers: writes the rows the renderer just drew from the data cache to PSRAM
// on the render core. Left in the cache, they would be written out whenever something evicts those lines, mostly
// the scanout's bounce fills (which read through the same cache), pushing fills past their time budget.
// Rows start and end on cache lines (32-px aligned dirty rectangles in 64-byte aligned buffers); the outward
// rounding only guards that, and writing back a neighbouring line is harmless.
static void flush_rows(const gfx_color_t *px, int count)
{
    const uintptr_t line = CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE;
    const uintptr_t start = (uintptr_t)px & ~(line - 1);
    const uintptr_t end = ((uintptr_t)(px + count) + line - 1) & ~(line - 1);
    ESP_ERROR_CHECK(esp_cache_msync((void *)start, end - start,
                                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA));
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

// Buddy's zoom into Office Mode and back (office_zoom.h); all Office Mode requests go here.
static office_zoom_t s_zoom;

// Buddy's questions on the stage ("PEUKIE ROKEN?", the ZDMW confirmation), shown while the brain asks one.
static menu_ask_t s_ask;
static const char k_ask_peukie[] = MENU_QUESTION_PEUKIE;
static const char k_ask_zdmw[] = MENU_QUESTION_ZDMW;

// No scene change under way or asked for: neither Buddy's zoom nor the scene switch.
static bool scene_settled(const office_switch_t *screen)
{
    return !office_zoom_busy(&s_zoom) && !office_switch_busy(screen) && screen->want_office == screen->office &&
           s_zoom.want_office == screen->office;
}

// What a zoom cost, like a performance window: everything since the window was restarted for it.
static void zoom_log(const char *way, const perf_t *p, int64_t window_us)
{
    display_stats_t d;
    display_stats_take(&d);
    const uint32_t n = p->renders ? p->renders : 1;
    ESP_LOGI(TAG,
             "ZOOM %s: %" PRIu32 " frames in %.0f ms (%.1f/s) | render avg %.1f max %.1f ms | redraw avg %" PRId64
             " max %d px | scanout resync %" PRIu32 ", fill max %" PRIu32 " us",
             way, p->renders, window_us / 1000.0, p->renders / (window_us / 1e6), p->render_us_sum / 1000.0 / n,
             p->render_us_max / 1000.0, p->px_sum / n, p->px_max, d.resyncs, d.fill_us_max);
}

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

// The PDC Manager integration as the companion offers it (pdc.h); the companion does the HTTP.
static pdc_t s_pdc;

// What the menu shows depends on Buddy; the brain owns his state (reading the mood also ends a timed one).
static menu_status_t menu_status(buddy_brain_t *brain, bool has_office, uint32_t now)
{
    const bool busy = buddy_brain_busy(brain, now);
    return (menu_status_t){buddy_brain_mood(brain, now), brain->state == BUDDY_STATE_MONSTER, busy,
                           busy ? buddy_activity_doing(brain->state) : NULL, has_office, pdc_available(&s_pdc, now),
                           pdc_ready(&s_pdc, now), s_pdc.count};
}

// Carries out what a tap in the menu asks for, the way the companion's commands do (so `evt mood` and `evt mode`
// tell the companion, whichever side changed it).
static void run_menu_action(const menu_result_t *r, buddy_brain_t *brain, uint32_t now)
{
    switch (r->action) {
    case MENU_DO_OPEN:
        ESP_LOGI(TAG, "menu: %s", r->item->id);
        break;
    case MENU_DO_MOOD:
        buddy_brain_set_mood(brain, (buddy_mood_t)r->arg, 0, now);
        ESP_LOGI(TAG, "mood %s (touch)", buddy_mood_name((buddy_mood_t)r->arg));
        break;
    case MENU_DO_EVENT:
        if (buddy_brain_event(brain, (buddy_event_t)r->arg, now)) {
            ESP_LOGI(TAG, "%s (touch)", r->item->id);
        }
        break;
    case MENU_DO_OFFICE:
        office_zoom_request(&s_zoom, true); // he zooms into the clock
        ESP_LOGI(TAG, "Clock Mode on");
        break;
    case MENU_DO_PDC: // first the confirmation: every click is a real Teams message
        if (pdc_ready(&s_pdc, now) && buddy_brain_event(brain, BUDDY_EVENT_ASK_ZDMW, now)) {
            ESP_LOGI(TAG, "ZDMW: asking for confirmation");
        }
        break;
    default:
        break;
    }
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

// Messages from the companion (msg.show), shown on the LED marquee. One message can wait while another one is on
// the sign or a Monster sequence runs. A repeated id is the same message: acknowledged, not shown again.
typedef struct {
    marquee_t sign;
    bool ready;          // the sign is in the scene
    uint32_t showing_id; // 0: nothing on the sign
    uint32_t last_id;    // last accepted message
    bool queued;
    bool caption;        // the sign shows a line of Buddy's own (buddy_pose_t.say), not a message: a message may
                         // replace it, and it sends no events
    uint8_t said;        // the line his pose asked for last
    uint32_t queued_id;
    char queued_text[LINK_TEXT_MAX_BYTES + 1];
} messages_t;
static messages_t s_msg;

// What the companion shows as the device mode.
static const char *mode_name(const office_switch_t *screen, const buddy_brain_t *brain)
{
    if (screen->office) {
        return "office";
    }
    return brain->state == BUDDY_STATE_MONSTER ? "monster" : "normal";
}

// A message can go up now: the screen is lit, the sign is free, no zoom is under way, and no Monster sequence would
// be interrupted (in Office Mode the character is paused, so the sign appears under the clock right away).
static bool msg_can_start(const office_switch_t *screen, const buddy_brain_t *brain, bool lit)
{
    return lit && (!marquee_active(&s_msg.sign) || s_msg.caption) && !office_zoom_busy(&s_zoom) &&
           (screen->office || brain->state != BUDDY_STATE_MONSTER);
}

static void msg_start(uint32_t id, const char *text, const office_switch_t *screen, buddy_brain_t *brain,
                      uint32_t now)
{
    marquee_start(&s_msg.sign, text, now);
    s_msg.showing_id = id;
    s_msg.caption = id == 0; // a line of the device's own (show_line): no events, no jump
    if (s_msg.caption) {
        return;
    }
    if (!screen->office) {
        buddy_brain_event(brain, BUDDY_EVENT_POKE, now); // he jumps when a message comes in
    }
    ESP_LOGI(TAG, "message %" PRIu32 " on the marquee for %" PRIu32 " ms", id, marquee_duration_ms(&s_msg.sign));
    char body[48];
    snprintf(body, sizeof(body), "evt msg id=%" PRIu32 " state=showing", id);
    serial_link_send(0, body);
}

// A line of the device's own on the sign (a ZDMW outcome): now if the sign is free, or as the waiting message.
// Companion message ids start at 1, so id 0 marks it.
static void show_line(const char *text, const office_switch_t *screen, buddy_brain_t *brain, bool lit, uint32_t now)
{
    if (!s_msg.ready) {
        return;
    }
    if (msg_can_start(screen, brain, lit)) {
        msg_start(0, text, screen, brain, now);
    } else if (!s_msg.queued) {
        s_msg.queued = true;
        s_msg.queued_id = 0;
        snprintf(s_msg.queued_text, sizeof(s_msg.queued_text), "%s", text);
    }
}

// How a ZDMW click ended (the companion's pdc.result, or no answer): Buddy's reaction if it went out, and a line.
static void pdc_outcome(link_pdc_result_t result, int32_t count, int teams, const office_switch_t *screen,
                        buddy_brain_t *brain, bool lit, uint32_t now)
{
    char text[PDC_TEXT_MAX];
    pdc_text(text, sizeof(text), result, count, teams);
    ESP_LOGI(TAG, "ZDMW: %s", text);
    // Exasperated, briefly; not during the zoom, in Clock Mode or another activity (busy: the line still shows).
    if (result == LINK_PDC_OK && !screen->office && scene_settled(screen)) {
        buddy_brain_event(brain, BUDDY_EVENT_ZDMW, now);
    }
    show_line(text, screen, brain, lit, now);
}

// JA on "ZE DOEN MAAR WAT?": one click, one id, one evt pdc.click; the companion sends it to PDC Manager.
static void zdmw_send(const office_switch_t *screen, buddy_brain_t *brain, uint32_t now)
{
    const uint16_t id = pdc_send(&s_pdc, now);
    if (!id) { // the companion stopped offering it, or a click is still under way / cooling down
        pdc_outcome(pdc_available(&s_pdc, now) ? LINK_PDC_BUSY : LINK_PDC_UNAVAILABLE, -1, -1, screen, brain, true,
                    now);
        return;
    }
    char body[32];
    snprintf(body, sizeof(body), "evt pdc.click id=%u", (unsigned)id);
    serial_link_send(0, body);
    ESP_LOGI(TAG, "ZDMW: click %u confirmed (JA), handed to the companion", (unsigned)id);
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
        snprintf(body, sizeof(body), "ok mode=%s mood=%s time=%s msg=%s queued=%d uptime=%" PRIu32,
                 mode_name(screen, brain), buddy_mood_name(buddy_brain_mood(brain, now)),
                 wall_clock(&tm, &ms) ? "set" : "unset",
                 marquee_active(&s_msg.sign) && !s_msg.caption ? "showing" : "idle",
                 s_msg.queued, now / 1000);
        break;
    }
    case LINK_CMD_POKE:
    case LINK_CMD_MONSTER:
    case LINK_CMD_ACT: {
        buddy_event_t be = c->cmd == LINK_CMD_POKE ? BUDDY_EVENT_POKE : BUDDY_EVENT_DRINK_MONSTER;
        if (c->cmd == LINK_CMD_ACT && !buddy_act_parse(c->u.act.name, &be)) {
            strcpy(body, "err unsupported"); // an activity this firmware does not have
            break;
        }
        if (!lit || screen->office || !scene_settled(screen) || !buddy_brain_event(brain, be, now)) {
            snprintf(body, sizeof(body), "err busy mode=%s", mode_name(screen, brain));
        } else {
            ESP_LOGI(TAG, "%s (companion)",
                     c->cmd == LINK_CMD_POKE ? "Poke" : c->cmd == LINK_CMD_MONSTER ? "Monster time" : c->u.act.name);
        }
        break;
    }
    case LINK_CMD_MODE_SET:
        if (!has_office) {
            strcpy(body, "err unavailable");
        } else if (!lit) {
            snprintf(body, sizeof(body), "err busy mode=%s", mode_name(screen, brain));
        } else {
            office_zoom_request(&s_zoom, c->u.office); // the same way in and out as from the menu and a tap
            ESP_LOGI(TAG, "Clock Mode %s (companion)", c->u.office ? "on" : "off");
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
    case LINK_CMD_MSG_SHOW: {
        const uint32_t id = c->u.msg.id;
        if (!s_msg.ready) {
            strcpy(body, "err unavailable");
        } else if (id == s_msg.last_id || (id == s_msg.showing_id && marquee_active(&s_msg.sign))) {
            strcpy(body, "ok dup=1");
        } else if (msg_can_start(screen, brain, lit)) {
            s_msg.last_id = id;
            serial_link_send(c->seq, "ok"); // the reply first, then the "showing" event
            msg_start(id, c->u.msg.text, screen, brain, now);
            return;
        } else if (s_msg.queued) {
            snprintf(body, sizeof(body), "err busy mode=%s", mode_name(screen, brain));
        } else {
            s_msg.queued = true;
            s_msg.queued_id = s_msg.last_id = id;
            strcpy(s_msg.queued_text, c->u.msg.text);
            strcpy(body, "ok queued=1");
            ESP_LOGI(TAG, "message %" PRIu32 " waits for the sign", id);
        }
        break;
    }
    case LINK_CMD_MOOD_SET: {
        // Always accepted: in Office Mode, or while a reaction plays, the new mood shows as soon as he is back.
        buddy_mood_t mood;
        if (!buddy_mood_parse(c->u.mood.name, &mood)) {
            strcpy(body, "err unsupported");
        } else {
            buddy_brain_set_mood(brain, mood, c->u.mood.secs * 1000u, now);
            ESP_LOGI(TAG, "mood %s for %" PRIu32 " s (0: until changed) (companion)", buddy_mood_name(mood),
                     c->u.mood.secs);
        }
        break;
    }
    case LINK_CMD_HELLO: // answered by the serial task; a new connection forgets what the previous one offered
        pdc_reset(&s_pdc);
        return;
    case LINK_CMD_PDC_SET:
        if (c->u.pdc_set.on != pdc_available(&s_pdc, now)) {
            ESP_LOGI(TAG, "PDC Manager %s (companion)", c->u.pdc_set.on ? "available" : "unavailable");
        }
        pdc_set(&s_pdc, c->u.pdc_set.on, c->u.pdc_set.count, now);
        break;
    case LINK_CMD_PDC_RESULT:
        // Only the click we are waiting for counts: a late or repeated result changes nothing (at most one outcome).
        if (pdc_result(&s_pdc, c->u.pdc_result.id, c->u.pdc_result.result, c->u.pdc_result.count, now)) {
            serial_link_send(c->seq, body); // the reply first
            pdc_outcome(c->u.pdc_result.result, c->u.pdc_result.count, c->u.pdc_result.teams, screen, brain, lit,
                        now);
            return;
        }
        ESP_LOGI(TAG, "ZDMW: result for click %u ignored (not waiting for it)", (unsigned)c->u.pdc_result.id);
        break;
    default: // ping is answered by the serial task
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
    static menu_t menu;
    static menu_view_t menu_view;
    static office_clock_t clock;
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
    // The menu's pixels are read by the compositor whenever a button redraws: internal RAM if there is room.
    uint8_t *menu_mem = heap_caps_malloc(MENU_VIEW_MEMORY, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!menu_mem) {
        menu_mem = heap_caps_malloc(MENU_VIEW_MEMORY, MALLOC_CAP_SPIRAM);
    }
    const bool has_menu = menu_init(&menu, &menu_default, layout_panel_content(&s_layout), t_start) &&
                          menu_view_init(&menu_view, &s_scene, &menu, menu_mem, MENU_VIEW_MEMORY, 10);
    if (!has_menu) {
        ESP_LOGW(TAG, "no touchscreen menu (the panel is too small, or no memory)");
    }
    const int head_top = view.home_y0 - BUDDY_MAX_LIFT * view.home_scale;
    if (!menu_ask_init(&s_ask, &s_scene, s_layout.stage, head_top, 25)) {
        ESP_LOGW(TAG, "Buddy cannot ask questions (they do not fit the stage)");
    }
    // The glyph cache is only read when the clock redraws (rarely): PSRAM.
    uint8_t *clock_mem = heap_caps_malloc(OFFICE_CLOCK_MEMORY, MALLOC_CAP_SPIRAM);
    const bool has_office = office_clock_init(&clock, &s_scene, s_layout.screen, clock_mem, OFFICE_CLOCK_MEMORY, 20);
    if (!has_office) {
        ESP_LOGW(TAG, "Clock Mode unavailable (no memory for the clock)");
    }
    office_switch_init(&screen);
    office_zoom_init(&s_zoom);
    s_msg.ready = marquee_init(&s_msg.sign, &s_scene, 30); // above everything
    if (!s_msg.ready) {
        ESP_LOGW(TAG, "no room in the scene for the message marquee");
    }

    buddy_brain_init(&brain, t_start, esp_random());
    pdc_init(&s_pdc, (uint16_t)esp_random()); // click ids start somewhere new on every boot
    gfx_scene_invalidate(&s_scene, s_layout.screen); // both buffers start black: first two frames are full
    ESP_LOGI(TAG, "stage %dx%d at x=%d, panel %dx%d at x=%d; character scale %d at (%d,%d)", s_layout.stage.w,
             s_layout.stage.h, s_layout.stage.x, s_layout.panel.w, s_layout.panel.h, s_layout.panel.x, view.scale,
             view.x0, view.y0);

    perf_t perf = {0};
    int64_t perf_since = esp_timer_get_time();
    int64_t pending_touch_us = 0; // interrupt time of a touch whose reaction is not on screen yet
    uint32_t presented = 0;
    bool pdc_was = false; // the companion offered PDC Manager last time round (to log when that ends)
    bool backlight = false; // board_init leaves the backlight off
    uint32_t wait_ms = 0;
    const char *mode = NULL; // last mode reported to the companion
    buddy_mood_t mood = buddy_brain_mood(&brain, t_start); // last mood reported
#if CONFIG_DESKBUDDY_DEMO_POKE_S > 0
    uint32_t next_demo = t_start + CONFIG_DESKBUDDY_DEMO_POKE_S * 1000u;
#endif
#if CONFIG_DESKBUDDY_DEMO_MONSTER_S > 0
    uint32_t next_monster = t_start + CONFIG_DESKBUDDY_DEMO_MONSTER_S * 1000u;
#endif
#if CONFIG_DESKBUDDY_DEMO_ACTIVITY_S > 0
    uint32_t next_activity = t_start + CONFIG_DESKBUDDY_DEMO_ACTIVITY_S * 1000u;
    int demo_activity = 0;
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
                // Touches count once the screen is lit and no scene change (Buddy's zoom, the switch) is under way
                // or asked for: taps during a transition are dropped, so repeated taps cannot pile up.
                if (presented < 2 || !scene_settled(&screen)) {
                    continue;
                }
                // In Office Mode any tap (on release) brings DeskBuddy back. Otherwise the menu takes the touches in
                // the control panel (it acts on release, see menu.h), and a touch-down on the robot pokes him.
                const uint32_t t = now_ms();
                if (screen.office) {
                    if (ev.type == INPUT_UP) {
                        office_zoom_request(&s_zoom, false); // he wakes up
                        ESP_LOGI(TAG, "Clock Mode off");
                    }
                    continue;
                }
                // His question takes the touches that go down on its buttons (and answers on release).
                const menu_touch_t type = ev.type == INPUT_DOWN ? MENU_TOUCH_DOWN
                                          : ev.type == INPUT_MOVE ? MENU_TOUCH_MOVE
                                                                  : MENU_TOUCH_UP;
                const menu_answer_t answer = menu_ask_touch(&s_ask, type, ev.x, ev.y);
                const bool confirming = brain.state == BUDDY_STATE_CONFIRM; // which question it answers
                if (answer != MENU_ANSWER_NONE && buddy_brain_answer(&brain, answer == MENU_ANSWER_YES, t)) {
                    if (!confirming) {
                        ESP_LOGI(TAG, "peukie? %s", answer == MENU_ANSWER_YES ? "ja" : "nee");
                    } else if (answer == MENU_ANSWER_YES) {
                        zdmw_send(&screen, &brain, t);
                    } else {
                        ESP_LOGI(TAG, "ZDMW: cancelled (NEE), nothing sent");
                    }
                }
                if (menu_ask_holding(&s_ask)) {
                    if (ev.type == INPUT_DOWN && !pending_touch_us) {
                        pending_touch_us = ev.irq_us; // the button sinks in
                    }
                    continue;
                }
                if (answer != MENU_ANSWER_NONE) {
                    continue;
                }
                if (has_menu) {
                    const menu_status_t st = menu_status(&brain, has_office, t);
                    const menu_result_t r = menu_touch(&menu, type, ev.x, ev.y, &st, t);
                    run_menu_action(&r, &brain, t);
                    if (menu.down) {
                        if (ev.type == INPUT_DOWN && !pending_touch_us) {
                            pending_touch_us = ev.irq_us; // the button sinks in: the reaction to measure
                        }
                        continue;
                    }
                }
                if (ev.type == INPUT_DOWN && buddy_view_hit(&view, ev.x, ev.y) &&
                    buddy_brain_event(&brain, BUDDY_EVENT_POKE, t) && !pending_touch_us) {
                    pending_touch_us = ev.irq_us;
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
#if CONFIG_DESKBUDDY_DEMO_ACTIVITY_S > 0
        if ((int32_t)(now - next_activity) >= 0) {
            static const buddy_event_t k_demo[] = {BUDDY_EVENT_DRINK_WATER, BUDDY_EVENT_SNEEZE, BUDDY_EVENT_STARE,
                                                   BUDDY_EVENT_MEDITATE, BUDDY_EVENT_ASK_PEUKIE}; // unanswered: sad
            const buddy_event_t ev = k_demo[demo_activity];
            if (!screen.office && scene_settled(&screen) && buddy_brain_event(&brain, ev, now)) {
                ESP_LOGI(TAG, "activity %d (demo)", (int)ev);
                demo_activity = (demo_activity + 1) % (int)(sizeof(k_demo) / sizeof(k_demo[0]));
            }
            next_activity = now + CONFIG_DESKBUDDY_DEMO_ACTIVITY_S * 1000u;
        }
#endif
#if CONFIG_DESKBUDDY_DEMO_OFFICE_S > 0
        if ((int32_t)(now - next_office) >= 0 && has_office && presented >= 2) {
            office_zoom_request(&s_zoom, !s_zoom.want_office);
            ESP_LOGI(TAG, "Clock Mode %s (demo)", s_zoom.want_office ? "on" : "off");
            next_office = now + CONFIG_DESKBUDDY_DEMO_OFFICE_S * 1000u;
        }
#endif

        // Buddy's zoom into Office Mode and back: it asks the scene switch for the clock or for Buddy.
        const office_zoom_step_t zoom = office_zoom_step(&s_zoom, &screen, now);
        if (zoom.menu_hide) {
            menu_cancel(&menu);
            menu_view_close(&menu_view, now);
            perf_log(&perf, esp_timer_get_time() - perf_since); // the next window covers exactly the zoom in
            memset(&perf, 0, sizeof(perf));
            perf_since = esp_timer_get_time();
        }
        if (zoom.menu_show && has_menu) {
            menu_view_show(&menu_view, true);
            menu_reveal(&menu, now); // the page he left, flipping back in
        }
        if (zoom.back) {
            buddy_view_camera_home(&view);
            buddy_brain_rest(&brain, now); // his mood carries on (a mood chosen meanwhile shows now)
            zoom_log("out", &perf, esp_timer_get_time() - perf_since);
            memset(&perf, 0, sizeof(perf));
            perf_since = esp_timer_get_time();
        }

        // Scene switch: the scene changes and both buffers are redrawn.
        const office_step_t step = office_switch_step(&screen);
        const bool light_ok = presented < 2 || backlight_on(&backlight);
        if (step.swap) {
            if (screen.office) {
                buddy_view_hide(&view); // the character pauses; a running Monster sequence is dropped on return
                menu_cancel(&menu);
                menu_view_show(&menu_view, false);
                office_clock_show(&clock, true);
                s_scene.background = office_background;
                s_scene.background_ctx = NULL;
            } else { // his dark face first (the zoom draws it); the menu comes back as he steps back
                office_clock_show(&clock, false);
                s_scene.background = layout_background;
                s_scene.background_ctx = &s_layout;
                buddy_brain_rest(&brain, now); // a paused Monster sequence is dropped (the mode is normal again)
            }
        }
        if (step.redraw_all) {
            gfx_scene_invalidate(&s_scene, s_layout.screen);
        } else if (step.redraw_kick) {
            gfx_scene_invalidate(&s_scene, (gfx_rect_t){0, 0, 1, 1}); // the repair redraws the other buffer fully
        }
        if (step.redrawn) {
            // Like at boot: report the full redraws separately, so the performance windows cover normal frames.
            // Into the clock, the window started with the zoom: the line covers the whole way in.
            if (screen.office) {
                zoom_log("in", &perf, esp_timer_get_time() - perf_since);
            } else {
                display_stats_t sw;
                display_stats_take(&sw);
                ESP_LOGI(TAG, "DeskBuddy drawn (%" PRIu32 " scanout resyncs, fill max %" PRIu32 " us)", sw.resyncs,
                         sw.fill_us_max);
            }
            memset(&perf, 0, sizeof(perf));
            perf_since = esp_timer_get_time();
        }

        // PDC Manager: when the companion stops offering it, nothing about it stays on screen (a confirmation, its
        // page); a click that got no answer at all is given up (maybe it was sent).
        const bool pdc_now = pdc_available(&s_pdc, now);
        if (!pdc_now) {
            if (pdc_was) {
                ESP_LOGI(TAG, "PDC Manager no longer offered (the lease ran out, or a new companion connection)");
            }
            if (brain.state == BUDDY_STATE_CONFIRM) {
                buddy_brain_rest(&brain, now);
                ESP_LOGI(TAG, "ZDMW: confirmation withdrawn (the companion no longer offers PDC Manager)");
            }
            if (menu.category == MENU_PDC) {
                menu_open(&menu, MENU_HOME, now);
            }
        }
        pdc_was = pdc_now;
        if (pdc_expired(&s_pdc, now)) {
            pdc_outcome(LINK_PDC_TIMEOUT, -1, -1, &screen, &brain, presented >= 2, now);
        }

        uint32_t next;
        if (screen.office) {
            struct tm tm;
            uint32_t ms = 0;
            const bool known = wall_clock(&tm, &ms);
            next = office_clock_apply(&clock, known ? &tm : NULL, ms);
        } else {
            buddy_pose_t pose;
            next = buddy_brain_tick(&brain, now, &pose); // his brain keeps time, also while the zoom draws him
            if (zoom.animate) {
                const office_camera_t home = {view.home_scale, view.home_x0, view.home_y0};
                office_camera_t cam;
                next = office_zoom_frame(&s_zoom, now, home, s_layout.screen, &cam, &pose);
                buddy_view_camera(&view, cam.scale, cam.x0, cam.y0);
            }
            const uint32_t view_next = buddy_view_apply(&view, &pose, now);
            next = view_next < next ? view_next : next;
            if (pose.say != s_msg.said) { // a line of his own on the sign, if it is free
                s_msg.said = pose.say;
                if (pose.say && s_msg.ready && !marquee_active(&s_msg.sign) && !s_msg.queued) {
                    marquee_start(&s_msg.sign, buddy_say_text((buddy_say_t)pose.say), now);
                    s_msg.caption = true;
                }
            }
            if (has_menu) {
                const menu_status_t st = menu_status(&brain, has_office, now);
                const uint32_t menu_next = menu_view_apply(&menu_view, &menu, &st, now);
                next = menu_next < next ? menu_next : next;
            }
        }
        // The question shows while he asks it: not during the zoom or in Clock Mode.
        const uint32_t ask_left = !screen.office && !zoom.animate ? buddy_brain_asking(&brain, now) : 0;
        const char *question = brain.state == BUDDY_STATE_CONFIRM ? k_ask_zdmw : k_ask_peukie;
        const uint32_t ask_next =
            menu_ask_apply(&s_ask, ask_left ? question : NULL, ask_left, buddy_activity_ms(brain.state));
        next = ask_next < next ? ask_next : next;
        const uint32_t pdc_wake = pdc_next(&s_pdc, now); // the lease, the cooldown, a click without answer
        next = pdc_wake < next ? pdc_wake : next;
        next = step.next_ms < next ? step.next_ms : next;
        next = step.redrawn ? 0 : next; // the zoom carries on (and a waiting message goes up) right away
        next = !light_ok && next > BACKLIGHT_RETRY_MS ? BACKLIGHT_RETRY_MS : next;

        // The message sign: above the robot, or below the clock in Office Mode (which stays on).
        if (s_msg.ready) {
            const gfx_rect_t area = screen.office ? s_layout.screen : s_layout.stage;
            const int mx = area.x + (area.w - MARQUEE_W) / 2;
            const int my = screen.office ? area.y + area.h - MARQUEE_H - 36 : area.y + 22;
            const bool was_showing = marquee_active(&s_msg.sign);
            uint32_t sign_next = marquee_apply(&s_msg.sign, mx, my, now);
            if (was_showing && !marquee_active(&s_msg.sign)) {
                if (s_msg.showing_id) {
                    char body[48];
                    snprintf(body, sizeof(body), "evt msg id=%" PRIu32 " state=done", s_msg.showing_id);
                    serial_link_send(0, body);
                }
                s_msg.showing_id = 0;
                s_msg.caption = false;
            }
            if (s_msg.queued && msg_can_start(&screen, &brain, presented >= 2)) {
                s_msg.queued = false;
                msg_start(s_msg.queued_id, s_msg.queued_text, &screen, &brain, now);
                sign_next = marquee_apply(&s_msg.sign, mx, my, now);
            }
            next = sign_next < next ? sign_next : next;
        }

        const char *new_mode = mode_name(&screen, &brain);
        if (new_mode != mode) {
            if (mode) {
                char body[32];
                snprintf(body, sizeof(body), "evt mode mode=%s", new_mode);
                serial_link_send(0, body);
            }
            mode = new_mode;
        }
        const buddy_mood_t new_mood = buddy_brain_mood(&brain, now); // also ends a timed mood, in Office Mode too
        if (new_mood != mood) {
            char body[32];
            snprintf(body, sizeof(body), "evt mood mood=%s", buddy_mood_name(new_mood));
            serial_link_send(0, body);
            mood = new_mood;
        }

        gfx_dirty_clear(&changes);
        gfx_scene_collect(&s_scene, &changes);
        if (changes.count > 0) {
            gfx_repair_next(&repair, &changes, &redraw);
            const gfx_surface_t back = {display_back_buffer(), W, H, W, flush_rows};
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
#if CONFIG_DESKBUDDY_DEMO_ACTIVITY_S > 0
        const int32_t to_activity = (int32_t)(next_activity - now_ms());
        wait_ms = to_activity <= 0 ? 0 : ((uint32_t)to_activity < wait_ms ? (uint32_t)to_activity : wait_ms);
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
