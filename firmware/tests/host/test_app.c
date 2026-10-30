// The application (components/app) end to end, as the board and the simulator run it, on a fake platform: a clock
// the test moves, frame buffers in memory, and the lines it sends to the companion collected. Commands go in as
// protocol lines, touches as touch events. Covers what the companion relies on: evt act (shown, then exactly one
// outcome; no and timeout distinct; an interruption never an answer), act retries, status act=, evt did (MY DAY),
// evt mood src=, the recorded events' identity, time and replay (log.since), and the Claude usage strip (usage.set: in
// normal mode and Clock Mode, not during Missile Defense, its lease). One application for all tests in this file (it is
// a singleton, like on the board), run in order.
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "app_link.h"
#include "app_port.h"
#include "buddy_activity.h"
#include "buddy_kitt.h"
#include "buddy_view.h"
#include "layout.h"
#include "marquee.h"
#include "md.h"
#include "md_view.h"
#include "menu.h"
#include "menu_ask.h"
#include "menu_ttt.h"
#include "sysinfo_view.h"
#include "test.h"
#include "ttt.h"
#include "usage_view.h"

enum { W = 1024, H = 600, ALIGN = 32 };
enum { KEPT = 64 };                    // APP_EVENTS_KEPT
#define BOOT "abababababababab"        // the boot id from app_port_random_fill() below

// ---- the fake platform ----

static int64_t s_now_us;
static int64_t s_wall_us;
static char s_out[64 * 1024]; // what was sent to the companion since clear_out()
static size_t s_out_len;
static uint16_t s_fb[2][W * H];
static int s_back;

int64_t app_port_time_us(void) { return s_now_us; }
uint32_t app_port_random(void) { return 12345; }
void app_port_wall_get(int64_t *sec, uint32_t *usec)
{
    *sec = s_wall_us / 1000000;
    *usec = (uint32_t)(s_wall_us % 1000000);
}
void app_port_wall_set(int64_t sec, uint32_t usec) { s_wall_us = sec * 1000000 + usec; }
const char *app_port_version(void) { return "test"; }
const char *app_port_device_id(void) { return "test-dev"; }
const char *app_port_info(void) { return "build=0123456789abcdef0123456789abcdef01234567 board=test hw=ok"; }
void app_port_random_fill(void *buf, size_t len) { memset(buf, 0xab, len); }
void *app_port_alloc(size_t size, app_mem_t kind)
{
    (void)kind;
    return malloc(size);
}
gfx_color_t *app_port_back_buffer(void) { return s_fb[s_back]; }
void app_port_flush(const gfx_color_t *px, int count)
{
    (void)px;
    (void)count;
}
void app_port_present(void) { s_back ^= 1; }
bool app_port_backlight_on(void) { return true; }
void app_port_scanout_stats(app_scanout_stats_t *out) { memset(out, 0, sizeof(*out)); }
void app_port_log(char level, const char *tag, const char *fmt, ...)
{
    (void)level;
    (void)tag;
    (void)fmt;
}
void app_port_send(const char *line, size_t len)
{
    if (s_out_len + len < sizeof(s_out)) {
        memcpy(s_out + s_out_len, line, len);
        s_out_len += len;
        s_out[s_out_len] = '\0';
    }
}

static void clear_out(void)
{
    s_out_len = 0;
    s_out[0] = '\0';
}

static bool sent(const char *text) { return strstr(s_out, text) != NULL; }

static int count(const char *text)
{
    int n = 0;
    for (const char *p = strstr(s_out, text); p; p = strstr(p + 1, text)) {
        n++;
    }
    return n;
}

// The body of the first line sent that contains text (without "<DB1 n " and the checksum); "" if there is none.
static const char *line_with(const char *text)
{
    static char body[200];
    body[0] = '\0';
    const char *p = strstr(s_out, text);
    if (!p) {
        return body;
    }
    while (p > s_out && p[-1] != '\n') {
        p--;
    }
    p = strchr(strchr(p, ' ') + 1, ' ') + 1; // past "<DB1 " and the seq
    const char *end = strstr(p, " #");
    snprintf(body, sizeof(body), "%.*s", (int)(end - p), p);
    return body;
}

static uint32_t seq_in(const char *body)
{
    const char *p = strstr(body, " seq=");
    return p ? (uint32_t)strtoul(p + 5, NULL, 10) : 0;
}

// Runs the application loop for ms of fake time, as the board's render task does.
static void run(uint32_t ms)
{
    const int64_t end = s_now_us + (int64_t)ms * 1000;
    for (;;) {
        uint32_t wait = app_frame();
        wait = wait ? wait : 1;
        if (s_now_us + (int64_t)wait * 1000 >= end) {
            s_now_us = end;
            app_frame();
            return;
        }
        s_now_us += (int64_t)wait * 1000;
    }
}

static bool deliver(const app_event_t *ev, void *ctx)
{
    (void)ctx;
    app_event(ev); // straight to the application (on the board: through the queue to the render task)
    return true;
}

// A command line from the companion, with its checksum, through the device's own line handling.
static void command(uint32_t seq, const char *body)
{
    char line[300];
    const int n = snprintf(line, sizeof(line), ">DB1 %" PRIu32 " %s", seq, body);
    snprintf(line + n, sizeof(line) - (size_t)n, " #%04X\n", link_crc16(line, (size_t)n));
    static app_link_rx_t rx;
    app_link_feed(&rx, (const uint8_t *)line, strlen(line), deliver, NULL);
}

static void touch(int x, int y)
{
    app_event(&(app_event_t){.type = APP_TOUCH_DOWN, .x = (int16_t)x, .y = (int16_t)y, .at_us = s_now_us});
    run(40);
    app_event(&(app_event_t){.type = APP_TOUCH_UP, .x = (int16_t)x, .y = (int16_t)y, .at_us = s_now_us});
    run(40);
}

// Where things are on screen: computed the way the application places them (same layout, same functions).
static layout_t s_layout;
static menu_t s_where; // only for the menu's area
static int s_no_x, s_no_y, s_yes_x, s_yes_y;
static gfx_rect_t s_no, s_yes; // Buddy's NEE/JA buttons, while he asks

static void tap_slot(menu_layout_t layout, int slot)
{
    const gfx_rect_t r = menu_slot_rect(layout, slot);
    touch(s_where.area.x + (r.x + r.w / 2) * MENU_SCALE, s_where.area.y + (r.y + r.h / 2) * MENU_SCALE);
}

static void tap_back(void)
{
    const gfx_rect_t r = menu_back_rect();
    touch(s_where.area.x + (r.x + r.w / 2) * MENU_SCALE, s_where.area.y + (r.y + r.h / 2) * MENU_SCALE);
}

static bool s_started;

static bool start(void)
{
    if (s_started) {
        return true;
    }
    static uint16_t strip[16 * 1024];
    static gfx_scene_t scene;
    static buddy_view_t view;
    static menu_ask_t ask;
    s_layout = layout_compute(W, H, 65, false, ALIGN);
    if (!gfx_scene_init(&scene, W, H, layout_background, &s_layout, strip, 16 * 1024, ALIGN) ||
        !buddy_view_init(&view, &scene, s_layout.stage, 10, 0) ||
        !menu_ask_init(&ask, &scene, s_layout.stage, view.home_y0 - BUDDY_MAX_LIFT * view.home_scale, 25) ||
        !menu_init(&s_where, &menu_default, layout_panel_content(&s_layout), 0)) {
        return false;
    }
    menu_ask_apply(&ask, MENU_QUESTION_PEUKIE, BUDDY_ASK_MS, BUDDY_ASK_MS);
    const gfx_rect_t no = gfx_sprite_bounds(&ask.button[MENU_ANSWER_NO]);
    const gfx_rect_t yes = gfx_sprite_bounds(&ask.button[MENU_ANSWER_YES]);
    s_no_x = no.x + no.w / 2;
    s_no_y = no.y + no.h / 2;
    s_yes_x = yes.x + yes.w / 2;
    s_yes_y = yes.y + yes.h / 2;
    s_no = no;
    s_yes = yes;

    const app_config_t config = {.width = W, .height = H, .stage_percent = 65};
    s_now_us = 1000000;
    app_link_boot(); // as the board's link task and the simulator do first: the device and boot ids
    if (!app_init(&config)) {
        return false;
    }
    run(500); // both buffers drawn, the screen lit
    s_started = true;
    return true;
}

// ---- tests ----

void test_app_hello_offers_tracking_and_the_question(void)
{
    CHECK(start());
    clear_out();
    command(1, "hello");
    CHECK(sent("caps=poke,monster,mode,time,status,msg,mood,act,pdc,track,did,log,face,info,usage,sys "));
    CHECK(sent("acts=water,sneeze,stare,meditate,focus,ask_peukie,kitt faces=classic,skeleton dev="));
    clear_out();
    command(2, "status");
    CHECK(sent(" act=none"));
}

// info is answered by the link at once, like ping: the firmware version, then what the platform reports.
void test_app_info_reports_the_build_and_the_board(void)
{
    CHECK(start());
    clear_out();
    command(3, "info");
    CHECK(sent("<DB1 3 ok fw=test build=0123456789abcdef0123456789abcdef01234567 board=test hw=ok #"));
    CHECK_EQ(count("<DB1"), 1);
}

// hello is the longest reply, and a reply that does not fit is not sent at all (the companion could not connect).
// With the board's 12-digit device id and a version as long as "10.20.30-rc.10" it still fits.
void test_app_hello_fits_with_a_long_version(void)
{
    CHECK(start());
    clear_out();
    command(4, "hello");
    CHECK_EQ(count("\n"), 1);
    const size_t worst =
        strlen(s_out) - strlen("test") + strlen("10.20.30-rc.10") - strlen("test-dev") + strlen("441bf68d1178");
    CHECK(worst < LINK_REPLY_MAX);
}

// The scheduled question: started by `act` with the companion's id, shown, then exactly one outcome. NEE and no
// answer are different outcomes, although Buddy is sad either way.
void test_app_question_by_command_reports_yes_no_and_timeout(void)
{
    CHECK(start());
    clear_out();
    command(3, "act name=ask_peukie iid=42");
    CHECK(sent("<DB1 3 ok #"));
    CHECK(!sent("state=shown")); // not before a frame with the question has been presented
    run(100);
    CHECK_EQ(count("evt act iid=42 name=ask_peukie state=shown src=cmd"), 1);

    // A retry of the same request (its reply was lost): no second question.
    clear_out();
    command(4, "act name=ask_peukie iid=42");
    CHECK(sent("<DB1 4 ok dup=1 state=shown"));
    run(100);
    CHECK(!sent("evt act")); // no second question, so nothing new to report

    // NEE.
    touch(s_no_x, s_no_y);
    CHECK_EQ(count("evt act iid=42 state=done result=no"), 1);
    clear_out();
    command(5, "status");
    CHECK(sent(" act=42:done:no"));
    run(4000); // sad, then his mood again

    // JA: the smoke break, its start and end reported as such.
    clear_out();
    command(6, "act name=ask_peukie iid=43");
    run(100);
    CHECK(sent("evt act iid=43 name=ask_peukie state=shown src=cmd"));
    touch(s_yes_x, s_yes_y);
    CHECK_EQ(count("evt act iid=43 state=done result=yes"), 1);
    CHECK(sent("evt mood mood=peukie src=break"));
    run(BUDDY_SMOKE_BREAK_MS + 1000);
    CHECK(sent("evt mood mood=idle src=break"));

    // No answer within the minute: timeout, never "no".
    clear_out();
    command(7, "act name=ask_peukie iid=44");
    run(BUDDY_ASK_MS - 1000);
    CHECK(sent("evt act iid=44 name=ask_peukie state=shown"));
    CHECK(!sent("state=done"));
    run(2000);
    CHECK_EQ(count("evt act iid=44 state=done result=timeout"), 1);
    CHECK(!sent("result=no"));
    run(4000);
}

// Clock Mode cuts the question off: interrupted, even after more than a minute; and while the clock is up an action
// is refused (never started, no events).
void test_app_an_interrupted_question_is_never_an_answer(void)
{
    CHECK(start());
    clear_out();
    command(10, "act name=ask_peukie iid=45");
    run(100);
    CHECK(sent("evt act iid=45 name=ask_peukie state=shown"));
    command(11, "mode.set mode=office");
    run(3000);
    CHECK(sent("evt mode mode=office"));
    clear_out();
    command(12, "act name=sneeze iid=46");
    CHECK(sent("<DB1 12 err busy mode=office"));
    run(BUDDY_ASK_MS + 10000);
    CHECK(!sent("evt act")); // nothing decided while he is away, and the minute running out is not an answer
    command(13, "mode.set mode=normal");
    run(3000);
    CHECK_EQ(count("evt act iid=45 state=done result=interrupted"), 1);
    CHECK(!sent("result=timeout") && !sent("iid=46"));
    clear_out();
    command(14, "status");
    CHECK(sent(" act=45:done:interrupted"));
}

// A show by command plays to its end (done); one already playing refuses another; without an iid, and on the
// touchscreen, the device picks the id (top bit set).
void test_app_shows_and_touches_report_their_own_ids(void)
{
    CHECK(start());
    clear_out();
    command(20, "act name=sneeze iid=47");
    run(50);
    CHECK(sent("evt act iid=47 name=sneeze state=shown src=cmd"));
    command(21, "act name=stare iid=48");
    CHECK(sent("<DB1 21 err busy"));
    run(BUDDY_SNEEZE_MS + 500);
    CHECK_EQ(count("evt act iid=47 state=done result=done"), 1);
    CHECK(!sent("iid=48"));

    clear_out();
    command(22, "act name=juggle_cats iid=49");
    CHECK(sent("<DB1 22 err unsupported"));
    command(23, "act name=sneeze");
    run(50);
    char own[64];
    snprintf(own, sizeof(own), "evt act iid=%" PRIu32 " name=sneeze state=shown src=cmd", 0x80000000u | 12345u);
    CHECK(sent(own));
    run(BUDDY_SNEEZE_MS + 500);

    // On the touchscreen: Activities, SNEEZE (after SURPRISE!).
    clear_out();
    tap_slot(MENU_LIST, 1);
    run(600); // the tiles flip in
    tap_slot(MENU_GRID, 1);
    run(50);
    snprintf(own, sizeof(own), "evt act iid=%" PRIu32 " name=sneeze state=shown src=touch", 0x80000000u | 12346u);
    CHECK(sent(own));
    run(BUDDY_SNEEZE_MS + 500);
    CHECK_EQ(count("state=done result=done"), 1);
    tap_back();
    run(600);
}

// MY DAY reports what you are doing (counted, so a repeat is a new record, a duplicate is not); his shows and moods
// never do. Moods report why they changed.
void test_app_my_day_and_mood_sources(void)
{
    CHECK(start());
    clear_out();
    tap_slot(MENU_LIST, 3); // MY DAY
    run(600);
    tap_slot(MENU_GRID, 2); // COFFEE
    run(100);
    const uint32_t coffee = seq_in(line_with("evt did item=myday.coffee seq="));
    CHECK(coffee > 0);
    run(1000);
    tap_slot(MENU_GRID, 1); // MEETING
    run(100);
    CHECK_EQ(seq_in(line_with("evt did item=myday.meeting seq=")), coffee + 1);
    CHECK_EQ(count("evt did"), 2);
    CHECK(!sent("evt act")); // he cheers; it is not one of his shows
    tap_back();
    run(600);

    clear_out();
    command(30, "mood.set name=happy");
    run(50);
    CHECK(sent("evt mood mood=happy src=cmd"));
    command(31, "mood.set name=happy"); // no change: no event, and it explains nothing later
    command(32, "mood.set name=sleepy secs=1");
    run(1500);
    CHECK(sent("evt mood mood=sleepy src=cmd"));
    CHECK(sent("evt mood mood=idle src=timer"));
    CHECK(!sent("evt did"));
}

// Every recorded event can be told apart from any other: hello names the device and this boot, each event has its
// seq; and its time is UTC only once the companion has set the clock, never made up from the uptime.
void test_app_events_carry_identity_and_time(void)
{
    CHECK(start());
    clear_out();
    command(40, "hello");
    CHECK(sent(" dev=test-dev boot=" BOOT " #"));

    clear_out();
    command(41, "act name=sneeze iid=901");
    run(4000);
    const uint32_t shown = seq_in(line_with("evt act iid=901 name=sneeze state=shown"));
    CHECK(shown > 0);
    CHECK(strstr(line_with("evt act iid=901 name=sneeze state=shown"), " up=") != NULL);
    CHECK(!sent(" t="));
    CHECK_EQ(seq_in(line_with("evt act iid=901 state=done")), shown + 1);

    command(42, "time.set utc=1792000000 ms=250 off=120");
    clear_out();
    command(43, "act name=sneeze iid=902");
    run(4000);
    CHECK(strstr(line_with("evt act iid=902 name=sneeze state=shown"), " t=1792000000250") != NULL);
    CHECK(!sent(" up="));
    CHECK_EQ(seq_in(line_with("evt act iid=902 name=sneeze state=shown")), shown + 2);
}

// log.since: the events after the companion's last stored one, the same lines again (replay=1), then the reply. A
// second replay repeats them exactly (so storing them twice is harmless); another boot gets all of this one.
void test_app_events_replay_after_a_reconnect(void)
{
    CHECK(start());
    clear_out();
    command(50, "act name=sneeze iid=903");
    run(4000);
    char done[200];
    snprintf(done, sizeof(done), "%s", line_with("evt act iid=903 state=done"));
    const uint32_t last = seq_in(done);
    CHECK(last > 3 && last < KEPT); // all of this boot is still kept

    for (uint32_t round = 0; round < 2; round++) {
        clear_out();
        char line[80], text[240];
        snprintf(line, sizeof(line), "log.since boot=" BOOT " seq=%" PRIu32, last - 3);
        command(51 + round, line);
        CHECK(!sent("ok boot=")); // the reply comes after the events
        run(200);
        CHECK_EQ(count(" replay=1"), 3);
        snprintf(text, sizeof(text), "%s replay=1 #", done);
        const char *again = strstr(s_out, text);
        CHECK(again != NULL);
        snprintf(text, sizeof(text), " seq=%" PRIu32 " ", last - 3);
        CHECK(!sent(text)); // the companion has that one
        snprintf(text, sizeof(text), "<DB1 %" PRIu32 " ok boot=" BOOT " last=%" PRIu32 " lost=0 #", 51 + round, last);
        const char *reply = strstr(s_out, text);
        CHECK(reply != NULL && again != NULL && reply > again);
    }

    clear_out();
    command(53, "log.since seq=0");
    command(54, "log.since seq=0"); // one replay at a time
    CHECK(sent("<DB1 54 err busy"));
    run(2000);
    CHECK(sent("<DB1 53 ok boot=" BOOT));

    clear_out();
    command(55, "log.since boot=00000000000000ff seq=2"); // a boot before the last restart
    run(2000);
    CHECK_EQ(count(" replay=1"), (int)last);
    char reply[80];
    snprintf(reply, sizeof(reply), "<DB1 55 ok boot=" BOOT " last=%" PRIu32 " lost=0 #", last);
    CHECK(sent(reply));
}

// More events than are kept while the companion was away: the newest KEPT come back, and the reply says how many of
// the ones it asked for are gone.
void test_app_events_replay_reports_what_was_lost(void)
{
    CHECK(start());
    clear_out();
    tap_slot(MENU_LIST, 3); // MY DAY
    run(600);
    tap_slot(MENU_GRID, 2); // COFFEE: the companion stores this one, then goes away
    run(300);
    const uint32_t stored = seq_in(line_with("evt did item=myday.coffee"));
    for (int i = 0; i < KEPT + 9; i++) {
        tap_slot(MENU_GRID, 2);
        run(300);
    }
    CHECK_EQ(count("evt did item=myday.coffee"), KEPT + 10);
    const uint32_t last = stored + KEPT + 9;
    tap_back();
    run(600);

    clear_out();
    char line[80], text[80];
    snprintf(line, sizeof(line), "log.since boot=" BOOT " seq=%" PRIu32, stored);
    command(60, line);
    run(3000);
    CHECK_EQ(count(" replay=1"), KEPT);
    snprintf(text, sizeof(text), " seq=%" PRIu32 " ", last - KEPT + 1); // the oldest kept
    CHECK(sent(text));
    snprintf(text, sizeof(text), " seq=%" PRIu32 " ", last - KEPT);
    CHECK(!sent(text));
    snprintf(text, sizeof(text), "<DB1 60 ok boot=" BOOT " last=%" PRIu32 " lost=9 #", last);
    CHECK(sent(text));
}

// Pixels of the last presented frame that are not the clock's plain background.
static int lit_pixels(void)
{
    const uint16_t *front = s_fb[s_back ^ 1];
    int n = 0;
    for (int i = 0; i < W * H; i++) {
        n += front[i] != GFX_RGB(8, 9, 12);
    }
    return n;
}

// The header's Buddy head: held, the CLOCK FACES page; tapped, Clock Mode.
static void head_point(int *x, int *y)
{
    const gfx_rect_t r = menu_head_rect();
    *x = s_where.area.x + (r.x + r.w / 2) * MENU_SCALE;
    *y = s_where.area.y + (r.y + r.h / 2) * MENU_SCALE;
}

static void hold_head(uint32_t ms)
{
    int x, y;
    head_point(&x, &y);
    app_event(&(app_event_t){.type = APP_TOUCH_DOWN, .x = (int16_t)x, .y = (int16_t)y, .at_us = s_now_us});
    run(ms); // the frames notice the long press (app_frame polls menu_hold)
    app_event(&(app_event_t){.type = APP_TOUCH_UP, .x = (int16_t)x, .y = (int16_t)y, .at_us = s_now_us});
    run(40);
}

// Clock faces: chosen by command or on the CLOCK FACES page (a long press on the header's Buddy head), reported once
// per change (evt face, status face=); a tap on the head enters the selected face, a face chosen meanwhile shows at
// once, a tap still leaves Clock Mode, and his mood (changed meanwhile) carries on afterwards.
void test_app_clock_faces_by_touch_and_command(void)
{
    CHECK(start());
    clear_out();
    command(70, "status");
    CHECK(sent(" face=classic #"));
    command(71, "clock.face name=skeleton");
    CHECK(sent("<DB1 71 ok #"));
    CHECK_EQ(count("evt face name=skeleton src=cmd"), 1);
    command(72, "clock.face name=skeleton"); // already: ok, nothing to report
    command(73, "clock.face name=nixie");
    command(74, "clock.face name=Nixie!");
    CHECK(sent("<DB1 72 ok #"));
    CHECK_EQ(count("evt face"), 1);
    CHECK(sent("<DB1 73 err unsupported #"));
    CHECK(sent("<DB1 74 err bad_args #"));

    // From Activities: held on his head, CLOCK FACES (no Clock Mode); CLASSIC, then SKELETON again.
    clear_out();
    tap_slot(MENU_LIST, 1);
    run(600);
    hold_head(MENU_HOLD_MS + 100);
    run(600);
    CHECK(!sent("evt mode"));
    tap_slot(MENU_GRID, 0);
    run(100);
    CHECK_EQ(count("evt face name=classic src=touch"), 1);
    tap_slot(MENU_GRID, 1);
    run(100);
    CHECK_EQ(count("evt face name=skeleton src=touch"), 1);
    command(75, "status");
    CHECK(sent(" face=skeleton #"));

    // Back goes Home; a tap on his head zooms into the Skeleton.
    tap_back();
    run(600);
    clear_out();
    int hx, hy;
    head_point(&hx, &hy);
    touch(hx, hy);
    run(4000);
    CHECK(sent("evt mode mode=office"));
    const int skeleton = lit_pixels(); // the movement, the timeplate
    CHECK(skeleton > W * H / 5);
    command(76, "mood.set name=happy");

    // Classic chosen while the clock shows: it changes at once (both buffers redrawn), and back.
    command(77, "clock.face name=classic");
    run(300);
    const int classic = lit_pixels(); // the digits and the date only
    CHECK(classic > 0 && classic * 3 < skeleton);
    command(78, "clock.face name=skeleton");
    run(300);
    CHECK(lit_pixels() > W * H / 5);

    // A tap anywhere: Buddy again, in the mood chosen meanwhile.
    clear_out();
    touch(W / 2, H / 2);
    run(3000);
    CHECK(sent("evt mode mode=normal"));
    command(79, "status");
    CHECK(sent(" mode=normal mood=happy "));
    CHECK(sent(" face=skeleton #"));
    command(80, "mood.set name=idle");
    run(600);
}

// KITT, from the Moods page's eighth button: he stays KITT (busy for other shows) until a tap on him turns him back
// into his mood (done), a mood choice turns him back into that one (done), or Clock Mode cuts it (interrupted).
void test_app_kitt_until_a_tap_a_mood_or_the_clock(void)
{
    CHECK(start());
    command(90, "mood.set name=working");
    run(100);
    clear_out();
    tap_slot(MENU_LIST, 0); // MOODS
    run(600);
    tap_slot(MENU_GRID, 7); // KITT
    run(100);
    CHECK(sent(" name=kitt state=shown src=touch"));
    run(BUDDY_KITT_IN_MS + 60000); // a minute of sweeping: nothing ends it
    CHECK(!sent("state=done"));
    command(91, "act name=sneeze iid=91");
    CHECK(sent("<DB1 91 err busy"));
    command(92, "status");
    CHECK(sent(" mood=working "));

    const int bx = s_layout.stage.x + s_layout.stage.w / 2, by = s_layout.stage.y + s_layout.stage.h / 2;
    touch(bx, by); // on him
    run(BUDDY_KITT_OUT_MS + 200);
    CHECK_EQ(count("state=done result=done"), 1);
    command(93, "status");
    CHECK(sent(" mode=normal mood=working "));

    clear_out();
    command(94, "act name=kitt iid=94");
    run(BUDDY_KITT_IN_MS + 1000);
    command(95, "mood.set name=happy");
    run(BUDDY_KITT_OUT_MS + 200);
    CHECK(sent("evt mood mood=happy src=cmd"));
    CHECK_EQ(count("evt act iid=94 state=done result=done"), 1);

    clear_out();
    command(96, "act name=kitt iid=96");
    run(BUDDY_KITT_IN_MS + 1000);
    command(97, "mode.set mode=clock");
    run(4000);
    CHECK(sent("evt mode mode=office"));
    command(98, "mode.set mode=normal");
    run(4000);
    CHECK_EQ(count("evt act iid=96 state=done result=interrupted"), 1);
    command(99, "status");
    CHECK(sent(" mode=normal mood=happy "));
    command(100, "mood.set name=idle");
    tap_back(); // Home, for whatever runs next
    run(600);
}

// ---- Tic-Tac-Toe, played on the touchscreen as you would: everything read back from the presented frames ----

static void tap_cell(int cell)
{
    const gfx_rect_t r = cell < TTT_CELLS ? menu_ttt_cell_rect(cell) : menu_ttt_strip_rect();
    touch(s_where.area.x + (r.x + r.w / 2) * MENU_SCALE, s_where.area.y + (r.y + r.h / 2) * MENU_SCALE);
}

// The mark in a cell of the last presented frame, from its colours: your X amber, his O cyan (white while his
// line is lit).
static int cell_mark(int cell)
{
    const gfx_rect_t r = menu_ttt_cell_rect(cell);
    const uint16_t *front = s_fb[s_back ^ 1];
    const gfx_color_t amber = GFX_RGB(255, 170, 40), hot = GFX_RGB(255, 232, 160), cyan = GFX_RGB(96, 232, 255);
    const gfx_color_t white = GFX_RGB(236, 242, 255);
    int x_px = 0, o_px = 0;
    // Inside the rim only (a lit rim is coloured too).
    for (int y = (r.y + 6) * MENU_SCALE; y < (r.y + r.h - 6) * MENU_SCALE; y++) {
        for (int x = (r.x + 6) * MENU_SCALE; x < (r.x + r.w - 6) * MENU_SCALE; x++) {
            const gfx_color_t c = front[(s_where.area.y + y) * W + s_where.area.x + x];
            x_px += c == amber || c == hot;
            o_px += c == cyan || c == white;
        }
    }
    return x_px > 100 ? TTT_X : o_px > 100 ? TTT_O : TTT_EMPTY;
}

static void read_board(uint8_t board[TTT_CELLS])
{
    for (int i = 0; i < TTT_CELLS; i++) {
        board[i] = (uint8_t)cell_mark(i);
    }
}

static int marks(const uint8_t board[TTT_CELLS], int mark)
{
    int n = 0;
    for (int i = 0; i < TTT_CELLS; i++) {
        n += board[i] == mark;
    }
    return n;
}

static void open_game(void)
{
    tap_slot(MENU_LIST, 4); // GAMES (no PDC offered: the fifth row)
    run(600);
    tap_slot(MENU_GRID, 0); // TIC-TAC-TOE
    run(700);
}

// Its tile opens the game (mode=game, busy for shows), the X goes where you tap and only on your turn, Buddy answers
// after his pause, a round ends with the rematch button, which starts the next (he starts it). Leaving it restores
// his mood, also one chosen meanwhile; Clock Mode and boredom end it too, and so does holding his head for the faces.
void test_app_tic_tac_toe_on_the_touchscreen(void)
{
    CHECK(start());
    command(200, "mood.set name=happy");
    run(100);
    clear_out();
    open_game();
    CHECK(sent("evt mode mode=game"));
    command(201, "status");
    CHECK(sent(" mode=game mood=happy "));
    command(202, "act name=water iid=202");
    command(203, "poke");
    CHECK(sent("<DB1 202 err busy mode=game #"));
    CHECK(sent("<DB1 203 err busy mode=game #"));
    CHECK(!sent("evt act"));

    // You start the first round after boot: an empty board, your X where you tap, then his O.
    uint8_t board[TTT_CELLS];
    read_board(board);
    CHECK_EQ(marks(board, TTT_X) + marks(board, TTT_O), 0);
    tap_cell(4);
    run(300);
    read_board(board);
    CHECK_EQ(board[4], TTT_X);
    CHECK_EQ(marks(board, TTT_O), 0); // he thinks first...
    tap_cell(0);                      // ...and a tap meanwhile does nothing (he protests)
    run(TTT_THINK_MAX_MS + 300);
    read_board(board);
    CHECK_EQ(marks(board, TTT_X), 1);
    CHECK_EQ(marks(board, TTT_O), 1); // ...then answers
    command(204, "mood.set name=relaxed"); // taken, shown after the game
    command(205, "status");
    CHECK(sent(" mode=game mood=relaxed "));

    // Play on (the first free cell each time) until the round is decided.
    for (int move = 0; move < 5 && ttt_judge(board, NULL) == TTT_OPEN; move++) {
        int free_cell = 0;
        while (board[free_cell] != TTT_EMPTY) {
            free_cell++;
        }
        const int before = marks(board, TTT_X);
        tap_cell(free_cell);
        run(TTT_THINK_MAX_MS + 300);
        read_board(board);
        CHECK_EQ(marks(board, TTT_X), before + 1);
        CHECK(ttt_judge(board, NULL) != TTT_OPEN || marks(board, TTT_O) == marks(board, TTT_X)); // he answered
    }
    CHECK(ttt_judge(board, NULL) != TTT_OPEN);
    tap_cell(TTT_CELLS); // the button is not there during his reaction
    run(TTT_REACTION_MS);
    read_board(board);
    CHECK(ttt_judge(board, NULL) != TTT_OPEN); // the board stays while he waits for the rematch
    tap_cell(TTT_CELLS);                       // REMATCH / AGAIN: a fresh board, and he starts this one
    run(300);
    read_board(board);
    CHECK_EQ(marks(board, TTT_X) + marks(board, TTT_O), 0);
    run(TTT_OPENING_MS + TTT_THINK_MAX_MS);
    read_board(board);
    CHECK_EQ(marks(board, TTT_X), 0);
    CHECK_EQ(marks(board, TTT_O), 1);

    // Back: the game is over, his mood (chosen meanwhile) carries on, the GAMES page shows.
    clear_out();
    tap_back();
    run(600);
    CHECK(sent("evt mode mode=normal"));
    command(206, "status");
    CHECK(sent(" mode=normal mood=relaxed "));
    command(207, "poke");
    CHECK(sent("<DB1 207 ok #"));
    run(2000);

    // Clock Mode from the game (a tap on his head): the round pauses (he is himself in the clock) and carries on
    // after it (test_app_tic_tac_toe_pauses_in_clock_mode has the details); Back then leaves for the GAMES page.
    tap_slot(MENU_GRID, 0);
    run(700);
    clear_out();
    int hx, hy;
    head_point(&hx, &hy);
    touch(hx, hy);
    run(4000);
    CHECK(sent("evt mode mode=normal")); // the game paused first...
    CHECK(sent("evt mode mode=office")); // ...then the clock
    touch(W / 2, H / 2);
    run(3000);
    command(208, "status");
    CHECK(sent(" mode=game mood=relaxed "));
    tap_back();
    run(600);
    clear_out();
    tap_slot(MENU_GRID, 0); // on GAMES: its tile starts a new game
    run(700);
    CHECK(sent("evt mode mode=game"));

    // Holding his head opens CLOCK FACES, which ends the game as well.
    clear_out();
    hold_head(MENU_HOLD_MS + 100);
    run(600);
    CHECK(sent("evt mode mode=normal"));
    tap_back(); // Home
    run(600);

    // Nobody plays: after two minutes he gets bored and the game ends by itself.
    open_game();
    clear_out();
    run(TTT_IDLE_MS - 1000);
    CHECK(!sent("evt mode mode=normal"));
    run(2000);
    CHECK(sent("evt mode mode=normal"));
    command(209, "mood.set name=idle");
    tap_back(); // Home, for whatever runs next
    run(600);
}

// Plays one X on the first free cell and lets him answer.
static void play_one(uint8_t board[TTT_CELLS])
{
    int c = 0;
    while (board[c] != TTT_EMPTY) {
        c++;
    }
    tap_cell(c);
    run(TTT_THINK_MAX_MS + 300);
    read_board(board);
}

// Clock Mode pauses the round: meanwhile he is himself (mode=office, not game; his mood shows), back from the clock
// the same board carries on where it was, and the time in the clock counts for nothing (no boredom). Back quits the
// round; a round left in the clock for ten minutes is dropped and the game page gives way to GAMES.
void test_app_tic_tac_toe_pauses_in_clock_mode(void)
{
    CHECK(start());
    command(220, "mood.set name=happy");
    run(100);
    open_game();
    run(TTT_OPENING_MS + TTT_THINK_MAX_MS + 300); // if he starts, he has moved
    uint8_t board[TTT_CELLS];
    read_board(board);
    play_one(board);
    const int xs = marks(board, TTT_X), os = marks(board, TTT_O);
    CHECK(xs >= 1 && (os == xs || os == xs + 1)); // your turn again (whoever started)

    // Into the clock from the game: paused, not playing.
    clear_out();
    int hx, hy;
    head_point(&hx, &hy);
    touch(hx, hy);
    run(4000);
    CHECK(sent("evt mode mode=office"));
    command(221, "status");
    CHECK(sent(" mode=office mood=happy "));
    command(222, "act name=sneeze iid=222");
    CHECK(sent("<DB1 222 err busy mode=office #")); // Clock Mode's own rule, not the game's
    run(60000);

    // Back: the same round, the same board, his turn handling intact; a minute in the clock is not idle time.
    clear_out();
    touch(W / 2, H / 2);
    run(3000);
    CHECK(sent("evt mode mode=game"));
    uint8_t again[TTT_CELLS];
    read_board(again);
    CHECK(memcmp(board, again, sizeof(board)) == 0);
    run(TTT_IDLE_MS - 50000); // 70 s more without a touch: counted from before the clock, still not bored
    command(223, "status");
    CHECK(sent(" mode=game mood=happy "));
    if (ttt_judge(again, NULL) == TTT_OPEN) {
        play_one(again);
        CHECK_EQ(marks(again, TTT_X), xs + 1); // you play on...
        CHECK(ttt_judge(again, NULL) != TTT_OPEN || marks(again, TTT_O) == os + 1); // ...and he answers
    }

    // Back quits the round: the game opens on a fresh board.
    clear_out();
    tap_back();
    run(600);
    CHECK(sent("evt mode mode=normal"));
    tap_slot(MENU_GRID, 0);
    run(300);
    read_board(board);
    CHECK_EQ(marks(board, TTT_X), 0);
    CHECK(marks(board, TTT_O) <= 1);

    // Ten minutes in the clock: the paused round is dropped; back, the GAMES page, and its tile starts afresh.
    run(TTT_OPENING_MS + TTT_THINK_MAX_MS + 300);
    read_board(board);
    play_one(board);
    touch(hx, hy);
    run(4000);
    run(TTT_PAUSE_MS);
    clear_out();
    touch(W / 2, H / 2);
    run(3000);
    CHECK(sent("evt mode mode=normal"));
    CHECK(!sent("evt mode mode=game"));
    command(224, "status");
    CHECK(sent(" mode=normal mood=happy "));
    tap_slot(MENU_GRID, 0); // GAMES: Tic-Tac-Toe
    run(300);
    CHECK(sent("evt mode mode=game"));
    read_board(board);
    CHECK_EQ(marks(board, TTT_X), 0);
    command(225, "mood.set name=idle");
    tap_back();
    run(600);
    tap_back(); // Home, for whatever runs next
    run(600);
}

// ---- Missile Defense ----

static uint16_t front_px(int x, int y)
{
    return s_fb[s_back ^ 1][y * W + x];
}

static bool game_on_screen(void)
{
    return front_px(300, 2) == GFX_RGB(14, 16, 28); // the game's score bar, where the stage is otherwise
}

static void open_missiles(void)
{
    tap_slot(MENU_LIST, 4); // GAMES (no PDC offered: the fifth row)
    run(600);
    tap_slot(MENU_GRID, 1); // MISSILES
}

// Its tile: Buddy's alert on his stage (busy, mode=game), then the game on the whole screen; shots on the touchscreen;
// shows refused and messages kept waiting while it is on; the X brings back his stage, the menu on GAMES and his mood,
// and the waiting message goes up. Clock Mode ends the game first and then zooms in. A game left alone ends with the
// desk gone; RETRY starts afresh; and nobody touching the game-over screen closes it.
void test_app_missile_defense_fullscreen_and_back(void)
{
    CHECK(start());
    command(300, "mood.set name=happy");
    run(100);
    clear_out();
    open_missiles();
    CHECK(sent("evt mode mode=game"));
    run(300);
    CHECK(!game_on_screen()); // his alert, still on his stage
    run(900);
    CHECK(game_on_screen());
    command(301, "status");
    CHECK(sent(" mode=game mood=happy "));
    command(302, "act name=sneeze iid=302");
    CHECK(sent("<DB1 302 err busy mode=game #"));
    command(303, "msg.show id=303 text=HELLO");
    CHECK(sent("<DB1 303 ok queued=1 #"));
    run(3000);
    CHECK(!sent("evt msg id=303 state=showing"));
    touch(300, 200); // a shot (as the finger comes down)
    touch(700, 150);
    run(500);
    CHECK(game_on_screen());

    // The X: back to his stage, the menu on GAMES, his mood; then the message.
    clear_out();
    touch(MD_W - 34, 20);
    run(800);
    CHECK(!game_on_screen());
    CHECK(sent("evt mode mode=normal"));
    CHECK(sent("evt msg id=303 state=showing"));
    command(304, "status");
    CHECK(sent(" mode=normal mood=happy "));
    run(15000); // the message runs its course

    // Clock Mode during the game: the game ends, then he zooms into the clock; a tap brings him back.
    clear_out();
    tap_slot(MENU_GRID, 1);
    run(1500);
    CHECK(game_on_screen());
    command(305, "mode.set mode=clock");
    CHECK(sent("<DB1 305 ok #"));
    run(4000);
    CHECK(sent("evt mode mode=normal"));
    CHECK(sent("evt mode mode=office"));
    command(306, "status");
    CHECK(sent(" mode=office mood=happy "));
    touch(W / 2, H / 2);
    run(3000);
    command(307, "status");
    CHECK(sent(" mode=normal mood=happy "));

    // Left alone, the missiles take the desk and the game is over; RETRY starts a new one; left alone again, the
    // game-over screen closes by itself.
    clear_out();
    tap_slot(MENU_GRID, 1);
    run(1500);
    CHECK(game_on_screen());
    run(150000); // over after about 70 s; its buttons from 1.5 s later
    CHECK(game_on_screen()); // the game-over screen
    CHECK(front_px(69, 507) != GFX_RGB(204, 210, 220)); // the mug is rubble
    touch(MD_W / 2 - 112, 240); // RETRY
    run(1000);
    CHECK(game_on_screen());
    CHECK(front_px(69, 507) == GFX_RGB(204, 210, 220)); // a new game: the mug stands again
    command(308, "status");
    CHECK(sent(" mode=game "));
    run(240000 + MD_OVER_IDLE_MS);
    CHECK(!game_on_screen());
    command(309, "status");
    CHECK(sent(" mode=normal mood=happy "));
    command(310, "mood.set name=idle");
    tap_back(); // Home, for whatever runs next
    run(600);
}

// ---- Claude usage ----

// Pixels of a strip's plate in a of the last presented frame, outside skip (its colour is nowhere else on the stage or
// the clock; Buddy's buttons and the sign are skipped where they may cover it).
static int plate_px(gfx_rect_t a, const gfx_rect_t *skip, int n_skip)
{
    int n = 0;
    for (int y = a.y; y < a.y + a.h; y++) {
        for (int x = a.x; x < a.x + a.w; x++) {
            bool skipped = false;
            for (int i = 0; i < n_skip; i++) {
                skipped = skipped || gfx_rect_contains_point(skip[i], x, y);
            }
            n += !skipped && front_px(x, y) == GFX_RGB(20, 23, 36);
        }
    }
    return n;
}

static int usage_plate_px(void)
{
    return plate_px(usage_view_area(s_layout.stage), NULL, 0);
}

#define USAGE_ON "usage.set state=on five=37 five_end=1792009000 five_pred=130 week=20 week_end=1792266400 week_pred=90"

// The strip: offered in hello's caps; shown while the lease holds, at the bottom of the stage, the same in Clock Mode;
// its countdowns only while the clock is set; not during Missile Defense (from his alert on); gone 30 s after the last
// usage.set, at once on a hello or state=off. Malformed lines get err bad_args.
void test_app_usage_strip_follows_the_lease_and_the_mode(void)
{
    CHECK(start());
    clear_out();
    command(400, "hello");
    CHECK(sent(",info,usage,sys "));
    run(100);
    CHECK_EQ(usage_plate_px(), 0);

    s_wall_us = 0; // the clock not set: no countdowns
    command(401, USAGE_ON);
    command(402, "usage.set state=on five=37");
    command(403, "usage.set state=on five=1000 five_end=1792009000");
    CHECK(sent("<DB1 401 ok #"));
    CHECK(sent("<DB1 402 err bad_args #"));
    CHECK(sent("<DB1 403 err bad_args #"));
    run(100);
    const int unset = usage_plate_px();
    CHECK(unset > 0);
    s_wall_us = 1792000000LL * 1000000; // set: "2:30" and "3d 2u" join in (the plate grows)
    run(100);
    const int shown = usage_plate_px();
    CHECK(shown > unset);

    // Clock Mode: the same strip in the same place; and back.
    clear_out();
    command(404, "mode.set mode=office");
    run(4000);
    CHECK(sent("evt mode mode=office"));
    CHECK_EQ(usage_plate_px(), shown);
    touch(W / 2, H / 2);
    run(3000);
    CHECK(sent("evt mode mode=normal"));
    CHECK_EQ(usage_plate_px(), shown);

    // The lease: renewed every 10 s it holds; 30 s without, the strip goes.
    for (int i = 0; i < 4; i++) {
        command(405, USAGE_ON);
        run(10000);
        CHECK_EQ(usage_plate_px(), shown);
    }
    run(20500); // 30.5 s since the last one
    CHECK_EQ(usage_plate_px(), 0);
    command(406, USAGE_ON);
    run(100);
    CHECK_EQ(usage_plate_px(), shown);
    command(407, "hello"); // a new connection: gone at once
    run(100);
    CHECK_EQ(usage_plate_px(), 0);
    command(408, USAGE_ON);
    run(100);
    CHECK_EQ(usage_plate_px(), shown);
    command(409, "usage.set state=off");
    run(100);
    CHECK_EQ(usage_plate_px(), 0);

    // Missile Defense: hidden from his alert on, back with his stage.
    command(410, USAGE_ON);
    run(100);
    CHECK_EQ(usage_plate_px(), shown);
    open_missiles();
    run(300);
    CHECK(!game_on_screen()); // his alert, on his stage
    CHECK_EQ(usage_plate_px(), 0);
    run(1200);
    CHECK(game_on_screen());
    CHECK_EQ(usage_plate_px(), 0);
    command(411, USAGE_ON);
    run(1000);
    CHECK_EQ(usage_plate_px(), 0);
    touch(MD_W - 34, 20); // the X
    run(800);
    CHECK(!game_on_screen());
    CHECK_EQ(usage_plate_px(), shown);
    command(412, "usage.set state=off");
    tap_back(); // Home, for whatever runs next
    run(600);
    CHECK_EQ(usage_plate_px(), 0);
}

// ---- The PC's load ----

#define SYS_ON "sys.set state=on cpu=12 ram=45 disk=85 disk_free=120"

static int sys_plate_px(bool raised)
{
    return plate_px(sysinfo_view_area(s_layout.stage, raised), NULL, 0);
}

// ...raised, beside what covers it (whose own colours may include the plate's).
static int sys_plate_px_beside(gfx_rect_t a, gfx_rect_t b)
{
    const gfx_rect_t skip[] = {a, b};
    return plate_px(sysinfo_view_area(s_layout.stage, true), skip, 2);
}

// The strip: offered in hello's caps; in the usage strip's place while that one is hidden, directly above it while it
// shows. Raised, it gives way to Buddy's NEE/JA buttons and to a message on the sign in Clock Mode, and comes back
// after them. Gone 30 s after the last sys.set, at once on a hello or state=off; not during Missile Defense. Malformed
// lines get err bad_args.
void test_app_sys_strip_stacks_with_usage(void)
{
    CHECK(start());
    clear_out();
    command(500, "hello");
    CHECK(sent(",usage,sys "));
    run(100);
    CHECK_EQ(sys_plate_px(false) + sys_plate_px(true), 0);

    command(501, SYS_ON);
    command(502, "sys.set state=on cpu=101");
    command(503, "sys.set state=on disk_free=120");
    CHECK(sent("<DB1 501 ok #"));
    CHECK(sent("<DB1 502 err bad_args #"));
    CHECK(sent("<DB1 503 err bad_args #"));
    run(100);
    const int alone = sys_plate_px(false);
    CHECK(alone > 0);
    CHECK_EQ(sys_plate_px(true), 0);

    // With Claude usage: that one at the bottom, this one above it, the same plate.
    command(504, USAGE_ON);
    run(100);
    CHECK(usage_plate_px() > 0 && usage_plate_px() != alone);
    CHECK_EQ(sys_plate_px(true), alone);

    // Buddy's question: gone while his buttons are up; back once he is answered.
    command(505, "act name=ask_peukie");
    run(100);
    CHECK_EQ(sys_plate_px_beside(s_no, s_yes), 0);
    CHECK(usage_plate_px() > 0);
    touch(s_no_x, s_no_y);
    run(500);
    CHECK_EQ(sys_plate_px(true), alone);

    // Clock Mode: both as on the stage; a message on the sign takes the raised strip's place while it shows.
    clear_out();
    command(506, "mode.set mode=office");
    run(4000);
    CHECK(sent("evt mode mode=office"));
    CHECK_EQ(sys_plate_px(true), alone);
    command(507, "msg.show id=507 text=HI");
    run(500);
    CHECK(sent("evt msg id=507 state=showing"));
    const gfx_rect_t sign = {(W - MARQUEE_W) / 2, H - MARQUEE_H - 36, MARQUEE_W, MARQUEE_H};
    CHECK_EQ(sys_plate_px_beside(sign, sign), 0);
    for (int i = 0; i < 30 && !sent("evt msg id=507 state=done"); i++) {
        command(508, SYS_ON);
        command(509, USAGE_ON);
        run(1000);
    }
    CHECK(sent("evt msg id=507 state=done"));
    run(100);
    CHECK_EQ(sys_plate_px(true), alone);
    touch(W / 2, H / 2);
    run(3000);
    CHECK(sent("evt mode mode=normal"));

    // Claude usage off: down in its place.
    command(510, "usage.set state=off");
    run(100);
    CHECK_EQ(sys_plate_px(false), alone);
    CHECK_EQ(sys_plate_px(true), 0);

    // The lease: 30 s without a sys.set, a hello, state=off.
    command(511, SYS_ON);
    run(29000);
    CHECK_EQ(sys_plate_px(false), alone);
    run(1500);
    CHECK_EQ(sys_plate_px(false), 0);
    command(512, SYS_ON);
    run(100);
    CHECK_EQ(sys_plate_px(false), alone);
    command(513, "hello");
    run(100);
    CHECK_EQ(sys_plate_px(false), 0);
    command(514, SYS_ON);
    run(100);
    CHECK_EQ(sys_plate_px(false), alone);
    command(515, "sys.set state=off");
    run(100);
    CHECK_EQ(sys_plate_px(false), 0);

    // Missile Defense: hidden from his alert on, back with his stage.
    command(516, SYS_ON);
    run(100);
    CHECK_EQ(sys_plate_px(false), alone);
    open_missiles();
    run(300);
    CHECK(!game_on_screen()); // his alert, on his stage
    CHECK_EQ(sys_plate_px(false), 0);
    run(1200);
    CHECK(game_on_screen());
    command(517, SYS_ON);
    touch(MD_W - 34, 20); // the X
    run(800);
    CHECK(!game_on_screen());
    CHECK_EQ(sys_plate_px(false), alone);
    command(518, "sys.set state=off");
    tap_back(); // Home, for whatever runs next
    run(600);
    CHECK_EQ(sys_plate_px(false), 0);
}
