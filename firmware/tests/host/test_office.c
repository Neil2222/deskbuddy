// Office Mode: the dark scene switch, the clock face, and pausing the character (including mid-Monster).
#include <stdlib.h>
#include <string.h>

#include "buddy_brain.h"
#include "buddy_monster.h"
#include "buddy_view.h"
#include "gfx_repair.h"
#include "layout.h"
#include "office_clock.h"
#include "office_skeleton.h"
#include "office_switch.h"
#include "test.h"

enum { W = 1024, H = 600, ALIGN = 32 };

void test_office_switch_finishes_a_transition_before_the_next(void)
{
    office_switch_t s;
    office_switch_init(&s);
    office_switch_request(&s, true);
    office_step_t st = office_switch_step(&s); // redrawing the clock
    CHECK(office_switch_busy(&s) && st.swap && st.redraw_all);
    office_switch_request(&s, false); // changed mind

    // Like the render loop: present one frame per step that draws something.
    int swaps = 0, full = 0;
    for (int i = 0; i < 100 && (office_switch_busy(&s) || s.want_office != s.office); i++) {
        office_switch_presented(&s);
        st = office_switch_step(&s);
        swaps += st.swap;
        full += st.redraw_all;
    }
    CHECK(!s.office && !office_switch_busy(&s)); // back where it started...
    CHECK_EQ(swaps, 1); // ...after finishing the switch to the clock, then switching back
    CHECK_EQ(full, 1);  // one full-screen invalidation per switch: the repair redraws the other buffer
    CHECK_EQ(office_switch_step(&s).next_ms, UINT32_MAX); // settled: nothing to do until the next request
}

void test_office_time_validity(void)
{
    CHECK(!office_time_valid(0));          // power-up: 1970
    CHECK(!office_time_valid(1767225599)); // 2025-12-31 23:59:59 UTC
    CHECK(office_time_valid(1767225600));  // 2026-01-01
}

void test_office_font_covers_every_text(void)
{
    static const char *const k_texts[] = {"0123456789:-", "SUN MON TUE WED THU FRI SAT",
                                          "JAN FEB MAR APR MAY JUN JUL AUG SEP OCT NOV DEC", "CLOCK NOT SET"};
    for (unsigned i = 0; i < sizeof(k_texts) / sizeof(k_texts[0]); i++) {
        for (const char *c = k_texts[i]; *c; c++) {
            CHECK(office_font_has(*c));
        }
    }
    CHECK(!office_font_has('q'));
    // Every date must fit the date line: the widest weekday, day, month and year.
    const office_font_style_t st = {.unit = 2.0f, .stroke = 1.8f, .tracking = 9.0f};
    CHECK(office_font_text_width("WED 28 MAY 2026", &st) < 480 - 8);
    CHECK(office_font_text_width("CLOCK NOT SET", &st) < 480 - 8);
}

static uint8_t s_mem[OFFICE_CLOCK_MEMORY];
static gfx_color_t s_strip[16 * 1024];

static int count_ink(const gfx_image_t *img)
{
    int n = 0;
    for (int i = 0; i < img->w * img->h; i++) {
        n += img->px[i] != 0;
    }
    return n;
}

void test_office_clock_shows_time_date_and_fallback(void)
{
    static gfx_scene_t scene;
    static office_clock_t c;
    CHECK(gfx_scene_init(&scene, W, H, office_background, NULL, s_strip, 16 * 1024, ALIGN));
    CHECK(!office_clock_init(&c, &scene, (gfx_rect_t){0, 0, W, H}, s_mem, 1000, 20)); // too little memory
    CHECK(gfx_scene_init(&scene, W, H, office_background, NULL, s_strip, 16 * 1024, ALIGN));
    CHECK(office_clock_init(&c, &scene, (gfx_rect_t){0, 0, W, H}, s_mem, sizeof(s_mem), 20));

    // Tabular numerals: same cell for every digit, each with ink.
    for (int i = 0; i < 11; i++) {
        CHECK(c.glyph[i].w == c.glyph[0].w && c.glyph[i].h == c.glyph[0].h);
        CHECK(count_ink(&c.glyph[i]) > 0);
    }
    CHECK(count_ink(&c.glyph[8]) > count_ink(&c.glyph[1]));

    office_clock_show(&c, true);
    struct tm tm = {.tm_year = 126, .tm_mon = 9, .tm_mday = 14, .tm_wday = 2, .tm_hour = 9, .tm_min = 41};
    CHECK_EQ(office_clock_apply(&c, &tm, 100), 400);
    CHECK(c.digit[0].image == &c.glyph[0] && c.digit[1].image == &c.glyph[9]); // 24-hour, zero-padded
    CHECK(c.digit[2].image == &c.glyph[4] && c.digit[3].image == &c.glyph[1]);
    CHECK(strcmp(c.date_text, "TUE 14 OCT 2026") == 0);
    CHECK(c.colon.palette == &c.ink_pal); // glowing, first half of the second
    CHECK_EQ(office_clock_apply(&c, &tm, 700), 300);
    CHECK(c.colon.palette == &c.dim_pal);

    tm.tm_hour = 23;
    tm.tm_min = 5;
    office_clock_apply(&c, &tm, 0);
    CHECK(c.digit[0].image == &c.glyph[2] && c.digit[1].image == &c.glyph[3]);
    CHECK(c.digit[2].image == &c.glyph[0] && c.digit[3].image == &c.glyph[5]);

    // Unknown time: dashes, no glowing colon, and it says so.
    CHECK_EQ(office_clock_apply(&c, NULL, 100), 1000);
    for (int i = 0; i < 4; i++) {
        CHECK(c.digit[i].image == &c.glyph[10]);
    }
    CHECK(c.colon.palette == &c.dim_pal);
    CHECK(strcmp(c.date_text, "CLOCK NOT SET") == 0);

    // Everything sits on the screen.
    gfx_sprite_t *parts[] = {&c.digit[0], &c.digit[1], &c.digit[2], &c.digit[3], &c.colon, &c.date};
    for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        CHECK(gfx_rect_contains_rect((gfx_rect_t){0, 0, W, H}, gfx_sprite_bounds(parts[i])));
    }
    office_clock_show(&c, false);
    CHECK(gfx_rect_is_empty(gfx_sprite_bounds(&c.date)));
}

static gfx_color_t s_buf[2][W * H], s_ref[W * H];

// Midnight on a Friday: every digit and the date change at once. The incremental redraw on alternating buffers
// must equal a full redraw, and a minute change must stay small.
void test_office_clock_redraws_match_full_redraw(void)
{
    static gfx_scene_t scene;
    static office_clock_t c;
    static gfx_dirty_t changes, redraw, all;
    static gfx_repair_t repair;
    CHECK(gfx_scene_init(&scene, W, H, office_background, NULL, s_strip, 16 * 1024, ALIGN));
    CHECK(office_clock_init(&c, &scene, (gfx_rect_t){0, 0, W, H}, s_mem, sizeof(s_mem), 20));
    office_clock_show(&c, true);
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
    memset(s_buf, 0, sizeof(s_buf));
    const gfx_surface_t ref = {s_ref, W, H, W};

    struct tm tm = {.tm_year = 126, .tm_mon = 9, .tm_mday = 16, .tm_wday = 5, .tm_hour = 23, .tm_min = 58};
    int back = 0, bad = 0, frames = 0, minute_area = 0;
    for (int step = 0; step < 16; step++) {
        const uint32_t ms = (uint32_t)(step % 4) * 250;
        if (step == 4) {
            tm.tm_min = 59; // a plain minute change
        } else if (step == 8) {
            tm = (struct tm){.tm_year = 126, .tm_mon = 9, .tm_mday = 17, .tm_wday = 6}; // midnight
        } else if (step == 12) {
            office_clock_apply(&c, NULL, 0); // the clock was lost
            continue;
        }
        office_clock_apply(&c, step >= 13 ? NULL : &tm, ms);
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
        frames++;
        if (step == 4) {
            minute_area = gfx_dirty_area(&redraw);
        }
        back ^= 1;
    }
    CHECK(frames > 6);
    CHECK_EQ(bad, 0);
    CHECK(strcmp(c.date_text, "CLOCK NOT SET") == 0);
    // 23:58 -> 23:59 redraws one digit cell (plus the colon from the previous frame), nowhere near the screen.
    CHECK(minute_area > 0 && minute_area < W * H / 10);
}

// Office Mode pauses the character mid-Monster: everything hides, and afterwards he is back to plain idle with
// no props, ready for the next can.
void test_office_pauses_character_mid_monster(void)
{
    static gfx_scene_t scene;
    static buddy_view_t v;
    static layout_t layout;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(buddy_view_init(&v, &scene, layout.stage, 10, 0));

    buddy_brain_t b;
    buddy_pose_t p;
    buddy_brain_init(&b, 0, 1);
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_DRINK_MONSTER, 1000));
    buddy_brain_tick(&b, 1000 + BUDDY_MONSTER_CHARGE_AT + 300, &p); // charging: cable, bolts, battery on screen
    buddy_view_apply(&v, &p, 1000 + BUDDY_MONSTER_CHARGE_AT + 300);
    CHECK(!gfx_rect_is_empty(gfx_sprite_bounds(&v.can)) && !gfx_rect_is_empty(gfx_sprite_bounds(&v.battery)));

    buddy_view_hide(&v);
    for (int i = 0; i < scene.count; i++) {
        CHECK(gfx_rect_is_empty(gfx_sprite_bounds(scene.sprite[i])));
    }
    CHECK(!buddy_view_hit(&v, v.x0 + 100, v.y0 + 100));

    // Minutes later he comes back.
    const uint32_t back = 1000 + 600000;
    buddy_brain_rest(&b, back);
    buddy_brain_tick(&b, back, &p);
    CHECK_EQ(p.state, BUDDY_STATE_IDLE);
    CHECK_EQ(p.props.phase, BUDDY_PHASE_NONE);
    buddy_view_apply(&v, &p, back);
    CHECK(!gfx_rect_is_empty(gfx_sprite_bounds(&v.head)) && !gfx_rect_is_empty(gfx_sprite_bounds(&v.torso)));
    CHECK(gfx_rect_is_empty(gfx_sprite_bounds(&v.can)) && gfx_rect_is_empty(gfx_sprite_bounds(&v.battery)));
    CHECK(gfx_rect_is_empty(gfx_sprite_bounds(&v.rig[0])) && gfx_rect_is_empty(gfx_sprite_bounds(&v.rig[1])));
    CHECK(buddy_view_hit(&v, v.head.x + v.head.image->w * v.scale / 2, v.head.y + 40));
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_DRINK_MONSTER, back + 10)); // a new can works
}

// The whole loop as the firmware runs it (scene, character, clock, switch, double-buffer repair): into Office
// Mode in the middle of the Monster's charging and back again. Every presented buffer must equal a full redraw,
// full-screen frames (which make the scanout resync) happen only while booting or switching, and the other
// frames stay incremental.
void test_office_switch_redraws_fully_only_while_switching(void)
{
    static gfx_scene_t scene;
    static buddy_view_t view;
    static office_clock_t clock;
    static layout_t layout;
    static gfx_dirty_t changes, redraw, all;
    static gfx_repair_t repair;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(buddy_view_init(&view, &scene, layout.stage, 10, 0));
    CHECK(office_clock_init(&clock, &scene, layout.screen, s_mem, sizeof(s_mem), 20));
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    const gfx_surface_t ref = {s_ref, W, H, W};

    office_switch_t sw;
    office_switch_init(&sw);
    buddy_brain_t brain;
    buddy_brain_init(&brain, 0, 4);
    const struct tm tm = {.tm_year = 126, .tm_mon = 9, .tm_mday = 14, .tm_wday = 3, .tm_hour = 9, .tm_min = 41};

    // Boot: two full frames (as the firmware does).
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
    int back = 0, presented = 0, full_other = 0, bad = 0, max_other = 0, switches = 0;
    bool monster_seen_in_office = false;
    for (uint32_t t = 0; t < 9000; t += 10) {
        if (t == 1000) {
            CHECK(buddy_brain_event(&brain, BUDDY_EVENT_DRINK_MONSTER, t));
        }
        if (t == 1000 + BUDDY_MONSTER_CHARGE_AT + 500) {
            office_switch_request(&sw, true); // mid-charge
        }
        if (t == 7000) {
            office_switch_request(&sw, false);
        }
        const office_step_t st = office_switch_step(&sw);
        if (st.swap) {
            switches++;
            if (sw.office) {
                buddy_view_hide(&view);
                office_clock_show(&clock, true);
                scene.background = office_background;
                scene.background_ctx = NULL;
            } else {
                office_clock_show(&clock, false);
                scene.background = layout_background;
                scene.background_ctx = &layout;
                buddy_brain_rest(&brain, t);
            }
        }
        if (st.redraw_all) {
            gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
        } else if (st.redraw_kick || presented == 1) {
            gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, 1, 1});
        }
        if (sw.office) {
            office_clock_apply(&clock, &tm, t % 1000);
            monster_seen_in_office |= brain.state == BUDDY_STATE_MONSTER;
        } else {
            buddy_pose_t pose;
            buddy_brain_tick(&brain, t, &pose);
            buddy_view_apply(&view, &pose, t);
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
        const bool switching = office_switch_busy(&sw);
        office_switch_presented(&sw);
        const bool other = ++presented > 2 && !switching;
        const int area = gfx_dirty_area(&redraw);
        if (other) {
            full_other += area == W * H;
            max_other = area > max_other ? area : max_other;
        }
    }
    CHECK_EQ(switches, 2);
    CHECK(!sw.office);
    CHECK(monster_seen_in_office); // the switch really happened mid-Monster
    CHECK_EQ(bad, 0);
    CHECK_EQ(full_other, 0);
    CHECK(max_other < W * H / 4);
}

// A new face while the clock shows: both buffers are redrawn as after a switch; on Buddy's scene nothing happens.
void test_office_switch_redraws_a_new_face(void)
{
    office_switch_t s;
    office_switch_init(&s);
    office_switch_redraw(&s);
    CHECK(!office_switch_busy(&s));
    office_switch_request(&s, true);
    office_switch_step(&s);
    office_switch_presented(&s);
    office_switch_step(&s);
    office_switch_presented(&s);
    CHECK(office_switch_step(&s).redrawn);
    office_switch_redraw(&s);
    office_step_t st = office_switch_step(&s);
    CHECK(office_switch_busy(&s) && !st.swap && st.redraw_all);
    office_switch_request(&s, false); // waits for the redraw
    office_switch_presented(&s);
    st = office_switch_step(&s);
    CHECK(st.redraw_kick && !st.swap);
    office_switch_presented(&s);
    st = office_switch_step(&s);
    CHECK(st.redrawn && s.office);
    CHECK(office_switch_step(&s).swap && !s.office);
}

// --- Mechanical Skeleton ----------------------------------------------------------------------------------------

static uint8_t s_sk_mem[OFFICE_SKELETON_MEMORY];

static int z_of(const gfx_scene_t *scene, const gfx_sprite_t *sp)
{
    for (int i = 0; i < scene->count; i++) {
        if (scene->sprite[i] == sp) {
            return scene->z[i];
        }
    }
    return -1000;
}

static struct tm sk_time(int h, int m, int s)
{
    return (struct tm){.tm_year = 126, .tm_mon = 9, .tm_mday = 17, .tm_wday = 6, .tm_hour = h, .tm_min = m,
                       .tm_sec = s};
}

// The time stays on top: every part of the movement lies under the timeplate, which is solid under every digit,
// so no wheel, at any position, changes a pixel of the time.
void test_office_skeleton_keeps_the_time_on_top(void)
{
    static gfx_scene_t scene;
    static office_skeleton_t k;
    CHECK(gfx_scene_init(&scene, W, H, office_skeleton_background, &k, s_strip, 16 * 1024, ALIGN));
    CHECK(!office_skeleton_init(&k, &scene, (gfx_rect_t){0, 0, W, H}, s_sk_mem, 100 * 1024, 20)); // too little
    CHECK(gfx_scene_init(&scene, W, H, office_skeleton_background, &k, s_strip, 16 * 1024, ALIGN));
    CHECK(!office_skeleton_init(&k, &scene, (gfx_rect_t){0, 0, 800, 480}, s_sk_mem, sizeof(s_sk_mem), 20));
    CHECK(gfx_scene_init(&scene, W, H, office_skeleton_background, &k, s_strip, 16 * 1024, ALIGN));
    CHECK(office_skeleton_init(&k, &scene, (gfx_rect_t){0, 0, W, H}, s_sk_mem, sizeof(s_sk_mem), 20));
    CHECK(k.mem_used <= OFFICE_SKELETON_MEMORY);
    office_skeleton_show(&k, true);
    struct tm tm = sk_time(14, 37, 21);
    office_skeleton_apply(&k, &tm, 300);

    gfx_sprite_t *movement[OFFICE_SKELETON_GEARS + 3] = {&k.frame, &k.anchor, &k.cock};
    for (int g = 0; g < OFFICE_SKELETON_GEARS; g++) {
        movement[3 + g] = &k.gear[g];
    }
    const int plate_z = z_of(&scene, &k.plate);
    for (unsigned i = 0; i < sizeof(movement) / sizeof(movement[0]); i++) {
        CHECK(z_of(&scene, movement[i]) < plate_z);
        CHECK(gfx_rect_contains_rect((gfx_rect_t){0, 0, W, H}, gfx_sprite_bounds(movement[i])));
    }
    const gfx_rect_t plate = gfx_sprite_bounds(&k.plate);
    for (int d = 0; d < 4; d++) {
        CHECK(z_of(&scene, &k.digit[d]) > plate_z);
        const gfx_rect_t r = gfx_sprite_bounds(&k.digit[d]);
        CHECK(gfx_rect_contains_rect(plate, r));
        for (int y = r.y; y < r.y + r.h; y++) { // the plate is solid under the digit
            for (int x = r.x; x < r.x + r.w; x++) {
                CHECK(k.plate_img.px[(y - plate.y) / 2 * k.plate_img.w + (x - plate.x) / 2] != 0);
            }
        }
    }
    CHECK(z_of(&scene, &k.date) > plate_z && gfx_rect_contains_rect(plate, gfx_sprite_bounds(&k.date)));

    // Rendered: the time looks the same with and without the movement, mid-flourish too.
    static gfx_dirty_t all;
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    const gfx_surface_t with = {s_ref, W, H, W}, without = {s_buf[0], W, H, W};
    for (int pass = 0; pass < 2; pass++) {
        tm = pass ? sk_time(15, 0, 0) : sk_time(14, 37, 21);
        office_skeleton_apply(&k, &tm, pass ? 400 : 300);
        gfx_scene_render(&scene, &all, &with);
        for (unsigned i = 0; i < sizeof(movement) / sizeof(movement[0]); i++) {
            movement[i]->flags |= GFX_SPRITE_HIDDEN;
        }
        gfx_scene_render(&scene, &all, &without);
        office_skeleton_show(&k, true);
        for (int d = 0; d < 4; d++) {
            const gfx_rect_t r = gfx_sprite_bounds(&k.digit[d]);
            for (int y = r.y; y < r.y + r.h; y++) {
                CHECK(memcmp(&s_ref[y * W + r.x], &s_buf[0][y * W + r.x], (size_t)r.w * sizeof(gfx_color_t)) == 0);
            }
        }
    }
}

// Clockwork: the escapement every second, the left train every 5 s, the right one every 15 s, each wheel back to
// the same picture after four steps; the minute rolls in two frames; on the hour a quick whir that ends exactly
// where the movement would be anyway; stopped while the time is unknown.
void test_office_skeleton_moves_like_clockwork(void)
{
    static gfx_scene_t scene;
    static office_skeleton_t k;
    CHECK(gfx_scene_init(&scene, W, H, office_skeleton_background, &k, s_strip, 16 * 1024, ALIGN));
    CHECK(office_skeleton_init(&k, &scene, (gfx_rect_t){0, 0, W, H}, s_sk_mem, sizeof(s_sk_mem), 20));
    enum { ESC = OFFICE_SKELETON_GEARS - 1, LEFT = 0, RIGHT = 3 };
    office_skeleton_show(&k, true);
    const gfx_image_t *img[3];
    bool flip;
#define SAMPLE(h, m, s, ms)                                                                                            \
    do {                                                                                                               \
        const struct tm t_ = sk_time(h, m, s);                                                                         \
        office_skeleton_apply(&k, &t_, ms);                                                                            \
        img[0] = k.gear[ESC].image;                                                                                    \
        img[1] = k.gear[LEFT].image;                                                                                   \
        img[2] = k.gear[RIGHT].image;                                                                                  \
        flip = (k.anchor.flags & GFX_SPRITE_FLIP_X) != 0;                                                              \
    } while (0)
    SAMPLE(14, 37, 20, 300);
    const gfx_image_t *at20[3] = {img[0], img[1], img[2]};
    const bool flip20 = flip;
    SAMPLE(14, 37, 21, 300);
    CHECK(img[0] != at20[0] && img[1] == at20[1] && img[2] == at20[2] && flip != flip20);
    SAMPLE(14, 37, 24, 300);
    CHECK(img[0] == at20[0] && img[1] == at20[1] && flip == flip20); // four quarter teeth: the same picture
    SAMPLE(14, 37, 25, 300);
    CHECK(img[1] != at20[1]); // the left train
    // Over a minute: the left train steps 12 times, the right one 4 times, never in the same second.
    int left_steps = 0, right_steps = 0;
    SAMPLE(14, 38, 59, 300);
    for (int s = 0; s < 60; s++) {
        const gfx_image_t *l = img[1], *r = img[2];
        SAMPLE(14, 39, s, 300);
        left_steps += img[1] != l;
        right_steps += img[2] != r;
        CHECK(img[1] == l || img[2] == r);
    }
    CHECK_EQ(left_steps, 12);
    CHECK_EQ(right_steps, 4);
    for (int g = 0; g + 1 < OFFICE_SKELETON_GEARS - 1; g++) {
        if (g != 2) { // meshing neighbours turn opposite ways
            CHECK_EQ(k.dir[g] + k.dir[g + 1], 0);
        }
    }
    const struct tm t20 = sk_time(14, 37, 20);
    CHECK_EQ(office_skeleton_apply(&k, &t20, 300), 700);

    // 14:37:59 -> 14:38:00: the last digit rolls (two frames), the others stay.
    const struct tm t38 = sk_time(14, 38, 0);
    CHECK_EQ(office_skeleton_apply(&k, &t38, 20), OFFICE_SKELETON_TICK_FRAME_MS - 20);
    CHECK(k.digit[0].image == &k.glyph[1] && k.digit[1].image == &k.glyph[4] && k.digit[2].image == &k.glyph[3]);
    const gfx_image_t *roll1 = k.digit[3].image;
    CHECK(roll1 != &k.glyph[8] && roll1 != &k.glyph[7]);
    static gfx_dirty_t seen;
    gfx_dirty_init(&seen, W, H, ALIGN);
    gfx_scene_collect(&scene, &seen); // as the render loop does between frames
    office_skeleton_apply(&k, &t38, OFFICE_SKELETON_TICK_FRAME_MS + 10);
    CHECK(k.digit[3].image != roll1 && k.digit[3].image != &k.glyph[8]);
    office_skeleton_apply(&k, &t38, 2 * OFFICE_SKELETON_TICK_FRAME_MS);
    CHECK(k.digit[3].image == &k.glyph[8]);

    // 23:59 -> 00:00: every digit rolls.
    const struct tm midnight = sk_time(0, 0, 0);
    office_skeleton_apply(&k, &midnight, 10);
    for (int d = 0; d < 4; d++) {
        CHECK(k.digit[d].image != &k.glyph[0]);
    }

    // On the hour: quick steps, then exactly where it would have been.
    const uint32_t steps_ms = OFFICE_SKELETON_FLOURISH_STEPS * OFFICE_SKELETON_FLOURISH_MS;
    const struct tm hour = sk_time(20, 0, 0);
    CHECK(office_skeleton_apply(&k, &hour, 100) <= OFFICE_SKELETON_FLOURISH_MS);
    const gfx_image_t *a = k.gear[LEFT].image;
    office_skeleton_apply(&k, &hour, 100 + OFFICE_SKELETON_FLOURISH_MS);
    CHECK(k.gear[LEFT].image != a);
    const struct tm end = sk_time(20, 0, (int)(steps_ms / 1000));
    office_skeleton_apply(&k, &end, steps_ms % 1000 - 1);
    const gfx_image_t *last[OFFICE_SKELETON_GEARS];
    for (int g = 0; g < OFFICE_SKELETON_GEARS; g++) {
        last[g] = k.gear[g].image;
    }
    office_skeleton_apply(&k, &end, steps_ms % 1000 + 1);
    for (int g = 0; g < OFFICE_SKELETON_GEARS; g++) {
        CHECK(k.gear[g].image == last[g]); // no jump when the flourish ends
    }

    // Unknown time: stopped, and it says so.
    CHECK_EQ(office_skeleton_apply(&k, NULL, 0), 1000);
    for (int d = 0; d < 4; d++) {
        CHECK(k.digit[d].image == &k.glyph[10]);
    }
    CHECK(strcmp(k.date_text, "CLOCK NOT SET") == 0);
#undef SAMPLE
}

// The face over a minute and an hour, as the render loop draws it: every presented buffer equals a full redraw, no
// frame redraws the whole screen, and the ticks stay small.
void test_office_skeleton_redraws_match_full_redraw(void)
{
    static gfx_scene_t scene;
    static office_skeleton_t k;
    static gfx_dirty_t changes, redraw, all;
    static gfx_repair_t repair;
    CHECK(gfx_scene_init(&scene, W, H, office_skeleton_background, &k, s_strip, 16 * 1024, ALIGN));
    CHECK(office_skeleton_init(&k, &scene, (gfx_rect_t){0, 0, W, H}, s_sk_mem, sizeof(s_sk_mem), 20));
    office_skeleton_show(&k, true);
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
    memset(s_buf, 0, sizeof(s_buf));
    const gfx_surface_t ref = {s_ref, W, H, W};

    // 19:59:44 to 20:00:04 (both trains step, a minute roll, the hour's flourish), frame by frame as woken.
    const int start = 19 * 3600 + 59 * 60 + 44;
    int back = 0, bad = 0, frames = 0, full = 0;
    int max_second = 0, max_train = 0, max_flourish = 0;
    uint32_t t = 0;
    while (t < 20000) {
        const int secs = start + (int)(t / 1000);
        const struct tm tm = sk_time(secs / 3600 % 24, secs / 60 % 60, secs % 60);
        const uint32_t ms = t % 1000;
        const uint32_t next = office_skeleton_apply(&k, &tm, ms);
        gfx_dirty_clear(&changes);
        gfx_scene_collect(&scene, &changes);
        if (changes.count > 0) {
            gfx_repair_next(&repair, &changes, &redraw);
            const gfx_surface_t dst = {s_buf[back], W, H, W};
            gfx_scene_render(&scene, &redraw, &dst);
            gfx_scene_render(&scene, &all, &ref);
            bad += memcmp(s_buf[back], s_ref, sizeof(s_ref)) != 0;
            back ^= 1;
            const int area = gfx_dirty_area(&redraw);
            if (frames >= 2) { // after the first full redraw of both buffers
                full += area == W * H;
                const bool flourish = tm.tm_min == 0 && tm.tm_sec < 3;
                const int s5 = tm.tm_sec % 5, s15 = tm.tm_sec % 15; // a step, and the repair of the other buffer
                const bool train = !flourish && (s5 == 0 || s5 == 1 || s15 == 7 || s15 == 8) && ms < 500;
                int *max = flourish ? &max_flourish : train ? &max_train : &max_second;
                *max = area > *max ? area : *max;
            }
            frames++;
        }
        t += next ? next : 1;
    }
    printf("    skeleton: %d frames over 20 s; largest redraw: escapement tick %d px, train step %d px, "
           "flourish %d px; %u KB of pixels\n", frames, max_second, max_train, max_flourish,
           (unsigned)(k.mem_used / 1024));
    CHECK_EQ(bad, 0);
    CHECK_EQ(full, 0);
    CHECK(max_second > 0 && max_second < W * H / 12);
    CHECK(max_train < W * H / 3);
    // The hour's first frames (all four digits rolling while every wheel turns) are the largest; never the whole
    // screen, and no larger than the zoom's frames, which the scanout keeps up with.
    CHECK(max_flourish < W * H * 9 / 10);
}
