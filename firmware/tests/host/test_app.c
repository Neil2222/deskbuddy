// The application (components/app) end to end, as the board and the simulator run it, on a fake platform: a clock
// the test moves, frame buffers in memory, and the lines it sends to the companion collected. Commands go in as
// protocol lines, touches as touch events. Covers what the companion relies on: evt act (shown, then exactly one
// outcome; no and timeout distinct; an interruption never an answer), act retries, status act=, evt did (MY DAY),
// evt mood src=, and the recorded events' identity, time and replay (log.since). One application for all tests in this
// file (it is a singleton, like on the board), run in order.
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "app_link.h"
#include "app_port.h"
#include "buddy_activity.h"
#include "buddy_view.h"
#include "layout.h"
#include "menu.h"
#include "menu_ask.h"
#include "test.h"

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
    CHECK(sent("caps=poke,monster,mode,time,status,msg,mood,act,pdc,track,did,log,face "));
    CHECK(sent("acts=water,sneeze,stare,meditate,focus,ask_peukie faces=classic,skeleton dev="));
    clear_out();
    command(2, "status");
    CHECK(sent(" act=none"));
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

    // On the touchscreen: Activities, SNEEZE (after SURPRISE!, CLOCK and CLOCK FACES).
    clear_out();
    tap_slot(MENU_LIST, 1);
    run(600); // the tiles flip in
    tap_slot(MENU_GRID, 3);
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

// Clock faces: chosen by command or on the CLOCK FACES page, reported once per change (evt face, status face=);
// the CLOCK shortcut enters the selected face, a face chosen meanwhile shows at once, a tap still leaves Clock Mode,
// and his mood (changed meanwhile) carries on afterwards.
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

    // Activities, CLOCK FACES: CLASSIC, then SKELETON again.
    clear_out();
    tap_slot(MENU_LIST, 1);
    run(600);
    tap_slot(MENU_GRID, 2);
    run(600);
    tap_slot(MENU_GRID, 0);
    run(100);
    CHECK_EQ(count("evt face name=classic src=touch"), 1);
    tap_slot(MENU_GRID, 1);
    run(100);
    CHECK_EQ(count("evt face name=skeleton src=touch"), 1);
    command(75, "status");
    CHECK(sent(" face=skeleton #"));

    // Back to Activities: CLOCK zooms into the Skeleton.
    tap_back();
    run(600);
    clear_out();
    tap_slot(MENU_GRID, 1);
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
    tap_back(); // Home, for whatever runs next
    run(600);
}
