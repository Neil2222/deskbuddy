// The shared application core: frame protocol, events, timing, verification and the cross-platform frame checksum.
#include <string.h>

#include "deskbuddy_core.h"
#include "test.h"

enum { W = CORE_SCREEN_W, H = CORE_SCREEN_H, STRIP = 16 * 1024 };

// First frame of the default configuration (idle pose, every animation at time 0). The firmware logs the same
// value at boot ("frame crc32"), so a match proves device and simulator draw the same thing. Update it together
// with intentional changes to art, layout or rendering.
#define GOLDEN_FIRST_FRAME_CRC 0x00000000u

static gfx_color_t s_strip[STRIP];
static gfx_color_t s_fb[2][W * H];
static gfx_color_t s_scratch[W * H];

typedef struct {
    core_t core;
    int back;
    uint32_t presented;
    uint32_t stale; // pixels that differed from a full redraw, over all presented frames
} rig_t;

static gfx_surface_t surface(int i)
{
    return (gfx_surface_t){s_fb[i], W, H, W};
}

static bool rig_init(rig_t *r, const core_config_t *cfg, uint32_t now)
{
    memset(r, 0, sizeof(*r));
    r->back = 1;
    for (int b = 0; b < 2; b++) {
        memset(s_fb[b], 0xA5 + b, sizeof(s_fb[b])); // undefined content, different per buffer
    }
    return core_init(&r->core, cfg, s_strip, STRIP, now);
}

// One device-style frame: update, collect, render into the back buffer, verify it, swap. Returns the redraw area,
// or -1 when nothing changed (no present). *next_ms receives the core's next deadline.
static int rig_frame(rig_t *r, uint32_t now, uint32_t *next_ms)
{
    const uint32_t next = core_update(&r->core, now);
    if (next_ms) {
        *next_ms = next;
    }
    const gfx_dirty_t *redraw = core_collect(&r->core);
    if (!redraw) {
        return -1;
    }
    const gfx_surface_t back = surface(r->back), scratch = {s_scratch, W, H, W};
    core_render(&r->core, redraw, &back);
    r->stale += core_verify(&r->core, &back, &scratch, NULL, NULL);
    r->back ^= 1;
    r->presented++;
    return gfx_dirty_area(redraw);
}

static core_event_t touch(core_event_type_t type, int x, int y, uint32_t t)
{
    core_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.time_ms = t;
    ev.touch.x = (int16_t)x;
    ev.touch.y = (int16_t)y;
    return ev;
}

static core_event_t signal_ev(uint16_t id, uint32_t t)
{
    core_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = CORE_EVENT_SIGNAL;
    ev.time_ms = t;
    ev.signal.id = id;
    return ev;
}

static void body_center(const core_t *c, int *x, int *y)
{
    const gfx_rect_t b = gfx_sprite_bounds(&c->view.body);
    *x = b.x + b.w / 2;
    *y = b.y + b.h / 2;
}

void test_core_first_frames_fill_both_buffers(void)
{
    static rig_t r;
    core_config_t cfg;
    core_config_defaults(&cfg);
    CHECK(rig_init(&r, &cfg, 1000));
    uint32_t next = 0;
    CHECK_EQ(rig_frame(&r, 1000, &next), W * H);
    CHECK(!core_ready(&r.core));
    CHECK_EQ(core_update(&r.core, 1000), 0); // second buffer right away, not at the next animation step
    CHECK_EQ(rig_frame(&r, 1000, &next), W * H);
    CHECK(core_ready(&r.core));
    CHECK(next >= 1);
    CHECK_EQ(rig_frame(&r, 1000, NULL), -1); // nothing changed: no present
    CHECK_EQ(r.stale, 0);
}

void test_core_first_frame_matches_golden_crc(void)
{
    static rig_t r;
    const uint32_t seeds[] = {1, 0xDEADBEEF};
    const uint32_t starts[] = {0, 123456789};
    uint32_t crc[2][2];
    for (int s = 0; s < 2; s++) {
        for (int t = 0; t < 2; t++) {
            core_config_t cfg;
            core_config_defaults(&cfg);
            cfg.seed = seeds[s];
            CHECK(rig_init(&r, &cfg, starts[t]));
            CHECK_EQ(rig_frame(&r, starts[t], NULL), W * H);
            const gfx_surface_t first = surface(1);
            crc[s][t] = core_frame_crc32(&first);
        }
    }
    // Independent of the random seed and the boot time, so the device's value is comparable.
    CHECK_EQ(crc[0][0], crc[0][1]);
    CHECK_EQ(crc[0][0], crc[1][0]);
    CHECK_EQ(crc[0][0], crc[1][1]);
    if (crc[0][0] != GOLDEN_FIRST_FRAME_CRC) {
        printf("    first frame crc32 %08x\n", (unsigned)crc[0][0]);
    }
    CHECK_EQ(crc[0][0], GOLDEN_FIRST_FRAME_CRC);
}

void test_core_crc32_matches_ieee(void)
{
    // "12345678" as little-endian pixels: CRC-32/IEEE("12345678") = 0x9AE0DAAF.
    gfx_color_t px[4] = {0x3231, 0x3433, 0x3635, 0x3837};
    const gfx_surface_t s = {px, 4, 1, 4};
    CHECK_EQ(core_frame_crc32(&s), 0x9AE0DAAFu);
    gfx_color_t two_rows[8] = {0x3231, 0x3433, 0xFFFF, 0xFFFF, 0x3635, 0x3837, 0xFFFF, 0xFFFF};
    const gfx_surface_t strided = {two_rows, 2, 2, 4}; // stride padding is not part of the frame
    CHECK_EQ(core_frame_crc32(&strided), 0x9AE0DAAFu);
}

void test_core_touch_reacts_only_on_character(void)
{
    static rig_t r;
    core_config_t cfg;
    core_config_defaults(&cfg);
    CHECK(rig_init(&r, &cfg, 0));
    rig_frame(&r, 0, NULL);
    rig_frame(&r, 0, NULL);

    const core_event_t miss = touch(CORE_EVENT_TOUCH_DOWN, 900, 300, 100); // control-panel area
    CHECK(!core_handle_event(&r.core, &miss));
    CHECK(r.core.touch.down);
    const core_event_t up = touch(CORE_EVENT_TOUCH_UP, 900, 300, 120);
    CHECK(!core_handle_event(&r.core, &up));
    CHECK(!r.core.touch.down);
    core_update(&r.core, 130);
    CHECK_EQ(r.core.pose.state, BUDDY_STATE_IDLE);

    int x, y;
    body_center(&r.core, &x, &y);
    const core_event_t hit = touch(CORE_EVENT_TOUCH_DOWN, x, y, 200);
    CHECK(core_handle_event(&r.core, &hit));
    const core_event_t move = touch(CORE_EVENT_TOUCH_MOVE, x + 5, y, 210);
    CHECK(!core_handle_event(&r.core, &move));
    core_update(&r.core, 220);
    CHECK_EQ(r.core.pose.state, BUDDY_STATE_SURPRISED);
    CHECK_EQ(r.core.count.events, 4);
    CHECK_EQ(r.core.count.reactions, 1);
    CHECK_EQ(r.core.count.ignored, 0);
}

void test_core_signals_poke_and_ignore_unknown(void)
{
    static rig_t r;
    core_config_t cfg;
    core_config_defaults(&cfg);
    CHECK(rig_init(&r, &cfg, 0));
    const core_event_t unknown = signal_ev(999, 50);
    CHECK(!core_handle_event(&r.core, &unknown));
    CHECK_EQ(r.core.count.ignored, 1);
    core_event_t bad_type = signal_ev(CORE_SIGNAL_POKE, 60);
    bad_type.type = (core_event_type_t)77;
    CHECK(!core_handle_event(&r.core, &bad_type));
    CHECK_EQ(r.core.count.ignored, 2);
    core_update(&r.core, 70);
    CHECK_EQ(r.core.pose.state, BUDDY_STATE_IDLE);

    const core_event_t poke = signal_ev(CORE_SIGNAL_POKE, 100);
    CHECK(core_handle_event(&r.core, &poke));
    core_update(&r.core, 100);
    CHECK_EQ(r.core.pose.state, BUDDY_STATE_SURPRISED);
}

void test_core_renders_only_when_something_changes(void)
{
    static rig_t r;
    core_config_t cfg;
    core_config_defaults(&cfg);
    CHECK(rig_init(&r, &cfg, 0));
    uint32_t next = 0;
    rig_frame(&r, 0, NULL);
    rig_frame(&r, 0, &next);
    CHECK_EQ(rig_frame(&r, 0, &next), -1);
    CHECK(next >= 1);
    CHECK_EQ(rig_frame(&r, next - 1, NULL), -1); // before the deadline: still nothing
    const int area = rig_frame(&r, next, NULL);   // at the deadline: a small redraw, never the whole screen
    CHECK(area > 0 && area < W * H / 4);
    CHECK_EQ(r.stale, 0);
}

void test_core_late_update_lands_on_right_state(void)
{
    static rig_t r;
    core_config_t cfg;
    core_config_defaults(&cfg);
    CHECK(rig_init(&r, &cfg, 0));
    rig_frame(&r, 0, NULL);
    rig_frame(&r, 0, NULL);
    const core_event_t poke = signal_ev(CORE_SIGNAL_POKE, 1000);
    core_handle_event(&r.core, &poke);
    // No updates for a long time (a stalled frame): the next one lands where real time says, nothing is replayed.
    CHECK(rig_frame(&r, 1000 + BUDDY_SURPRISE_MS + 100, NULL) > 0);
    CHECK_EQ(r.core.pose.state, BUDDY_STATE_HAPPY);
    rig_frame(&r, 1000 + 5000, NULL);
    CHECK_EQ(r.core.pose.state, BUDDY_STATE_IDLE);
    CHECK_EQ(r.stale, 0);
}

void test_core_event_stamped_ahead_keeps_clock_monotonic(void)
{
    static rig_t r;
    core_config_t cfg;
    core_config_defaults(&cfg);
    CHECK(rig_init(&r, &cfg, 0));
    const core_event_t poke = signal_ev(CORE_SIGNAL_POKE, 500);
    core_handle_event(&r.core, &poke);
    core_update(&r.core, 100); // older than the event: treated as 500
    CHECK_EQ(r.core.now, 500);
    CHECK_EQ(r.core.pose.state, BUDDY_STATE_SURPRISED);
    CHECK_EQ(r.core.pose.lift, 0); // start of the hop, not a time-travel artefact
}

void test_core_demo_pokes(void)
{
    static rig_t r;
    core_config_t cfg;
    core_config_defaults(&cfg);
    cfg.demo_poke_ms = 3000;
    CHECK(rig_init(&r, &cfg, 0));
    uint32_t next = 0;
    rig_frame(&r, 0, NULL);
    rig_frame(&r, 2999, &next);
    CHECK_EQ(r.core.pose.state, BUDDY_STATE_IDLE);
    CHECK(next >= 1 && next <= 1);
    rig_frame(&r, 3000, NULL);
    CHECK_EQ(r.core.pose.state, BUDDY_STATE_SURPRISED);
    CHECK_EQ(r.core.count.reactions, 1);
}

void test_core_presented_frames_match_full_redraw(void)
{
    // Device-style run: startup, idle, taps on and off the character, at irregular frame times.
    static rig_t r;
    core_config_t cfg;
    core_config_defaults(&cfg);
    cfg.seed = 42;
    CHECK(rig_init(&r, &cfg, 0));
    int x, y;
    body_center(&r.core, &x, &y);
    uint32_t t = 0, next = 0;
    int full_after_ready = 0;
    for (int i = 0; i < 400; i++) {
        if (i % 37 == 5) {
            const core_event_t ev = touch(CORE_EVENT_TOUCH_DOWN, i % 2 ? x : 950, y, t);
            core_handle_event(&r.core, &ev);
        }
        const bool was_ready = core_ready(&r.core);
        const int area = rig_frame(&r, t, &next);
        full_after_ready += was_ready && area == W * H;
        t += next + (uint32_t)(i % 7) * 3; // sometimes late
    }
    CHECK(r.presented > 100);
    CHECK_EQ(r.stale, 0);
    CHECK_EQ(full_after_ready, 0); // no full-screen redraw once the screen is lit
}

void test_core_verify_reports_stale_pixels(void)
{
    static rig_t r;
    core_config_t cfg;
    core_config_defaults(&cfg);
    CHECK(rig_init(&r, &cfg, 0));
    rig_frame(&r, 0, NULL);
    gfx_surface_t frame = surface(1);
    const gfx_surface_t scratch = {s_scratch, W, H, W};
    int fx = -1, fy = -1;
    CHECK_EQ(core_verify(&r.core, &frame, &scratch, &fx, &fy), 0);
    s_fb[1][7 * W + 300] ^= 0xFFFF;
    s_fb[1][9 * W + 2] ^= 0x0001;
    CHECK_EQ(core_verify(&r.core, &frame, &scratch, &fx, &fy), 2);
    CHECK(fx == 300 && fy == 7);
}
