// DeskBuddy: placeholder character on the rendering engine, the touchscreen menu in the control panel, the Office
// Mode desk clock, and the companion's commands. The same code on the board and in the simulator (app.h).
#include "app.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_events.h"
#include "app_link.h"
#include "app_log.h"
#include "app_port.h"
#include "buddy_activity.h"
#include "buddy_brain.h"
#include "buddy_view.h"
#include "gfx_repair.h"
#include "gfx_scene.h"
#include "layout.h"
#include "marquee.h"
#include "md.h"
#include "md_buddy.h"
#include "md_view.h"
#include "menu.h"
#include "menu_ask.h"
#include "menu_thought.h"
#include "menu_ttt.h"
#include "menu_view.h"
#include "office_faces.h"
#include "office_switch.h"
#include "office_zoom.h"
#include "pdc.h"
#include "ttt.h"

static const char *TAG = "deskbuddy";

#define X_ALIGN 32           // dirty rectangles start/end on 64-byte PSRAM cache lines
#define STRIP_PX (16 * 1024) // 32 KB composition buffer in internal RAM
#define MAX_SLEEP_MS 1000    // wake at least this often (performance log)
#define BACKLIGHT_RETRY_MS 20

typedef struct {
    uint32_t renders, full_renders;
    int64_t render_us_sum, render_us_max;
    int64_t px_sum;
    int px_max;
    uint32_t touches;
    int64_t touch_us_sum, touch_us_max; // touch -> reaction frame starts showing
    uint32_t verified, mismatched;
} perf_t;

static app_config_t s_cfg;
static int W, H;
static layout_t s_layout;
static gfx_scene_t s_scene;

static uint32_t now_ms(void)
{
    return (uint32_t)(app_port_time_us() / 1000);
}

// Sends a recorded event (evt act, evt did): numbered and kept for log.since (app_events.h), with the UTC time it
// happened if the clock has been set, else only the time since start.
static void record(const char *body)
{
    int64_t sec;
    uint32_t usec;
    app_port_wall_get(&sec, &usec);
    app_events_send(body, office_time_valid((time_t)sec), sec * 1000 + usec / 1000, now_ms());
}

static int64_t max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static void *alloc_internal_first(size_t size)
{
    void *p = app_port_alloc(size, APP_MEM_INTERNAL);
    return p ? p : app_port_alloc(size, APP_MEM_LARGE);
}

// Pixels written into PSRAM outside the frame buffers (the clock faces' images): out of the cache at once, as the
// frame buffers' rows are, so the scanout never has to evict them.
static void flush_bytes(const void *px, size_t bytes)
{
    app_port_flush((const gfx_color_t *)px, (int)((bytes + sizeof(gfx_color_t) - 1) / sizeof(gfx_color_t)));
}

static gfx_color_t *s_verify_px; // gfx_verify only

// Draws the whole scene into a scratch buffer and compares it with the incrementally repaired frame.
static void verify_frame(const gfx_surface_t *frame, perf_t *perf)
{
    gfx_dirty_t all;
    gfx_dirty_init(&all, W, H, X_ALIGN);
    gfx_dirty_add(&all, s_layout.screen);
    const gfx_surface_t ref = {s_verify_px, W, H, W, NULL};
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
        APP_LOGE(TAG, "VERIFY: %" PRIu32 " stale pixels, first at (%d,%d)", bad, fx, fy);
    }
}

// Buddy's zoom into Office Mode and back (office_zoom.h); all Office Mode requests go here.
static office_zoom_t s_zoom;

// Clock Mode's faces (Classic, Mechanical Skeleton) and which one is selected: by touch (CLOCK FACES) or command
// (clock.face). In RAM only: Classic after a restart.
static office_faces_t s_faces;
static void select_face(office_face_t face, const char *src);

// Buddy's questions on the stage ("PEUKIE ROKEN?", the ZDMW confirmation), shown while the brain asks one.
static menu_ask_t s_ask;
static const char k_ask_peukie[] = MENU_QUESTION_PEUKIE;
static const char k_ask_zdmw[] = MENU_QUESTION_ZDMW;

// What goes on in his head (FOCUS), drawn behind him on the stage.
static menu_thought_t s_thought;

// Missile Defense (md.h) on the whole screen. Its way in: Buddy's alert on his stage (MDS_ALERT), then the scene
// switch into the game scene (the clock's two-step full redraw of both buffers, office_switch.h, as its own instance:
// .office means "the game scene"), the game (MDS_PLAY), and the same switch back out. Clock Mode during the game ends it
// first (s_mg.clock_after): the zoom starts once Buddy is back on his stage.
typedef enum { MDS_OFF, MDS_ALERT, MDS_IN, MDS_PLAY, MDS_OUT } md_stage_t;
#define MD_ALERT_MS 900 // "INCOMING!" on his stage before the game scene comes up
static md_t s_md;
static md_view_t s_md_view;
static struct {
    md_stage_t stage;
    uint32_t alert_at;
    office_switch_t sw;
    bool has_view;    // the view could be set up (else the tile does nothing)
    bool clock_after; // Clock Mode was asked for during the game
} s_mg;

static bool md_on(void)
{
    return s_mg.stage != MDS_OFF;
}

// No scene change under way or asked for: neither Buddy's zoom nor a scene switch (the clock's or the game's), nor his
// alert before the game.
static bool scene_settled(const office_switch_t *screen)
{
    return !office_zoom_busy(&s_zoom) && !office_switch_busy(screen) && screen->want_office == screen->office &&
           s_zoom.want_office == screen->office && s_mg.stage != MDS_ALERT && !office_switch_busy(&s_mg.sw) &&
           s_mg.sw.want_office == s_mg.sw.office;
}

// The strip-copy timing of a window (app_scanout_stats_t), on its own line after the window's line; nothing where
// there is no scanout (the simulator).
static void strips_log(const char *window, const app_scanout_stats_t *d)
{
    if (d->fills_checked == 0) {
        return;
    }
    APP_LOGI(TAG,
             "STRIPS %s: %" PRIu32 " fills | start late max %" PRIu32 " us (while drawing %" PRIu32
             " us) | margin min %" PRId32 " us | close %" PRIu32 " (while drawing %" PRIu32 ") | missed %" PRIu32,
             window, d->fills_checked, d->late_us_max, d->late_us_max_drawing, d->margin_us_min, d->close,
             d->close_drawing, d->missed);
}

// What a zoom cost, like a performance window: everything since the window was restarted for it.
static void zoom_log(const char *way, const perf_t *p, int64_t window_us)
{
    app_scanout_stats_t d;
    app_port_scanout_stats(&d);
    const uint32_t n = p->renders ? p->renders : 1;
    APP_LOGI(TAG,
             "ZOOM %s: %" PRIu32 " frames in %.0f ms (%.1f/s) | render avg %.1f max %.1f ms | redraw avg %" PRId64
             " max %d px | scanout resync %" PRIu32 ", fill max %" PRIu32 " us",
             way, p->renders, window_us / 1000.0, p->renders / (window_us / 1e6), p->render_us_sum / 1000.0 / n,
             p->render_us_max / 1000.0, p->px_sum / n, p->px_max, d.resyncs, d.fill_us_max);
    strips_log(way, &d);
}

static void perf_log(const perf_t *p, int64_t window_us)
{
    if (window_us < 50000) {
        return; // a window that just began (one switch right after another): nothing to say yet
    }
    app_scanout_stats_t d;
    app_port_scanout_stats(&d);
    const double s = window_us / 1e6;
    const uint32_t n = p->renders ? p->renders : 1;
    const uint32_t presents = d.presents ? d.presents : 1;
    const uint32_t touches = p->touches ? p->touches : 1;
    APP_LOGI(TAG,
             "PERF %.1fs: scanout %.2f Hz (resync %" PRIu32 ", fill max %" PRIu32 " us) | %" PRIu32 " frames (%.1f/s, %" PRIu32
             " full) | render avg %.2f max %.2f ms | redraw avg %" PRId64 " max %d px"
             " | swap wait avg %.1f max %.1f ms (slow %" PRIu32 ") | touch->scanout n=%" PRIu32 " avg %.1f max %.1f ms",
             s, d.frames / s, d.resyncs, d.fill_us_max, p->renders, p->renders / s, p->full_renders,
             p->render_us_sum / 1000.0 / n, p->render_us_max / 1000.0, p->px_sum / n, p->px_max,
             d.wait_us_sum / 1000.0 / presents, d.wait_us_max / 1000.0, d.slow_waits, p->touches,
             p->touch_us_sum / 1000.0 / touches, p->touch_us_max / 1000.0);
    strips_log("PERF", &d);
    if (s_cfg.gfx_verify) {
        APP_LOGI(TAG, "VERIFY %.1fs: %" PRIu32 " frames checked, %" PRIu32 " mismatched", s, p->verified,
                 p->mismatched);
    }
}

// The PDC Manager integration as the companion offers it (pdc.h); the companion does the HTTP.
static pdc_t s_pdc;

// What the menu shows depends on Buddy; the brain owns his state (reading the mood also ends a timed one).
static menu_status_t menu_status(buddy_brain_t *brain, bool has_office, uint32_t now)
{
    const bool busy = buddy_brain_busy(brain, now);
    uint8_t faces = 0;
    for (int i = 0; i < OFFICE_FACE_COUNT; i++) {
        faces |= (uint8_t)(s_faces.has[i] ? 1u << i : 0);
    }
    return (menu_status_t){buddy_brain_mood(brain, now), brain->state == BUDDY_STATE_MONSTER, busy,
                           busy ? buddy_activity_doing(brain->state) : NULL, has_office, pdc_available(&s_pdc, now),
                           pdc_ready(&s_pdc, now), s_pdc.count, faces, (uint8_t)s_faces.face,
                           brain->state == BUDDY_STATE_KITT, NULL};
}

// ---- Interactions: the activities the companion hears about (evt act), started by `act` or on the touchscreen ----
//
// One at a time (the brain plays one activity at a time). Its life: started (the brain took it) -> shown (a frame
// with it has been presented: for the question, the question itself is on screen) -> done, with how it ended:
//   yes / no / timeout   the question was answered with JA, with NEE, or not within the minute
//   done                 a show played to its end
//   interrupted          cut off (Clock Mode, a restart of his mood): never counted as an answer
// `status` reports the last one, so a companion that missed an event can still learn how it ended.

#define IA_OWN_BIT 0x80000000u // ids the device picks itself (touch, or `act` without iid) have the top bit set

typedef enum { IA_NONE, IA_STARTED, IA_SHOWN, IA_DONE } ia_state_t;

typedef struct {
    ia_state_t state;
    uint32_t iid;
    const char *name;     // act name (buddy_act_name)
    bool touch;           // started on the touchscreen (else by a command)
    buddy_state_t bstate; // the brain state it plays in
    uint32_t start;       // now_ms when the brain took it
    bool cut;             // the brain was made to rest while it played
    const char *result;
    uint32_t next_own;
} interaction_t;
static interaction_t s_ia;

static const char *ia_state_name(ia_state_t state)
{
    return state == IA_STARTED ? "started" : state == IA_SHOWN ? "shown" : state == IA_DONE ? "done" : "none";
}

// How it ended, decided when the brain has left its state.
static const char *ia_outcome(const buddy_brain_t *b, uint32_t now)
{
    if (s_ia.cut) {
        return "interrupted";
    }
    if (s_ia.bstate == BUDDY_STATE_ASK) {
        switch (b->answer) {
        case BUDDY_ANSWER_YES:
            return "yes";
        case BUDDY_ANSWER_NO:
            return "no";
        case BUDDY_ANSWER_TIMEOUT:
            return "timeout";
        default: // the minute is over but the brain has not moved on yet (a new activity came first)
            return now - s_ia.start >= BUDDY_ASK_MS ? "timeout" : "interrupted";
        }
    }
    return now - s_ia.start >= buddy_activity_ms(s_ia.bstate) ? "done" : "interrupted";
}

static void ia_finish(const buddy_brain_t *b, uint32_t now)
{
    s_ia.result = ia_outcome(b, now);
    s_ia.state = IA_DONE;
    char body[64];
    snprintf(body, sizeof(body), "evt act iid=%" PRIu32 " state=done result=%s", s_ia.iid, s_ia.result);
    record(body);
    APP_LOGI(TAG, "interaction %" PRIu32 " (%s): %s", s_ia.iid, s_ia.name, s_ia.result);
}

// The brain has just taken an activity (b->state is its state). iid 0: the device picks one.
static void ia_begin(uint32_t iid, const char *name, bool touch, const buddy_brain_t *b, uint32_t now)
{
    if (s_ia.state == IA_STARTED || s_ia.state == IA_SHOWN) {
        ia_finish(b, now); // its end and the new start fell between two frames
    }
    if (!iid) {
        iid = IA_OWN_BIT | (s_ia.next_own++ & ~IA_OWN_BIT);
    }
    const uint32_t next_own = s_ia.next_own;
    s_ia = (interaction_t){IA_STARTED, iid, name, touch, b->state, now, false, NULL, next_own};
    APP_LOGI(TAG, "interaction %" PRIu32 ": %s (%s)", iid, name, touch ? "touch" : "companion");
}

// Once per frame, after the brain moved on and after the frame was (or was not) presented.
// visible: what was presented shows it (the question on the stage, or Buddy doing his show).
static void ia_update(const buddy_brain_t *b, bool presented, bool visible, uint32_t now)
{
    if (s_ia.state != IA_STARTED && s_ia.state != IA_SHOWN) {
        return;
    }
    if (b->state != s_ia.bstate) {
        ia_finish(b, now);
        return;
    }
    if (s_ia.state == IA_STARTED && presented && visible) {
        s_ia.state = IA_SHOWN;
        char body[80];
        snprintf(body, sizeof(body), "evt act iid=%" PRIu32 " name=%s state=shown src=%s", s_ia.iid, s_ia.name,
                 s_ia.touch ? "touch" : "cmd");
        record(body);
    }
}

// Drops what the brain is doing: an interaction under way ends as interrupted, never as an answer.
static void brain_rest(buddy_brain_t *b, uint32_t now)
{
    if (s_ia.state == IA_STARTED || s_ia.state == IA_SHOWN) {
        s_ia.cut = true;
    }
    buddy_brain_rest(b, now);
}

// Why the mood changed, for evt mood src=: set where it is changed on purpose, used once by the next report.
static const char *s_mood_src;
static bool s_smoke_break; // a smoke break (JA to the question) is under way: its start and end are src=break

static void missile_start(uint32_t now);
static void missile_leave(const char *why, uint32_t now);

// Carries out what a tap in the menu asks for, the way the companion's commands do (so `evt mood` and `evt mode`
// tell the companion, whichever side changed it).
static void run_menu_action(const menu_result_t *r, buddy_brain_t *brain, uint32_t now)
{
    switch (r->action) {
    case MENU_DO_OPEN:
        APP_LOGI(TAG, "menu: %s", r->item->id);
        break;
    case MENU_DO_MOOD:
        buddy_brain_set_mood(brain, (buddy_mood_t)r->arg, 0, now);
        s_mood_src = "touch";
        s_smoke_break = false;
        APP_LOGI(TAG, "mood %s (touch)", buddy_mood_name((buddy_mood_t)r->arg));
        break;
    case MENU_DO_EVENT:
        if (buddy_brain_event(brain, (buddy_event_t)r->arg, now)) {
            APP_LOGI(TAG, "%s (touch)", r->item->id);
            const char *act = buddy_act_event_name((buddy_event_t)r->arg); // a show or the question: tracked
            if (act) {
                ia_begin(0, act, true, brain, now);
            }
        }
        break;
    case MENU_DO_OFFICE:
        office_zoom_request(&s_zoom, true); // he zooms into the clock
        APP_LOGI(TAG, "Clock Mode on");
        break;
    case MENU_DO_FACE:
        select_face((office_face_t)r->arg, "touch");
        break;
    case MENU_DO_PDC: // first the confirmation: every click is a real Teams message
        if (pdc_ready(&s_pdc, now) && buddy_brain_event(brain, BUDDY_EVENT_ASK_ZDMW, now)) {
            APP_LOGI(TAG, "ZDMW: asking for confirmation");
        }
        break;
    case MENU_DO_MISSILE:
        missile_start(now);
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
    int64_t sec;
    uint32_t usec;
    app_port_wall_get(&sec, &usec);
    if (!s_tz_known || !office_time_valid((time_t)sec)) {
        return false;
    }
    const time_t local = (time_t)(sec + (int64_t)link_tz_offset_min(&s_tz, sec) * 60);
#ifdef _WIN32
    gmtime_s(tm, &local);
#else
    gmtime_r(&local, tm);
#endif
    *ms = usec / 1000;
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
    return brain->state == BUDDY_STATE_MONSTER ? "monster" : brain->state == BUDDY_STATE_GAME ? "game" : "normal";
}

// A message can go up now: the screen is lit, the sign is free, no zoom is under way, no Monster sequence would be
// interrupted (in Office Mode the character is paused, so the sign appears under the clock right away), and no
// Missile Defense game is on (it waits until the game is over).
static bool msg_can_start(const office_switch_t *screen, const buddy_brain_t *brain, bool lit)
{
    return lit && (!marquee_active(&s_msg.sign) || s_msg.caption) && !office_zoom_busy(&s_zoom) &&
           (screen->office || brain->state != BUDDY_STATE_MONSTER) && !md_on();
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
    APP_LOGI(TAG, "message %" PRIu32 " on the marquee for %" PRIu32 " ms", id, marquee_duration_ms(&s_msg.sign));
    char body[48];
    snprintf(body, sizeof(body), "evt msg id=%" PRIu32 " state=showing", id);
    app_link_send(0, body);
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
    APP_LOGI(TAG, "ZDMW: %s", text);
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
    app_link_send(0, body);
    APP_LOGI(TAG, "ZDMW: click %u confirmed (JA), handed to the companion", (unsigned)id);
}

// Runs a companion command (on the application thread, which owns the character and the scene) and replies.
// lit: the screen is up (touches are only taken from then on, too).
static void run_command(const link_command_t *c, buddy_brain_t *brain, office_switch_t *screen, bool has_office,
                        bool lit, uint32_t now)
{
    char body[160] = "ok";
    switch (c->cmd) {
    case LINK_CMD_STATUS: {
        struct tm tm;
        uint32_t ms;
        int n = snprintf(body, sizeof(body), "ok mode=%s mood=%s time=%s msg=%s queued=%d uptime=%" PRIu32,
                         mode_name(screen, brain), buddy_mood_name(buddy_brain_mood(brain, now)),
                         wall_clock(&tm, &ms) ? "set" : "unset",
                         marquee_active(&s_msg.sign) && !s_msg.caption ? "showing" : "idle",
                         s_msg.queued, now / 1000);
        // The last interaction (evt act), so a companion that missed its events can still learn how it ended.
        if (n > 0 && (size_t)n < sizeof(body)) {
            if (s_ia.state == IA_NONE) {
                n += snprintf(body + n, sizeof(body) - (size_t)n, " act=none");
            } else {
                n += snprintf(body + n, sizeof(body) - (size_t)n, " act=%" PRIu32 ":%s%s%s", s_ia.iid,
                              ia_state_name(s_ia.state), s_ia.result ? ":" : "", s_ia.result ? s_ia.result : "");
            }
        }
        if (n > 0 && (size_t)n < sizeof(body)) {
            snprintf(body + n, sizeof(body) - (size_t)n, " face=%s", office_face_name(s_faces.face));
        }
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
        if (c->cmd == LINK_CMD_ACT && c->u.act.iid && c->u.act.iid == s_ia.iid && s_ia.state != IA_NONE) {
            // The same request again (a retry after a lost reply): it never starts a second time.
            snprintf(body, sizeof(body), "ok dup=1 state=%s", ia_state_name(s_ia.state));
            break;
        }
        if (!lit || screen->office || !scene_settled(screen) || !buddy_brain_event(brain, be, now)) {
            snprintf(body, sizeof(body), "err busy mode=%s", mode_name(screen, brain));
        } else {
            APP_LOGI(TAG, "%s (companion)",
                     c->cmd == LINK_CMD_POKE ? "Poke" : c->cmd == LINK_CMD_MONSTER ? "Monster time" : c->u.act.name);
            if (c->cmd == LINK_CMD_ACT) {
                ia_begin(c->u.act.iid, buddy_act_event_name(be), false, brain, now);
            }
        }
        break;
    }
    case LINK_CMD_MODE_SET:
        if (!has_office) {
            strcpy(body, "err unavailable");
        } else if (!lit) {
            snprintf(body, sizeof(body), "err busy mode=%s", mode_name(screen, brain));
        } else if (c->u.office && md_on()) { // the game ends first; the zoom starts once he is back on his stage
            s_mg.clock_after = true;
            missile_leave("clock", now);
            APP_LOGI(TAG, "Clock Mode on (companion), after Missile Defense");
        } else {
            office_zoom_request(&s_zoom, c->u.office); // the same way in and out as from the menu and a tap
            APP_LOGI(TAG, "Clock Mode %s (companion)", c->u.office ? "on" : "off");
        }
        break;
    case LINK_CMD_CLOCK_FACE: {
        office_face_t face;
        if (!has_office) {
            strcpy(body, "err unavailable");
        } else if (!office_face_parse(c->u.face.name, &face) || !s_faces.has[face]) {
            strcpy(body, "err unsupported"); // a face this firmware does not have
        } else {
            app_link_send(c->seq, "ok"); // the reply first, then evt face (if it changed)
            select_face(face, "cmd");
            return;
        }
        break;
    }
    case LINK_CMD_TIME_SET: {
        app_port_wall_set(c->u.time.utc, c->u.time.ms * 1000u);
        s_tz = c->u.time.tz;
        s_tz_known = true;
        struct tm tm;
        uint32_t ms;
        if (wall_clock(&tm, &ms)) {
            APP_LOGI(TAG, "clock set by the companion: %04d-%02d-%02d %02d:%02d:%02d (UTC%+" PRId32 " min)",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                     link_tz_offset_min(&s_tz, c->u.time.utc));
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
            app_link_send(c->seq, "ok"); // the reply first, then the "showing" event
            msg_start(id, c->u.msg.text, screen, brain, now);
            return;
        } else if (s_msg.queued) {
            snprintf(body, sizeof(body), "err busy mode=%s", mode_name(screen, brain));
        } else {
            s_msg.queued = true;
            s_msg.queued_id = s_msg.last_id = id;
            strcpy(s_msg.queued_text, c->u.msg.text);
            strcpy(body, "ok queued=1");
            APP_LOGI(TAG, "message %" PRIu32 " waits for the sign", id);
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
            s_mood_src = "cmd";
            s_smoke_break = false;
            APP_LOGI(TAG, "mood %s for %" PRIu32 " s (0: until changed) (companion)", buddy_mood_name(mood),
                     c->u.mood.secs);
        }
        break;
    }
    case LINK_CMD_HELLO: // answered by the link (app_link.c); a new connection forgets what the previous one offered
        pdc_reset(&s_pdc);
        return;
    case LINK_CMD_PDC_SET:
        if (c->u.pdc_set.on != pdc_available(&s_pdc, now)) {
            APP_LOGI(TAG, "PDC Manager %s (companion)", c->u.pdc_set.on ? "available" : "unavailable");
        }
        pdc_set(&s_pdc, c->u.pdc_set.on, c->u.pdc_set.count, now);
        break;
    case LINK_CMD_PDC_RESULT:
        // Only the click we are waiting for counts: a late or repeated result changes nothing (at most one outcome).
        if (pdc_result(&s_pdc, c->u.pdc_result.id, c->u.pdc_result.result, c->u.pdc_result.count, now)) {
            app_link_send(c->seq, body); // the reply first
            pdc_outcome(c->u.pdc_result.result, c->u.pdc_result.count, c->u.pdc_result.teams, screen, brain, lit,
                        now);
            return;
        }
        APP_LOGI(TAG, "ZDMW: result for click %u ignored (not waiting for it)", (unsigned)c->u.pdc_result.id);
        break;
    case LINK_CMD_LOG_SINCE:
        // The reply follows the replayed events (app_frame sends a few per frame).
        if (app_events_replay_start(c->seq, c->u.log_since.has_boot, c->u.log_since.boot, c->u.log_since.seq)) {
            return;
        }
        strcpy(body, "err busy");
        break;
    default: // ping is answered by the link
        strcpy(body, "err unknown_cmd");
        break;
    }
    app_link_send(c->seq, body);
}

// Turns the backlight on, once both buffers are drawn. A failed attempt is left for the next call: the caller keeps
// calling until it returns true. The backlight is never changed after that: the IO expander that drives it on the
// board did not apply brightness changes reliably (see docs/rendering-results.md).
static bool backlight_on(bool *on)
{
    if (*on) {
        return true;
    }
    *on = app_port_backlight_on();
    return *on;
}

// Everything the frame loop keeps from one call to the next (the board's render task kept it on its stack).
static struct {
    gfx_dirty_t changes, redraw;
    gfx_repair_t repair;
    buddy_brain_t brain;
    buddy_view_t view;
    menu_t menu;
    menu_view_t menu_view;
    office_switch_t screen;
    bool has_menu, has_office;
    bool face_redraw; // the redraw under way is for a new clock face, not a switch

    perf_t perf;
    int64_t perf_since;
    int64_t pending_touch_us; // time of a touch whose reaction is not on screen yet
    uint32_t presented;
    bool pdc_was;    // the companion offered PDC Manager last time round (to log when that ends)
    bool backlight;  // off until both buffers are drawn
    const char *mode; // last mode reported to the companion
    buddy_mood_t mood; // last mood reported
    uint32_t next_demo, next_monster, next_activity, next_office;
    int demo_activity;
} s;

// ---- Tic-Tac-Toe (ttt.h): on while its menu page shows (menu_ttt.h), Buddy playing it (buddy_game.h) ---------------
//
// Leaving the page (Back, the clock's faces) and boredom end it; his mood carries on as it was (or as it was set
// meanwhile). Clock Mode (a tap on his head or mode.set) pauses the round: Buddy is himself again (not busy, so the
// companion's shows and questions work), the page stays, and back from the clock the same round carries on (if no show
// is playing then; else after it), unless it waited TTT_PAUSE_MS. The score stays until reboot. While he plays he is
// busy, as during a show: poke, monster and act get err busy mode=game; nothing about the game is recorded (evt act).

_Static_assert(BUDDY_GAME_REACT_MS == TTT_REACTION_MS, "his reaction lasts as long as the game waits");

static ttt_t s_ttt;
static menu_ttt_t s_ttt_touch;
static bool s_playing;
static uint8_t s_ttt_phase; // last logged

static uint8_t game_attitude(void)
{
    switch (ttt_attitude(&s_ttt)) {
    case TTT_SMUG:
        return BUDDY_GAME_SMUG;
    case TTT_KEEN:
        return BUDDY_GAME_KEEN;
    default:
        return BUDDY_GAME_EVEN;
    }
}

static void game_leave(const char *why, uint32_t now)
{
    if (!s_playing) {
        return;
    }
    s_playing = false;
    ttt_stop(&s_ttt);
    menu_ttt_cancel(&s_ttt_touch);
    buddy_brain_game_leave(&s.brain, now);
    if (s.menu.category == MENU_TTT) {
        menu_open(&s.menu, MENU_GAMES, now);
    }
    APP_LOGI(TAG, "tic-tac-toe: over (%s); you %u, Buddy %u, draws %u", why, (unsigned)s_ttt.you,
             (unsigned)s_ttt.buddy, (unsigned)s_ttt.draws);
}

// Clock Mode: the round waits on its page (ttt_pause), Buddy goes back to his mood.
static void game_pause(uint32_t now)
{
    s_playing = false;
    ttt_pause(&s_ttt, now);
    menu_ttt_cancel(&s_ttt_touch);
    buddy_brain_game_leave(&s.brain, now);
    APP_LOGI(TAG, "tic-tac-toe: paused (clock)");
}

// A paused round: dropped once it has waited too long or its page was left; carried on once Buddy is back on his
// stage, the zoom is over and no show plays. Returns false while it still waits (*wait_ms: until it is dropped).
static bool game_resume(uint32_t now, uint32_t *wait_ms)
{
    *wait_ms = ttt_tick(&s_ttt, now); // drops it after TTT_PAUSE_MS
    const bool page = s.has_menu && s.menu.category == MENU_TTT;
    if (s_ttt.phase == TTT_OFF || !page) {
        ttt_stop(&s_ttt);
        if (page) {
            menu_open(&s.menu, MENU_GAMES, now);
        }
        APP_LOGI(TAG, "tic-tac-toe: paused round dropped (%s); you %u, Buddy %u, draws %u",
                 page ? "waited too long" : "left", (unsigned)s_ttt.you, (unsigned)s_ttt.buddy, (unsigned)s_ttt.draws);
        *wait_ms = UINT32_MAX;
        return false;
    }
    if (s.screen.office || !scene_settled(&s.screen) || buddy_brain_busy(&s.brain, now)) {
        return false;
    }
    ttt_resume(&s_ttt, now);
    s_playing = true;
    APP_LOGI(TAG, "tic-tac-toe: resumed");
    return true;
}

// The face he pulls now: from the phase of the round and when it began, so it only changes when the game moves on.
static buddy_game_t game_face(uint32_t now)
{
    buddy_game_t f = {BUDDY_GAME_WATCH, game_attitude(), (int8_t)(s_cfg.panel_left ? -1 : 1), s_ttt.phase_at, 0, 0};
    switch (s_ttt.phase) {
    case TTT_YOUR_TURN:
    case TTT_THINKING:
        if (now - s_ttt.round_at < BUDDY_GAME_READY_MS) {
            f.face = BUDDY_GAME_READY;
            f.since = s_ttt.round_at;
        } else {
            f.face = s_ttt.phase == TTT_THINKING ? BUDDY_GAME_THINK : BUDDY_GAME_WATCH;
        }
        break;
    case TTT_REACTION:
    case TTT_REMATCH:
        f.face = s_ttt.result == TTT_O_WINS ? BUDDY_GAME_WON : s_ttt.result == TTT_X_WINS ? BUDDY_GAME_LOST
                                                                                            : BUDDY_GAME_DREW;
        f.since = s_ttt.phase == TTT_REACTION ? s_ttt.phase_at : s_ttt.phase_at - TTT_REACTION_MS;
        break;
    default:
        break;
    }
    return f;
}

// Once per frame, before anything else moves: starts or ends the game, moves it on, and gives Buddy his face.
// Returns ms until the game changes on its own.
static uint32_t game_update(uint32_t now)
{
    if (s_ttt.paused) {
        uint32_t wait;
        if (!game_resume(now, &wait)) {
            return wait;
        }
    }
    if (!s_playing && (!s.has_menu || s.menu.category != MENU_TTT)) {
        return UINT32_MAX;
    }
    if (s_playing && (s.menu.category != MENU_TTT)) {
        game_leave("left", now);
        return UINT32_MAX;
    }
    if (s.screen.office || s_zoom.want_office) { // Clock Mode pauses the round
        if (s_playing) {
            game_pause(now);
        } else {
            menu_open(&s.menu, MENU_GAMES, now);
        }
        return UINT32_MAX;
    }
    if (!s_playing) {
        const buddy_game_t ready = {BUDDY_GAME_READY, game_attitude(), 1, now, 0, 0};
        if (!buddy_brain_game(&s.brain, &ready, now)) { // a show started first: he plays once it is over
            menu_open(&s.menu, MENU_GAMES, now);
            return UINT32_MAX;
        }
        ttt_start(&s_ttt, now);
        s_playing = true;
        s_ttt_phase = TTT_OFF;
        APP_LOGI(TAG, "tic-tac-toe: on (%s first)", s_ttt.you_started ? "you" : "Buddy");
    }
    uint32_t next = ttt_tick(&s_ttt, now);
    if (ttt_bored(&s_ttt, now)) {
        game_leave("bored", now);
        return UINT32_MAX;
    }
    if (s_ttt.phase != s_ttt_phase) {
        s_ttt_phase = s_ttt.phase;
        if (s_ttt.phase == TTT_REACTION) {
            APP_LOGI(TAG, "tic-tac-toe: %s (you %u, Buddy %u, draws %u)",
                     s_ttt.result == TTT_X_WINS ? "you win" : s_ttt.result == TTT_O_WINS ? "Buddy wins" : "draw",
                     (unsigned)s_ttt.you, (unsigned)s_ttt.buddy, (unsigned)s_ttt.draws);
        }
    }
    const buddy_game_t face = game_face(now);
    buddy_brain_game(&s.brain, &face, now);
    if (s_ttt.phase == TTT_YOUR_TURN || s_ttt.phase == TTT_THINKING) { // the ready hop gives way on time
        const uint32_t t = now - s_ttt.round_at;
        if (t < BUDDY_GAME_READY_MS && BUDDY_GAME_READY_MS - t < next) {
            next = BUDDY_GAME_READY_MS - t;
        }
    }
    return next;
}

// A touch while the game is on: the board and the REMATCH button are the game's (the header stays the menu's).
// Returns true if the game took it.
static bool game_touch(const app_event_t *ev, menu_touch_t type, uint32_t now)
{
    if (!s_playing) {
        return false;
    }
    if (ev->type == APP_TOUCH_DOWN) {
        ttt_touched(&s_ttt, now); // any touch keeps him from getting bored
    }
    const int hit = menu_ttt_touch(&s_ttt_touch, &s_ttt, type, ev->x, ev->y);
    if (hit == MENU_TTT_HIT_STRIP) {
        if (ttt_rematch(&s_ttt, now)) {
            APP_LOGI(TAG, "tic-tac-toe: rematch (%s first)", s_ttt.you_started ? "you" : "Buddy");
        }
    } else if (hit != MENU_TTT_HIT_NONE) {
        ttt_play(&s_ttt, hit, now); // refused on his turn: he protests (ttt_line)
    }
    if (ev->type == APP_TOUCH_DOWN && menu_ttt_holding(&s_ttt_touch) && !s.pending_touch_us) {
        s.pending_touch_us = ev->at_us; // the cell sinks in
    }
    return menu_ttt_holding(&s_ttt_touch) || hit != MENU_TTT_HIT_NONE;
}

// ---- Missile Defense (md.h) on the whole screen; its stages: s_mg ----------------------------------------------------
//
// GAMES -> MISSILES: Buddy's alert on his stage ("INCOMING!"), then the game scene (both buffers redrawn, as for the
// clock): the game view's background and overlay, Buddy at his game place (md_view.h), the menu folded away. The X,
// QUIT, nobody touching the game-over screen, and Clock Mode end it: the same switch back, the menu on GAMES. While it
// is on he is playing (BUDDY_STATE_GAME: mode=game, shows refused as busy), messages wait, and nothing is recorded.

static const char k_incoming[] = "INCOMING!";

static void missile_start(uint32_t now)
{
    if (md_on() || !s_mg.has_view || s.screen.office || !scene_settled(&s.screen)) {
        return;
    }
    const buddy_game_t alert = {BUDDY_GAME_MD_ALERT, BUDDY_GAME_EVEN, 0, now, 0, 0};
    if (!buddy_brain_game(&s.brain, &alert, now)) {
        return; // a show is playing (the tile shows him busy)
    }
    s_mg.stage = MDS_ALERT;
    s_mg.alert_at = now;
    s_mg.clock_after = false;
    APP_LOGI(TAG, "missile defense: incoming!");
}

// Back to his stage (an alert is just dropped). Clock Mode asked for meanwhile follows once he is back.
static void missile_leave(const char *why, uint32_t now)
{
    switch (s_mg.stage) {
    case MDS_ALERT:
        s_mg.stage = MDS_OFF;
        buddy_brain_game_leave(&s.brain, now);
        if (s_mg.clock_after) {
            s_mg.clock_after = false;
            office_zoom_request(&s_zoom, true);
        }
        break;
    case MDS_IN:
    case MDS_PLAY:
        s_mg.stage = MDS_OUT;
        md_view_cancel(&s_md_view);
        office_switch_request(&s_mg.sw, false);
        break;
    default:
        return;
    }
    APP_LOGI(TAG, "missile defense: over (%s); wave %u, score %u, best %u", why, (unsigned)s_md.wave,
             (unsigned)s_md.score, (unsigned)s_md.best);
}

// The scene switch's swap: the game scene comes up (in) or his stage and the menu come back.
static void missile_swap(bool in, uint32_t now)
{
    if (in) {
        menu_cancel(&s.menu);
        menu_view_show(&s.menu_view, false);
        buddy_view_camera(&s.view, MD_BUDDY_SCALE, MD_BUDDY_X0, MD_BUDDY_Y0);
        s_scene.background = md_view_background;
        s_scene.background_ctx = &s_md_view;
        s_scene.overlay = md_view_overlay;
        s_scene.overlay_ctx = &s_md_view;
        md_view_show(&s_md_view, true);
        md_start(&s_md, now);
        if (s_mg.stage == MDS_IN) {
            s_mg.stage = MDS_PLAY;
        }
        return;
    }
    md_view_show(&s_md_view, false);
    md_stop(&s_md);
    s_scene.background = layout_background;
    s_scene.background_ctx = &s_layout;
    s_scene.overlay = NULL;
    s_scene.overlay_ctx = NULL;
    buddy_view_camera_home(&s.view);
    menu_view_show(&s.menu_view, true);
    menu_reveal(&s.menu, now); // GAMES, flipping back in
    buddy_brain_game_leave(&s.brain, now); // his mood carries on (one chosen meanwhile shows now)
}

// A full redraw finished: logged like the clock's, and the performance windows start afresh (so they cover only the
// game, or only the normal screen).
static void missile_drawn(const char *what)
{
    app_scanout_stats_t sw;
    app_port_scanout_stats(&sw);
    APP_LOGI(TAG, "%s drawn (%" PRIu32 " scanout resyncs, fill max %" PRIu32 " us)", what, sw.resyncs, sw.fill_us_max);
    strips_log(what, &sw);
    memset(&s.perf, 0, sizeof(s.perf));
    s.perf_since = app_port_time_us();
}

// Once per frame, before Buddy's pose: the alert, the switch, the game and his face. Returns ms until any of it
// changes on its own.
static uint32_t missile_update(uint32_t now)
{
    if (!md_on()) {
        return UINT32_MAX;
    }
    if (s_mg.stage == MDS_ALERT) {
        if (now - s_mg.alert_at < MD_ALERT_MS) {
            return MD_ALERT_MS - (now - s_mg.alert_at);
        }
        s_mg.stage = MDS_IN;
        office_switch_request(&s_mg.sw, true);
        perf_log(&s.perf, app_port_time_us() - s.perf_since); // the window before the switch
        memset(&s.perf, 0, sizeof(s.perf));
        s.perf_since = app_port_time_us();
    }
    const office_step_t step = office_switch_step(&s_mg.sw);
    if (step.swap) {
        missile_swap(s_mg.sw.office, now);
    }
    if (step.redraw_all) {
        gfx_scene_invalidate(&s_scene, s_layout.screen);
    } else if (step.redraw_kick) {
        gfx_scene_invalidate(&s_scene, (gfx_rect_t){0, 0, 1, 1}); // the repair redraws the other buffer fully
    }
    if (step.redrawn) {
        missile_drawn(s_mg.sw.office ? "Missile Defense" : "DeskBuddy");
        if (!s_mg.sw.office) {
            s_mg.stage = MDS_OFF;
            if (s_mg.clock_after) {
                s_mg.clock_after = false;
                office_zoom_request(&s_zoom, true);
            }
            return 0;
        }
    }
    uint32_t next = step.next_ms;
    if (s_mg.stage == MDS_PLAY) {
        const uint32_t n = md_tick(&s_md, now);
        next = n < next ? n : next;
        if (md_bored(&s_md, now)) {
            missile_leave("nobody playing", now);
        } else {
            const buddy_game_t face = md_buddy_face(&s_md, now);
            buddy_brain_game(&s.brain, &face, now);
        }
    }
    return step.redrawn ? 0 : next;
}

// A touch while the game is on: shots as the finger comes down, the buttons on release.
static void missile_touch(const app_event_t *ev, uint32_t now)
{
    if (ev->type == APP_TOUCH_DOWN) {
        md_touched(&s_md, now);
    }
    const md_view_touch_t type = ev->type == APP_TOUCH_DOWN   ? MD_VIEW_TOUCH_DOWN
                                 : ev->type == APP_TOUCH_MOVE ? MD_VIEW_TOUCH_MOVE
                                                              : MD_VIEW_TOUCH_UP;
    switch (md_view_touch(&s_md_view, &s_md, type, ev->x, ev->y, now)) {
    case MD_VIEW_HIT_SKY:
        md_fire(&s_md, ev->x, ev->y, now);
        break;
    case MD_VIEW_HIT_EXIT:
        missile_leave("exit", now);
        break;
    case MD_VIEW_HIT_QUIT:
        missile_leave("quit", now);
        break;
    case MD_VIEW_HIT_RETRY:
        APP_LOGI(TAG, "missile defense: retry (score %u, best %u)", (unsigned)s_md.score, (unsigned)s_md.best);
        md_start(&s_md, now);
        break;
    default:
        break;
    }
    if (ev->type == APP_TOUCH_DOWN && !s.pending_touch_us) {
        s.pending_touch_us = ev->at_us; // the shot, or a button sinking in: the reaction to measure
    }
}

// A face chosen on the touchscreen or by the companion: shown at once if the clock is up (a full redraw of both
// buffers, like the switch), and reported (evt face) if it changed.
static void select_face(office_face_t face, const char *src)
{
    if (face == s_faces.face || !office_faces_select(&s_faces, face)) {
        return;
    }
    if (s_faces.visible) {
        s_scene.background = office_faces_background(&s_faces, &s_scene.background_ctx);
        office_switch_redraw(&s.screen);
        s.face_redraw = true;
    }
    char body[48];
    snprintf(body, sizeof(body), "evt face name=%s src=%s", office_face_name(face), src);
    app_link_send(0, body);
    APP_LOGI(TAG, "clock face %s (%s)", office_face_name(face), src);
}

bool app_init(const app_config_t *config)
{
    s_cfg = *config;
    W = s_cfg.width;
    H = s_cfg.height;
    // Wall-clock time is kept in UTC; the companion sets it and the local offset (time.set). The board has no RTC:
    // until the companion has sent the time, Office Mode says "CLOCK NOT SET" instead of showing a made-up time.
    s_layout = layout_compute(W, H, s_cfg.stage_percent, s_cfg.panel_left, X_ALIGN);

    gfx_color_t *strip = app_port_alloc(STRIP_PX * sizeof(gfx_color_t), APP_MEM_INTERNAL);
    if (!strip || !gfx_scene_init(&s_scene, W, H, layout_background, &s_layout, strip, STRIP_PX, X_ALIGN)) {
        APP_LOGE(TAG, "no internal RAM for the composition strip");
        return false;
    }
    if (s_cfg.gfx_verify) {
        s_verify_px = app_port_alloc((size_t)W * H * sizeof(gfx_color_t), APP_MEM_LARGE);
        if (!s_verify_px) {
            APP_LOGE(TAG, "no PSRAM for the verify buffer");
            return false;
        }
        APP_LOGW(TAG, "GFX_VERIFY enabled: every frame is checked against a full redraw (slow)");
    }

    gfx_dirty_init(&s.changes, W, H, X_ALIGN);
    gfx_dirty_init(&s.redraw, W, H, X_ALIGN);
    gfx_repair_init(&s.repair, W, H, X_ALIGN);

    const uint32_t t_start = now_ms();
    if (!buddy_view_init(&s.view, &s_scene, s_layout.stage, 10, t_start)) {
        APP_LOGE(TAG, "character does not fit the stage or art is invalid");
        return false;
    }
    // The menu's pixels are read by the compositor whenever a button redraws: internal RAM if there is room.
    uint8_t *menu_mem = alloc_internal_first(MENU_VIEW_MEMORY);
    s.has_menu = menu_init(&s.menu, &menu_default, layout_panel_content(&s_layout), t_start) &&
                 menu_view_init(&s.menu_view, &s_scene, &s.menu, menu_mem, MENU_VIEW_MEMORY, 10);
    if (s.has_menu) {
        menu_ttt_init(&s_ttt_touch, &s.menu);
    }
    if (!s.has_menu) {
        APP_LOGW(TAG, "no touchscreen menu (the panel is too small, or no memory)");
    }
    const int head_top = s.view.home_y0 - BUDDY_MAX_LIFT * s.view.home_scale;
    if (!menu_ask_init(&s_ask, &s_scene, s_layout.stage, head_top, 25)) {
        APP_LOGW(TAG, "Buddy cannot ask questions (they do not fit the stage)");
    }
    // Read by the compositor whenever the thought redraws: internal RAM if there is room (as the menu).
    uint8_t *thought_mem = alloc_internal_first(MENU_THOUGHT_MEMORY);
    const int head_cx = s.view.home_x0 + s.view.art->w * s.view.home_scale / 2;
    if (!menu_thought_init(&s_thought, &s_scene, s_layout.stage, head_cx, thought_mem, MENU_THOUGHT_MEMORY, 5)) {
        APP_LOGW(TAG, "Buddy's thoughts cannot be shown (no memory, or they do not fit the stage)");
    }
    // The faces' pixels are only read when the clock redraws: PSRAM. Classic is Clock Mode itself; Mechanical
    // Skeleton is left out if its memory is missing. Its frame (runs read on every redraw of the face) goes into
    // internal RAM if there is room, else it is read from the firmware's constant data.
    uint8_t *clock_mem = app_port_alloc(OFFICE_CLOCK_MEMORY, APP_MEM_LARGE);
    uint8_t *skeleton_mem = app_port_alloc(OFFICE_SKELETON_MEMORY, APP_MEM_LARGE);
    uint8_t *frame_mem = app_port_alloc(office_skeleton_frame_memory(), APP_MEM_INTERNAL);
    const int64_t faces_t0 = app_port_time_us();
    s.has_office = clock_mem && office_faces_init(&s_faces, &s_scene, s_layout.screen, clock_mem, skeleton_mem,
                                                  frame_mem, flush_bytes, 20);
    if (!s.has_office) {
        APP_LOGW(TAG, "Clock Mode unavailable (no memory for the clock)");
    } else {
        APP_LOGI(TAG, "clock faces ready in %.1f ms: classic%s (skeleton %u KB of %u KB, frame %u B in %s)",
                 (app_port_time_us() - faces_t0) / 1000.0, s_faces.has[OFFICE_FACE_SKELETON] ? ", skeleton" : "",
                 (unsigned)(s_faces.skeleton.mem_used / 1024), (unsigned)(OFFICE_SKELETON_MEMORY / 1024),
                 (unsigned)office_skeleton_frame_memory(),
                 s_faces.skeleton.frame_copied ? "internal RAM" : "constant data");
    }
    office_switch_init(&s.screen);
    office_switch_init(&s_mg.sw);
    s_mg.has_view = md_view_init(&s_md_view, &s_scene);
    if (!s_mg.has_view) {
        APP_LOGW(TAG, "Missile Defense unavailable (its art is malformed)");
    }
    office_zoom_init(&s_zoom);
    s_msg.ready = marquee_init(&s_msg.sign, &s_scene, 30); // above everything
    if (!s_msg.ready) {
        APP_LOGW(TAG, "no room in the scene for the message marquee");
    }

    app_events_init();
    buddy_brain_init(&s.brain, t_start, app_port_random());
    ttt_init(&s_ttt, app_port_random());
    pdc_init(&s_pdc, (uint16_t)app_port_random()); // click ids start somewhere new on every boot
    s_ia.next_own = app_port_random();              // as do the device's own interaction ids
    gfx_scene_invalidate(&s_scene, s_layout.screen); // both buffers start black: first two frames are full
    APP_LOGI(TAG, "stage %dx%d at x=%d, panel %dx%d at x=%d; character scale %d at (%d,%d); scene sprites %d of %d",
             s_layout.stage.w, s_layout.stage.h, s_layout.stage.x, s_layout.panel.w, s_layout.panel.h,
             s_layout.panel.x, s.view.scale, s.view.x0, s.view.y0, s_scene.count, GFX_SCENE_MAX_SPRITES);

    s.perf_since = app_port_time_us();
    s.mood = buddy_brain_mood(&s.brain, t_start);
    s.next_demo = t_start + (uint32_t)s_cfg.demo_poke_s * 1000u;
    s.next_monster = t_start + (uint32_t)s_cfg.demo_monster_s * 1000u;
    s.next_activity = t_start + (uint32_t)s_cfg.demo_activity_s * 1000u;
    s.next_office = t_start + (uint32_t)s_cfg.demo_office_s * 1000u;
    return true;
}

// MY DAY: you say what you are doing. The companion records it (evt did, sent for touches only, never for what it
// commanded itself); Buddy cheers (a hop, unless a show is playing) and puts the item's line on his sign.
static void myday(const menu_item_t *it, uint32_t now)
{
    char body[64];
    snprintf(body, sizeof(body), "evt did item=%s", it->id);
    record(body);
    APP_LOGI(TAG, "MY DAY: %s", it->id);
    if (!s.screen.office && scene_settled(&s.screen)) {
        buddy_brain_event(&s.brain, BUDDY_EVENT_POKE, now);
    }
    if (menu_item_note(it)) {
        show_line(menu_item_note(it), &s.screen, &s.brain, s.presented >= 2, now);
    }
}

void app_event(const app_event_t *ev)
{
    if (ev->type == APP_COMMAND) {
        run_command(&ev->command, &s.brain, &s.screen, s.has_office, s.presented >= 2, now_ms());
        return;
    }
    // Touches count once the screen is lit and no scene change (Buddy's zoom, the switch) is under way or asked
    // for: taps during a transition are dropped, so repeated taps cannot pile up.
    if (s.presented < 2 || !scene_settled(&s.screen)) {
        return;
    }
    // In Office Mode any tap (on release) brings DeskBuddy back. Otherwise the menu takes the touches in the control
    // panel (it acts on release, see menu.h), and a touch-down on the robot pokes him.
    const uint32_t t = now_ms();
    if (s_mg.stage == MDS_PLAY) { // the game takes every touch on its screen
        missile_touch(ev, t);
        return;
    }
    if (s.screen.office) {
        if (ev->type == APP_TOUCH_UP) {
            office_zoom_request(&s_zoom, false); // he wakes up
            APP_LOGI(TAG, "Clock Mode off");
        }
        return;
    }
    // His question takes the touches that go down on its buttons (and answers on release).
    const menu_touch_t type = ev->type == APP_TOUCH_DOWN   ? MENU_TOUCH_DOWN
                              : ev->type == APP_TOUCH_MOVE ? MENU_TOUCH_MOVE
                                                           : MENU_TOUCH_UP;
    const menu_answer_t answer = menu_ask_touch(&s_ask, type, ev->x, ev->y);
    const bool confirming = s.brain.state == BUDDY_STATE_CONFIRM; // which question it answers
    if (answer != MENU_ANSWER_NONE && buddy_brain_answer(&s.brain, answer == MENU_ANSWER_YES, t)) {
        if (!confirming) {
            APP_LOGI(TAG, "peukie? %s", answer == MENU_ANSWER_YES ? "ja" : "nee");
            if (answer == MENU_ANSWER_YES && s.brain.mood_timed && s.brain.mood == BUDDY_MOOD_PEUKIE) {
                s_mood_src = "break"; // the smoke break starts now...
                s_smoke_break = true; // ...and its end is reported as src=break too
            }
        } else if (answer == MENU_ANSWER_YES) {
            zdmw_send(&s.screen, &s.brain, t);
        } else {
            APP_LOGI(TAG, "ZDMW: cancelled (NEE), nothing sent");
        }
    }
    if (menu_ask_holding(&s_ask)) {
        if (ev->type == APP_TOUCH_DOWN && !s.pending_touch_us) {
            s.pending_touch_us = ev->at_us; // the button sinks in
        }
        return;
    }
    if (answer != MENU_ANSWER_NONE) {
        return;
    }
    if (game_touch(ev, type, t)) {
        return;
    }
    if (s.has_menu) {
        const menu_status_t st = menu_status(&s.brain, s.has_office, t);
        const menu_result_t r = menu_touch(&s.menu, type, ev->x, ev->y, &st, t);
        run_menu_action(&r, &s.brain, t);
        if (r.action == MENU_DO_LOG) {
            myday(r.item, t);
        }
        if (s.menu.down) {
            if (ev->type == APP_TOUCH_DOWN && !s.pending_touch_us) {
                s.pending_touch_us = ev->at_us; // the button sinks in: the reaction to measure
            }
            return;
        }
    }
    // A touch-down on the robot pokes him; KITT, fully turned, turns back instead.
    if (ev->type == APP_TOUCH_DOWN && buddy_view_hit(&s.view, ev->x, ev->y)) {
        const bool kitt = buddy_brain_kitt_leave(&s.brain, t);
        if (kitt) {
            APP_LOGI(TAG, "KITT: turning back (touch)");
        }
        if ((kitt || buddy_brain_event(&s.brain, BUDDY_EVENT_POKE, t)) && !s.pending_touch_us) {
            s.pending_touch_us = ev->at_us;
        }
    }
}

// The automatic demo actions (app_config_t.demo_*), for hands-off measurements.
static void run_demos(uint32_t now)
{
    if (s_cfg.demo_poke_s > 0 && (int32_t)(now - s.next_demo) >= 0) {
        buddy_brain_event(&s.brain, BUDDY_EVENT_POKE, now);
        s.next_demo = now + (uint32_t)s_cfg.demo_poke_s * 1000u;
    }
    if (s_cfg.demo_monster_s > 0 && (int32_t)(now - s.next_monster) >= 0) {
        if (!s.screen.office && buddy_brain_event(&s.brain, BUDDY_EVENT_DRINK_MONSTER, now)) {
            APP_LOGI(TAG, "Monster time (demo)");
        }
        s.next_monster = now + (uint32_t)s_cfg.demo_monster_s * 1000u;
    }
    if (s_cfg.demo_activity_s > 0 && (int32_t)(now - s.next_activity) >= 0) {
        static const buddy_event_t k_demo[] = {BUDDY_EVENT_DRINK_WATER, BUDDY_EVENT_SNEEZE, BUDDY_EVENT_STARE,
                                               BUDDY_EVENT_MEDITATE,    BUDDY_EVENT_FOCUS,
                                               BUDDY_EVENT_ASK_PEUKIE}; // unanswered: sad
        const buddy_event_t ev = k_demo[s.demo_activity];
        if (!s.screen.office && scene_settled(&s.screen) && buddy_brain_event(&s.brain, ev, now)) {
            APP_LOGI(TAG, "activity %d (demo)", (int)ev);
            s.demo_activity = (s.demo_activity + 1) % (int)(sizeof(k_demo) / sizeof(k_demo[0]));
        }
        s.next_activity = now + (uint32_t)s_cfg.demo_activity_s * 1000u;
    }
    if (s_cfg.demo_office_s > 0 && (int32_t)(now - s.next_office) >= 0 && s.has_office && s.presented >= 2) {
        office_zoom_request(&s_zoom, !s_zoom.want_office);
        APP_LOGI(TAG, "Clock Mode %s (demo)", s_zoom.want_office ? "on" : "off");
        s.next_office = now + (uint32_t)s_cfg.demo_office_s * 1000u;
    }
}

// The wait shortened to the next demo action that has one (pokes, Monster sequences, activities).
static uint32_t demo_wait(uint32_t wait_ms)
{
    const int secs[] = {s_cfg.demo_poke_s, s_cfg.demo_monster_s, s_cfg.demo_activity_s};
    const uint32_t at[] = {s.next_demo, s.next_monster, s.next_activity};
    for (int i = 0; i < 3; i++) {
        if (secs[i] > 0) {
            const int32_t to = (int32_t)(at[i] - now_ms());
            wait_ms = to <= 0 ? 0 : ((uint32_t)to < wait_ms ? (uint32_t)to : wait_ms);
        }
    }
    return wait_ms;
}

uint32_t app_frame(void)
{
    const uint32_t now = now_ms();
    run_demos(now);
    const uint32_t game_next = game_update(now);
    const uint32_t md_next = missile_update(now);

    // Buddy's zoom into Office Mode and back: it asks the scene switch for the clock or for Buddy.
    const office_zoom_step_t zoom = office_zoom_step(&s_zoom, &s.screen, now);
    if (zoom.menu_hide) {
        menu_cancel(&s.menu);
        menu_view_close(&s.menu_view, now);
        perf_log(&s.perf, app_port_time_us() - s.perf_since); // the next window covers exactly the zoom in
        memset(&s.perf, 0, sizeof(s.perf));
        s.perf_since = app_port_time_us();
    }
    if (zoom.menu_show && s.has_menu) {
        menu_view_show(&s.menu_view, true);
        menu_reveal(&s.menu, now); // the page he left, flipping back in
    }
    if (zoom.back) {
        buddy_view_camera_home(&s.view);
        brain_rest(&s.brain, now); // his mood carries on (a mood chosen meanwhile shows now)
        zoom_log("out", &s.perf, app_port_time_us() - s.perf_since);
        memset(&s.perf, 0, sizeof(s.perf));
        s.perf_since = app_port_time_us();
    }

    // Scene switch: the scene changes and both buffers are redrawn.
    const office_step_t step = office_switch_step(&s.screen);
    const bool light_ok = s.presented < 2 || backlight_on(&s.backlight);
    if (step.swap) {
        if (s.screen.office) {
            buddy_view_hide(&s.view); // the character pauses; a running Monster sequence is dropped on return
            menu_cancel(&s.menu);
            menu_view_show(&s.menu_view, false);
            office_faces_show(&s_faces, true);
            s_scene.background = office_faces_background(&s_faces, &s_scene.background_ctx);
        } else { // his dark face first (the zoom draws it); the menu comes back as he steps back
            office_faces_show(&s_faces, false);
            s_scene.background = layout_background;
            s_scene.background_ctx = &s_layout;
            brain_rest(&s.brain, now); // a paused Monster sequence is dropped (the mode is normal again)
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
        if (s.face_redraw) {
            app_scanout_stats_t sw;
            app_port_scanout_stats(&sw);
            APP_LOGI(TAG, "clock face drawn (%" PRIu32 " scanout resyncs, fill max %" PRIu32 " us)", sw.resyncs,
                     sw.fill_us_max);
            strips_log("face", &sw);
            s.face_redraw = false;
        } else if (s.screen.office) {
            zoom_log("in", &s.perf, app_port_time_us() - s.perf_since);
        } else {
            app_scanout_stats_t sw;
            app_port_scanout_stats(&sw);
            APP_LOGI(TAG, "DeskBuddy drawn (%" PRIu32 " scanout resyncs, fill max %" PRIu32 " us)", sw.resyncs,
                     sw.fill_us_max);
            strips_log("DeskBuddy", &sw);
        }
        memset(&s.perf, 0, sizeof(s.perf));
        s.perf_since = app_port_time_us();
    }

    // PDC Manager: when the companion stops offering it, nothing about it stays on screen (a confirmation, its
    // page); a click that got no answer at all is given up (maybe it was sent).
    const bool pdc_now = pdc_available(&s_pdc, now);
    if (!pdc_now) {
        if (s.pdc_was) {
            APP_LOGI(TAG, "PDC Manager no longer offered (the lease ran out, or a new companion connection)");
        }
        if (s.brain.state == BUDDY_STATE_CONFIRM) {
            brain_rest(&s.brain, now);
            APP_LOGI(TAG, "ZDMW: confirmation withdrawn (the companion no longer offers PDC Manager)");
        }
        if (s.menu.category == MENU_PDC) {
            menu_open(&s.menu, MENU_HOME, now);
        }
    }
    s.pdc_was = pdc_now;
    if (pdc_expired(&s_pdc, now)) {
        pdc_outcome(LINK_PDC_TIMEOUT, -1, -1, &s.screen, &s.brain, s.presented >= 2, now);
    }

    uint32_t next;
    if (s.screen.office) {
        struct tm tm;
        uint32_t ms = 0;
        const bool known = wall_clock(&tm, &ms);
        next = office_faces_apply(&s_faces, known ? &tm : NULL, ms);
        menu_thought_apply(&s_thought, NULL);
    } else {
        buddy_pose_t pose;
        next = buddy_brain_tick(&s.brain, now, &pose); // his brain keeps time, also while the zoom draws him
        if (zoom.animate) {
            const office_camera_t home = {s.view.home_scale, s.view.home_x0, s.view.home_y0};
            office_camera_t cam;
            next = office_zoom_frame(&s_zoom, now, home, s_layout.screen, &cam, &pose);
            buddy_view_camera(&s.view, cam.scale, cam.x0, cam.y0);
        }
        const uint32_t view_next = buddy_view_apply(&s.view, &pose, now);
        next = view_next < next ? view_next : next;
        if (s_md_view.shown) { // the game around him; his beams start at his eyes
            int ex[2], ey[2];
            for (int i = 0; i < 2; i++) {
                const gfx_rect_t e = gfx_sprite_bounds(&s.view.eye[i]);
                ex[i] = e.x + e.w / 2;
                ey[i] = e.y + e.h / 2;
            }
            const uint32_t md_view_next = md_view_apply(&s_md_view, &s_md, ex, ey, now);
            next = md_view_next < next ? md_view_next : next;
        }
        // His thoughts: not while the camera moves, nor in the game.
        menu_thought_apply(&s_thought, zoom.animate || md_on() ? NULL : &pose.thought);
        if (pose.say != s_msg.said) { // a line of his own on the sign, if it is free
            s_msg.said = pose.say;
            if (pose.say && s_msg.ready && !marquee_active(&s_msg.sign) && !s_msg.queued && !md_on()) {
                marquee_start(&s_msg.sign, buddy_say_text((buddy_say_t)pose.say), now);
                s_msg.caption = true;
            }
        }
        if (s.has_menu) {
            menu_status_t st = menu_status(&s.brain, s.has_office, now);
            menu_game_t game;
            if (s_playing) {
                const uint32_t look_next = menu_ttt_look(&game, &s_ttt, &s_ttt_touch, now);
                next = look_next < next ? look_next : next;
                st.game = &game;
            } else if (s_ttt.paused) { // back from the clock, not resumed yet: the board as it was left
                menu_ttt_look(&game, &s_ttt, NULL, s_ttt.paused_at);
                st.game = &game;
            }
            uint32_t hold_wait;
            if (menu_hold(&s.menu, &st, now, &hold_wait)) {
                APP_LOGI(TAG, "menu: clock faces (long press on his head)");
            }
            const uint32_t menu_next = menu_view_apply(&s.menu_view, &s.menu, &st, now);
            next = menu_next < next ? menu_next : next;
            next = hold_wait < next ? hold_wait : next;
        }
    }
    // The question shows while he asks it, his game lines while he plays (Tic-Tac-Toe), his alert before Missile
    // Defense: not during the zoom or in Clock Mode.
    const uint32_t ask_left = !s.screen.office && !zoom.animate ? buddy_brain_asking(&s.brain, now) : 0;
    const char *question = s.brain.state == BUDDY_STATE_CONFIRM ? k_ask_zdmw : k_ask_peukie;
    const uint32_t ask_next =
        s_playing && !zoom.animate ? menu_ask_say(&s_ask, ttt_line(&s_ttt, now))
        : s_mg.stage == MDS_ALERT
            ? menu_ask_say(&s_ask, k_incoming)
            : menu_ask_apply(&s_ask, ask_left ? question : NULL, ask_left, buddy_activity_ms(s.brain.state));
    next = ask_next < next ? ask_next : next;
    next = game_next < next ? game_next : next;
    next = md_next < next ? md_next : next;
    const uint32_t pdc_wake = pdc_next(&s_pdc, now); // the lease, the cooldown, a click without answer
    next = pdc_wake < next ? pdc_wake : next;
    next = step.next_ms < next ? step.next_ms : next;
    next = step.redrawn ? 0 : next; // the zoom carries on (and a waiting message goes up) right away
    next = !light_ok && next > BACKLIGHT_RETRY_MS ? BACKLIGHT_RETRY_MS : next;

    // The message sign: above the robot, or below the clock in Office Mode (which stays on).
    if (s_msg.ready) {
        // In the game scene: at the top of the sky, until a message that was already up has run.
        const bool game = s_md_view.shown;
        const gfx_rect_t area = s.screen.office || game ? s_layout.screen : s_layout.stage;
        const int mx = area.x + (area.w - MARQUEE_W) / 2;
        const int my = s.screen.office ? area.y + area.h - MARQUEE_H - 36 : game ? MD_SKY_TOP + 8 : area.y + 22;
        const bool was_showing = marquee_active(&s_msg.sign);
        uint32_t sign_next = marquee_apply(&s_msg.sign, mx, my, now);
        if (was_showing && !marquee_active(&s_msg.sign)) {
            if (s_msg.showing_id) {
                char body[48];
                snprintf(body, sizeof(body), "evt msg id=%" PRIu32 " state=done", s_msg.showing_id);
                app_link_send(0, body);
            }
            s_msg.showing_id = 0;
            s_msg.caption = false;
        }
        if (s_msg.queued && msg_can_start(&s.screen, &s.brain, s.presented >= 2)) {
            s_msg.queued = false;
            msg_start(s_msg.queued_id, s_msg.queued_text, &s.screen, &s.brain, now);
            sign_next = marquee_apply(&s_msg.sign, mx, my, now);
        }
        next = sign_next < next ? sign_next : next;
    }

    const char *new_mode = mode_name(&s.screen, &s.brain);
    if (new_mode != s.mode) {
        if (s.mode) {
            char body[32];
            snprintf(body, sizeof(body), "evt mode mode=%s", new_mode);
            app_link_send(0, body);
        }
        s.mode = new_mode;
    }
    const buddy_mood_t new_mood = buddy_brain_mood(&s.brain, now); // also ends a timed mood, in Office Mode too
    if (new_mood != s.mood) {
        // Why: chosen (touch, cmd), a smoke break starting or ending (break), or a timed mood running out (timer).
        const char *src = s_mood_src ? s_mood_src : s_smoke_break ? "break" : "timer";
        if (!s_mood_src) {
            s_smoke_break = false; // a break that ended by itself
        }
        char body[48];
        snprintf(body, sizeof(body), "evt mood mood=%s src=%s", buddy_mood_name(new_mood), src);
        app_link_send(0, body);
        s.mood = new_mood;
    }
    s_mood_src = NULL; // a choice that changed nothing explains nothing later

    bool presented = false;
    gfx_dirty_clear(&s.changes);
    gfx_scene_collect(&s_scene, &s.changes);
    if (s.changes.count > 0) {
        gfx_repair_next(&s.repair, &s.changes, &s.redraw);
        const gfx_surface_t back = {app_port_back_buffer(), W, H, W, app_port_flush};
        const int64_t t0 = app_port_time_us();
        gfx_scene_render(&s_scene, &s.redraw, &back);
        const int64_t render_us = app_port_time_us() - t0;
        if (s_cfg.gfx_verify) {
            verify_frame(&back, &s.perf);
        }
        app_port_present();
        presented = true;
        office_switch_presented(&s.screen);
        office_switch_presented(&s_mg.sw);

        const int area = gfx_dirty_area(&s.redraw);
        s.perf.renders++;
        s.perf.full_renders += area == W * H;
        s.perf.render_us_sum += render_us;
        s.perf.render_us_max = max64(s.perf.render_us_max, render_us);
        s.perf.px_sum += area;
        s.perf.px_max = area > s.perf.px_max ? area : s.perf.px_max;
        if (s.pending_touch_us) {
            const int64_t latency = app_port_time_us() - s.pending_touch_us;
            s.perf.touches++;
            s.perf.touch_us_sum += latency;
            s.perf.touch_us_max = max64(s.perf.touch_us_max, latency);
            s.pending_touch_us = 0;
        }
        // Bring both buffers up to date before the backlight comes on. The first frame was a full redraw and is in
        // the repair history, so any change makes the second buffer redraw fully too: request one now. Full
        // redraws starve the board's scanout of PSRAM bandwidth (a resync glitch); this keeps them in the dark.
        if (++s.presented == 1) {
            APP_LOGI(TAG, "first full redraw %.1f ms", render_us / 1000.0);
            gfx_scene_invalidate(&s_scene, (gfx_rect_t){0, 0, 1, 1});
        } else if (s.presented == 2) {
            backlight_on(&s.backlight); // retried by the loop if it fails
            app_scanout_stats_t boot;
            app_port_scanout_stats(&boot);
            APP_LOGI(TAG, "both buffers drawn, backlight on (while dark: %" PRIu32 " scanout resyncs, fill max %" PRIu32
                     " us)", boot.resyncs, boot.fill_us_max);
            strips_log("boot", &boot);
            memset(&s.perf, 0, sizeof(s.perf)); // performance windows cover the lit screen only
            s.perf_since = app_port_time_us();
        }
    }

    // The interaction (evt act): shown once a presented frame shows it, done once the brain has moved on.
    ia_update(&s.brain, presented, s_ia.bstate == BUDDY_STATE_ASK ? ask_left != 0 : !s.screen.office, now);
    // log.since: a few replayed events per frame, so a long replay never holds up the screen (the board's console
    // blocks while it sends).
    const bool replaying = app_events_replay_step();

    const int64_t t = app_port_time_us();
    if (s_cfg.perf_log_s > 0 && t - s.perf_since >= s_cfg.perf_log_s * 1000000LL) {
        perf_log(&s.perf, t - s.perf_since);
        memset(&s.perf, 0, sizeof(s.perf));
        s.perf_since = t;
    }

    // Sleep until the pose may change next, minus the time this call took.
    const uint32_t spent = now_ms() - now;
    uint32_t wait_ms = next > spent && s.presented != 1 ? next - spent : 0;
    wait_ms = demo_wait(wait_ms);
    if (replaying && wait_ms > 20) {
        wait_ms = 20;
    }
    return wait_ms > MAX_SLEEP_MS ? MAX_SLEEP_MS : wait_ms;
}
