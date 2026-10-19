// Claude usage on the device (usage.h, usage_view.h): the protocol line, the lease, the countdown's texts, the colours
// and the strip (where it sits, that it fits, and that it redraws only when what it says changes). The application's
// side (the strip in normal mode, in Clock Mode, not during Missile Defense) is in test_app.c.
#include <stdio.h>
#include <string.h>

#include "gfx_repair.h"
#include "gfx_scene.h"
#include "layout.h"
#include "link.h"
#include "marquee.h"
#include "menu_ask.h"
#include "test.h"
#include "usage.h"
#include "usage_view.h"

enum { W = 1024, H = 600, ALIGN = 32 };

static const char *frame(const char *content)
{
    static char buf[LINK_LINE_MAX];
    snprintf(buf, sizeof(buf), "%s #%04X", content, link_crc16(content, strlen(content)));
    return buf;
}

static link_result_t parse(const char *content, link_command_t *c)
{
    const char *line = frame(content);
    return link_parse_command(line, strlen(line), c);
}

void test_usage_protocol_lines(void)
{
    link_command_t c;
    CHECK_EQ(parse(">DB1 5 usage.set state=on five=37 five_end=1792360200 five_pred=130 week=20 "
                   "week_end=1792472400 week_pred=90",
                   &c),
             LINK_OK);
    CHECK(c.cmd == LINK_CMD_USAGE_SET && c.u.usage_set.on);
    const link_usage_t *five = &c.u.usage_set.win[LINK_USAGE_FIVE], *week = &c.u.usage_set.win[LINK_USAGE_WEEK];
    CHECK(five->has && five->pct == 37 && five->end == 1792360200 && five->pred == 130);
    CHECK(week->has && week->pct == 20 && week->end == 1792472400 && week->pred == 90);

    // A window without a prediction, the other missing; the limits; unknown keys ignored.
    CHECK_EQ(parse(">DB1 5 usage.set state=on week=999 week_end=0 later=1", &c), LINK_OK);
    CHECK(!c.u.usage_set.win[LINK_USAGE_FIVE].has);
    CHECK(week->has && week->pct == 999 && week->pred == -1 && week->end == 0);
    CHECK_EQ(parse(">DB1 5 usage.set state=on five=0 five_end=1792360200 five_pred=999", &c), LINK_OK);
    CHECK(five->has && five->pct == 0 && five->pred == 999 && !week->has);
    CHECK_EQ(parse(">DB1 5 usage.set state=on", &c), LINK_OK); // nothing to show, but well-formed
    CHECK(c.u.usage_set.on && !five->has && !week->has);
    CHECK_EQ(parse(">DB1 5 usage.set state=off", &c), LINK_OK);
    CHECK(!c.u.usage_set.on);

    static const char *const k_bad[] = {
        ">DB1 6 usage.set",                                          // no state
        ">DB1 6 usage.set state=maybe five=1 five_end=1",            // not on or off
        ">DB1 6 usage.set state=on five=37",                         // a pct without its end...
        ">DB1 6 usage.set state=on five_end=1792360200",             // ...an end without its pct
        ">DB1 6 usage.set state=on week=20 five_end=1792360200",     // the pair split across the windows
        ">DB1 6 usage.set state=on five_pred=130",                   // a prediction without its window
        ">DB1 6 usage.set state=on week=1 week_end=1 five_pred=130", // ...also next to the other one
        ">DB1 6 usage.set state=on five=1000 five_end=1792360200",   // out of range
        ">DB1 6 usage.set state=on five=-1 five_end=1792360200",
        ">DB1 6 usage.set state=on five=1 five_end=1 five_pred=1000",
        ">DB1 6 usage.set state=on five=1 five_end=-1",
        ">DB1 6 usage.set state=on five=1 five_end=4102444801",      // after 2100
        ">DB1 6 usage.set state=on five=37.5 five_end=1792360200",   // not a whole number
        ">DB1 6 usage.set state=on five=1 five_end=soon",
    };
    for (unsigned i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
        if (parse(k_bad[i], &c) != LINK_ERR_ARGS) {
            printf("    accepted: %s\n", k_bad[i]);
        }
        CHECK_EQ(parse(k_bad[i], &c), LINK_ERR_ARGS);
        CHECK_EQ(c.seq, 6); // answered: err bad_args
    }
}

// The lease: 30 s from the last usage.set; off, nothing to show, and a hello end it at once.
void test_usage_lease_and_hello(void)
{
    usage_t u;
    usage_init(&u);
    CHECK(!usage_available(&u, 0));
    CHECK_EQ(usage_next(&u, 0), UINT32_MAX);
    link_usage_t win[LINK_USAGE_WINDOWS] = {{true, 37, 130, 1792360200}, {false, 0, -1, 0}};
    const uint32_t t0 = 0xFFFFF000u; // across the ms counter's wrap
    usage_set(&u, true, win, t0);
    CHECK(usage_available(&u, t0));
    CHECK(usage_available(&u, t0 + USAGE_LEASE_MS - 1));
    CHECK_EQ(usage_next(&u, t0 + 1000), USAGE_LEASE_MS - 1000);
    CHECK(!usage_available(&u, t0 + USAGE_LEASE_MS));
    CHECK_EQ(usage_next(&u, t0 + USAGE_LEASE_MS), UINT32_MAX);

    usage_set(&u, true, win, t0); // renewed every 10 s: it holds
    for (uint32_t t = t0; t != t0 + 60000u; t += 10000u) {
        CHECK(usage_available(&u, t));
        usage_set(&u, true, win, t);
    }
    usage_reset(&u); // hello
    CHECK(!usage_available(&u, t0 + 60000u));
    CHECK_EQ(usage_next(&u, t0 + 60000u), UINT32_MAX);

    usage_set(&u, true, win, 100);
    usage_set(&u, false, win, 200); // switched off: the numbers go too
    CHECK(!usage_available(&u, 200));
    CHECK(!u.win[LINK_USAGE_FIVE].has);
    const link_usage_t none[LINK_USAGE_WINDOWS] = {{false, 0, -1, 0}, {false, 0, -1, 0}};
    usage_set(&u, true, none, 300); // no data: nothing to show
    CHECK(!usage_available(&u, 300));
}

static const char *countdown(int64_t left_ms, uint32_t *next)
{
    static char buf[USAGE_COUNTDOWN_MAX];
    const int64_t now = 1792360200000LL;
    *next = usage_countdown(buf, sizeof(buf), now, (now + left_ms) / 1000);
    return buf;
}

// Whole minutes, rounded up: 59m, then 1:00; 23:59, then 1d 0u; nu once it is reached.
void test_usage_countdown_boundaries(void)
{
    uint32_t next;
    CHECK(strcmp(countdown(-5000, &next), "nu") == 0 && next == UINT32_MAX);
    CHECK(strcmp(countdown(0, &next), "nu") == 0 && next == UINT32_MAX);
    CHECK(strcmp(countdown(1000, &next), "1m") == 0 && next == 1000);
    CHECK(strcmp(countdown(60000, &next), "1m") == 0 && next == 60000);
    CHECK(strcmp(countdown(61000, &next), "2m") == 0 && next == 1000);
    CHECK(strcmp(countdown(45 * 60000, &next), "45m") == 0);
    CHECK(strcmp(countdown(59 * 60000, &next), "59m") == 0);
    CHECK(strcmp(countdown(59 * 60000 + 1000, &next), "1:00") == 0);
    CHECK(strcmp(countdown(60 * 60000, &next), "1:00") == 0);
    CHECK(strcmp(countdown(150 * 60000, &next), "2:30") == 0);
    CHECK(strcmp(countdown((23 * 60 + 59) * 60000LL, &next), "23:59") == 0);
    CHECK(strcmp(countdown((23 * 60 + 59) * 60000LL + 1000, &next), "1d 0u") == 0);
    CHECK(strcmp(countdown(24 * 3600000LL, &next), "1d 0u") == 0);
    CHECK(strcmp(countdown((3 * 24 + 2) * 3600000LL + 30 * 60000, &next), "3d 2u") == 0);
    CHECK(strcmp(countdown(7 * 24 * 3600000LL, &next), "7d 0u") == 0);
    // From the device clock with its milliseconds: the next change is the next whole minute.
    char buf[USAGE_COUNTDOWN_MAX];
    CHECK_EQ(usage_countdown(buf, sizeof(buf), 1792360200250LL, 1792360200 + 120), 59750);
    CHECK(strcmp(buf, "2m") == 0);
    // The longest a well-formed usage.set can ask for still fits.
    usage_countdown(buf, sizeof(buf), 1767225600000LL, 4102444800LL); // 2026 to 2100
    CHECK(strcmp(buf, "27028d 0u") == 0);
}

void test_usage_colours_follow_the_pct(void)
{
    CHECK_EQ(usage_level(0), USAGE_GREEN);
    CHECK_EQ(usage_level(49), USAGE_GREEN);
    CHECK_EQ(usage_level(50), USAGE_YELLOW);
    CHECK_EQ(usage_level(74), USAGE_YELLOW);
    CHECK_EQ(usage_level(75), USAGE_ORANGE);
    CHECK_EQ(usage_level(89), USAGE_ORANGE);
    CHECK_EQ(usage_level(90), USAGE_RED);
    CHECK_EQ(usage_level(130), USAGE_RED);
    CHECK_EQ(usage_level(999), USAGE_RED);
}

static gfx_color_t s_strip[16 * 1024];
static gfx_color_t s_buf[2][W * H], s_ref[W * H];
static uint8_t s_mem[USAGE_VIEW_MEMORY];

// The strip: at the bottom of the stage, clear of Buddy's NEE/JA buttons and of the LED sign where Clock Mode puts it
// (both layouts); the widest it can say fits; and over three minutes of countdowns it redraws once a minute, only
// itself, every frame equal to a full redraw.
void test_usage_strip_fits_and_redraws_only_on_change(void)
{
    static gfx_scene_t scene;
    static layout_t layout;
    static usage_view_t v;
    static menu_ask_t ask;
    for (int left = 0; left < 2; left++) {
        layout = layout_compute(W, H, 65, left, ALIGN);
        CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
        CHECK(menu_ask_init(&ask, &scene, layout.stage, 200, 25));
        menu_ask_apply(&ask, MENU_QUESTION_PEUKIE, 1000, 1000);
        const gfx_rect_t area = usage_view_area(layout.stage);
        CHECK(gfx_rect_contains_rect(layout.stage, area));
        CHECK(area.y + area.h <= H);
        for (int i = 0; i < 2; i++) {
            CHECK(!gfx_rect_overlaps(area, gfx_sprite_bounds(&ask.button[i])));
        }
        CHECK(!gfx_rect_overlaps(area, gfx_sprite_bounds(&ask.timer)));
        const gfx_rect_t sign = {(W - MARQUEE_W) / 2, H - MARQUEE_H - 36, MARQUEE_W, MARQUEE_H}; // in Clock Mode
        CHECK(!gfx_rect_overlaps(area, sign));
        CHECK(!usage_view_init(&v, &scene, layout.stage, NULL, 28));
        CHECK(usage_view_init(&v, &scene, layout.stage, s_mem, 28));
        CHECK(gfx_rect_equal(gfx_sprite_bounds(&v.sprite), (gfx_rect_t){0, 0, 0, 0})); // hidden until there is data

        // The widest: both windows at 999 % with predictions, the widest countdowns of their windows.
        usage_t u;
        usage_init(&u);
        const int64_t now_ms = 1792360200000LL;
        const link_usage_t widest[LINK_USAGE_WINDOWS] = {
            {true, 999, 999, now_ms / 1000 + (4 * 60 + 59) * 60},
            {true, 999, 999, now_ms / 1000 + ((6 * 24 + 23) * 60 + 59) * 60}};
        usage_set(&u, true, widest, 0);
        usage_view_apply(&v, &u, now_ms);
        CHECK(gfx_rect_equal(gfx_sprite_bounds(&v.sprite), area));
        CHECK(strcmp(v.shown, "0:999:999:4:59;1:999:999:6d 23u;") == 0);
        CHECK(v.plate_w < v.img_w); // nothing cut off
        CHECK(v.plate_x > 0 && v.plate_x + v.plate_w < v.img_w);
    }

    // Three minutes of a five-hour window, the render loop's way.
    static gfx_dirty_t changes, redraw, all;
    static gfx_repair_t repair;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(usage_view_init(&v, &scene, layout.stage, s_mem, 28));
    const gfx_rect_t area = usage_view_area(layout.stage);
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    const gfx_surface_t ref = {s_ref, W, H, W};
    memset(s_buf, 0, sizeof(s_buf));
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
    usage_t u;
    usage_init(&u);
    const int64_t t0 = 1792360200000LL + 500; // half a second into a minute
    const link_usage_t win[LINK_USAGE_WINDOWS] = {{true, 37, 130, (t0 - 500) / 1000 + 150 * 60},
                                                  {true, 20, -1, (t0 - 500) / 1000 + 3 * 86400 + 2 * 3600}};
    const int strip_area = (area.w + 2 * ALIGN) * area.h;
    int back = 0, frames = 0, bad = 0, too_big = 0, strip_frames = 0;
    uint32_t wake = 0;
    for (uint32_t t = 0; t <= 200000; t += 100) {
        if (t >= 1000 && t < 190000 && t % 10000 == 1000) { // renewed every 10 s, as the companion does
            usage_set(&u, true, win, t);
        }
        if (t == 190000) {
            usage_reset(&u); // hello: gone at once
        }
        const uint32_t next = usage_view_apply(&v, usage_available(&u, t) ? &u : NULL, t0 + t);
        if (t == 1000) {
            wake = next;
        }
        gfx_dirty_clear(&changes);
        gfx_scene_collect(&scene, &changes);
        if (changes.count == 0) {
            continue;
        }
        gfx_repair_next(&repair, &changes, &redraw);
        const gfx_surface_t dst = {s_buf[back], W, H, W};
        gfx_scene_render(&scene, &redraw, &dst);
        gfx_scene_render(&scene, &all, &ref);
        bad += memcmp(s_buf[back], s_ref, sizeof(s_ref)) != 0;
        back ^= 1;
        if (++frames > 2) { // after the two boot frames
            too_big += gfx_dirty_area(&redraw) > strip_area;
            strip_frames++;
        }
    }
    CHECK_EQ(wake, 59500 - 1000); // the countdown changes at the next whole minute of the reset
    CHECK(strcmp(v.shown, "0:37:130:2:27;1:20:-1:3d 1u;") == 0); // 3 d 1 h 57 min: whole hours
    CHECK(v.sprite.flags & GFX_SPRITE_HIDDEN);
    CHECK_EQ(bad, 0);
    CHECK_EQ(too_big, 0);
    // After the boot frames (the second shows it): 2:29 at 59.5 s, 2:28, 2:27, then hidden by the hello.
    CHECK_EQ(strip_frames, 4);
}
