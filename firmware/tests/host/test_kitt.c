// KITT: the turn into a Knight Rider scanner, the sweep (for as long as it lasts), and the turn back to his mood.
#include <string.h>

#include "buddy_activity.h"
#include "buddy_brain.h"
#include "buddy_kitt.h"
#include "buddy_view.h"
#include "gfx_repair.h"
#include "layout.h"
#include "test.h"

enum { W = 1024, H = 600, ALIGN = 32 };
enum { KW = 32, KH = 16 }; // the overlay at his stage scale (8: half art pixels)
enum { HOT = 8 };          // buddy_kitt.c's hot core (the palette's last colour)

// The column of the brightest lamp in the overlay's lamp row (the first, if several).
static int bright_column(const uint8_t *px)
{
    int best = -1;
    for (int x = 0; x < KW; x++) {
        best = best < 0 || px[(KH / 2 - 1) * KW + x] > px[(KH / 2 - 1) * KW + best] ? x : best;
    }
    return best;
}

// The column of the light's hot core in the overlay's lamp row (-1: none).
static int hot_column(const uint8_t *px)
{
    const int x = bright_column(px);
    return px[(KH / 2 - 1) * KW + x] == HOT ? x : -1;
}

void test_kitt_turns_in_sweeps_and_turns_back_to_his_mood(void)
{
    buddy_brain_t b;
    buddy_brain_init(&b, 0, 5);
    buddy_brain_set_mood(&b, BUDDY_MOOD_HAPPY, 0, 0);
    buddy_pose_t p;
    buddy_brain_tick(&b, 900, &p);
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_KITT, 1000));

    // In: the eyes close before anything is drawn over the face; the antenna turns red once the face is black.
    int closed = -1, overlay = -1, red = -1;
    for (uint32_t t = 1000; t < 1000 + BUDDY_KITT_IN_MS; t += 10) {
        buddy_brain_tick(&b, t, &p);
        CHECK_EQ(p.state, BUDDY_STATE_KITT);
        CHECK(buddy_brain_busy(&b, t));
        CHECK(!buddy_brain_event(&b, BUDDY_EVENT_POKE, t));         // no other show...
        CHECK(!buddy_brain_kitt_leave(&b, t));                      // ...and no turning back halfway in
        closed = closed < 0 && p.eyes == BUDDY_EYES_CLOSED ? (int)t : closed;
        overlay = overlay < 0 && p.kitt.on ? (int)t : overlay;
        red = red < 0 && p.antenna == BUDDY_ANTENNA_STEADY ? (int)t : red;
    }
    CHECK(closed > 1000 && overlay > closed && red > overlay);

    // The sweep goes on and on: hours later he is still KITT, sweeping, and his clock stays bounded.
    for (uint32_t t = 1000 + BUDDY_KITT_IN_MS; t < 1000 + 3u * 3600 * 1000; t += 997) {
        buddy_brain_tick(&b, t, &p);
        CHECK(p.state == BUDDY_STATE_KITT && p.kitt.on && !p.kitt.out && p.antenna == BUDDY_ANTENNA_STEADY);
        CHECK(p.kitt.t >= BUDDY_KITT_IN_MS && p.kitt.t < BUDDY_KITT_IN_MS + 2 * 1800000u);
    }

    // A tap turns him back: no more taps count, and then his mood, as it was.
    const uint32_t leave = 1000 + 3u * 3600 * 1000;
    CHECK(buddy_brain_kitt_leave(&b, leave));
    CHECK(!buddy_brain_kitt_leave(&b, leave + 10));
    int face_back = -1;
    for (uint32_t t = leave; t < leave + BUDDY_KITT_OUT_MS; t += 10) {
        buddy_brain_tick(&b, t, &p);
        CHECK_EQ(p.state, BUDDY_STATE_KITT);
        CHECK(buddy_brain_busy(&b, t));
        face_back = face_back < 0 && !p.kitt.on ? (int)(t - leave) : face_back;
    }
    CHECK(face_back > 0 && face_back < (int)BUDDY_KITT_OUT_MS);
    buddy_brain_tick(&b, leave + BUDDY_KITT_OUT_MS, &p);
    CHECK_EQ(p.state, BUDDY_STATE_IDLE);
    CHECK(!p.kitt.on && p.tint == BUDDY_TINT_NORMAL);
    CHECK_EQ(buddy_brain_mood(&b, leave + BUDDY_KITT_OUT_MS), BUDDY_MOOD_HAPPY);
    CHECK(!buddy_brain_busy(&b, leave + BUDDY_KITT_OUT_MS));
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_KITT, leave + BUDDY_KITT_OUT_MS + 10)); // and again, any time
}

void test_kitt_a_mood_choice_turns_him_back_into_that_mood(void)
{
    // Chosen while he turns into KITT: he finishes turning, then turns straight back.
    buddy_brain_t b;
    buddy_brain_init(&b, 0, 5);
    buddy_brain_set_mood(&b, BUDDY_MOOD_RELAXED, 0, 0);
    buddy_pose_t p;
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_KITT, 1000));
    buddy_brain_set_mood(&b, BUDDY_MOOD_SLEEPY, 0, 1500);
    buddy_brain_tick(&b, 1000 + BUDDY_KITT_IN_MS - 1, &p);
    CHECK(p.kitt.on);
    CHECK_EQ(b.kitt_out, BUDDY_KITT_IN_MS);
    const uint32_t end = 1000 + BUDDY_KITT_IN_MS + BUDDY_KITT_OUT_MS;
    buddy_brain_tick(&b, end - 1, &p);
    CHECK_EQ(p.state, BUDDY_STATE_KITT);
    buddy_brain_tick(&b, end, &p);
    CHECK_EQ(p.state, BUDDY_STATE_IDLE);
    CHECK_EQ(buddy_brain_mood(&b, end), BUDDY_MOOD_SLEEPY);

    // Chosen while he sweeps: he turns back right away; the same mood as before counts too.
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_KITT, 10000));
    buddy_brain_tick(&b, 15000, &p);
    CHECK(p.kitt.on && !p.kitt.out);
    buddy_brain_set_mood(&b, BUDDY_MOOD_SLEEPY, 0, 15000);
    buddy_brain_tick(&b, 15000 + BUDDY_KITT_OUT_MS, &p);
    CHECK_EQ(p.state, BUDDY_STATE_IDLE);
    CHECK_EQ(buddy_brain_mood(&b, 15000 + BUDDY_KITT_OUT_MS), BUDDY_MOOD_SLEEPY);
}

void test_kitt_scanner_sweeps_with_a_tail(void)
{
    static uint8_t px[KW * KH];
    const gfx_palette_t *pal = buddy_kitt_palette();
    buddy_kitt_t k = {true, 0, 0};

    // Going in, the traces leave black behind them and his face in front of them.
    k.t = 900;
    buddy_kitt_draw(&k, px, KW, KH);
    CHECK_EQ(px[(KH / 2) * KW + 0], 1);      // black on the left...
    CHECK_EQ(px[(KH / 2) * KW + KW - 1], 0); // ...his face still on the right
    k.t = 1450;
    buddy_kitt_draw(&k, px, KW, KH);
    for (int i = 0; i < KW * KH; i++) {
        CHECK(px[i] != 0 && px[i] < pal->count); // all covered: the face is gone
    }

    // The sweep: the light reaches both ends, one round trip each BUDDY_KITT_SWEEP_MS, with its tail behind it.
    int lo = KW, hi = -1;
    for (uint32_t t = BUDDY_KITT_IN_MS; t < BUDDY_KITT_IN_MS + BUDDY_KITT_SWEEP_MS; t += 10) {
        k.t = t;
        buddy_kitt_draw(&k, px, KW, KH);
        const int x = hot_column(px);
        CHECK(x >= 0);
        lo = x < lo ? x : lo;
        hi = x > hi ? x : hi;
    }
    CHECK(lo <= 3 && hi >= KW - 4);
    k.t = BUDDY_KITT_IN_MS; // the middle, heading right (it came on at the left end)
    buddy_kitt_draw(&k, px, KW, KH);
    const int x = hot_column(px), row = (KH / 2 - 1) * KW;
    CHECK(x > 8 && x < KW - 8);
    CHECK(px[row + x - 3] > px[row + x + 3]); // the tail trails behind
    uint8_t again[KW * KH];
    k.t = BUDDY_KITT_IN_MS + 7u * BUDDY_KITT_SWEEP_MS; // the same picture every round trip
    buddy_kitt_draw(&k, again, KW, KH);
    CHECK(memcmp(px, again, sizeof(again)) == 0);

    // Turning back: the light glides to the middle and fades; the traces bring his face back.
    k.out = 50000;
    k.t = k.out + 500;
    buddy_kitt_draw(&k, px, KW, KH);
    const int mid = bright_column(px);
    CHECK(mid >= KW / 2 - 4 && mid <= KW / 2 + 1); // lamp row columns 2..29: its middle
    CHECK(hot_column(px) < 0);                      // fading
    k.t = k.out + 1000;
    buddy_kitt_draw(&k, px, KW, KH);
    CHECK_EQ(px[(KH / 2) * KW + 0], 1);      // still black on the left...
    CHECK_EQ(px[(KH / 2) * KW + KW - 1], 0); // ...his face back on the right
}

// The render loop's way, two buffers repaired from dirty rectangles: every frame equals a full redraw, and while he
// sweeps only his face screen is redrawn.
static gfx_color_t s_strip[16 * 1024];
static gfx_color_t s_buf[2][W * H];
static gfx_color_t s_ref[W * H];

void test_kitt_redraws_match_full_redraw(void)
{
    static gfx_scene_t scene;
    static buddy_view_t view;
    static gfx_dirty_t changes, redraw, all;
    static gfx_repair_t repair;
    static layout_t layout;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(buddy_view_init(&view, &scene, layout.stage, 10, 0));
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
    memset(s_buf, 0, sizeof(s_buf));
    const gfx_surface_t ref = {s_ref, W, H, W};

    buddy_brain_t brain;
    buddy_brain_init(&brain, 0, 3);
    buddy_brain_set_mood(&brain, BUDDY_MOOD_PEUKIE, 0, 0); // his hand and cigarette use the buffers KITT borrows
    const uint32_t kitt = 3000, leave = kitt + 20000, end = leave + BUDDY_KITT_OUT_MS + 4000;
    int back = 0, frames = 0, bad = 0, outside = 0, sweep_frames = 0, sweep_max = 0;
    bool started = false, left = false;
    for (uint32_t t = 0, k = 0; t < end; k++) {
        if (!started && t >= kitt) {
            CHECK(buddy_brain_event(&brain, BUDDY_EVENT_KITT, t));
            started = true;
        }
        if (!left && t >= leave) {
            CHECK(buddy_brain_kitt_leave(&brain, t));
            left = true;
        }
        buddy_pose_t pose;
        uint32_t next = buddy_brain_tick(&brain, t, &pose);
        const uint32_t view_next = buddy_view_apply(&view, &pose, t);
        next = view_next < next ? view_next : next;
        gfx_dirty_clear(&changes);
        gfx_scene_collect(&scene, &changes);
        if (changes.count > 0) {
            gfx_repair_next(&repair, &changes, &redraw);
            const gfx_surface_t dst = {s_buf[back], W, H, W};
            gfx_scene_render(&scene, &redraw, &dst);
            gfx_scene_render(&scene, &all, &ref);
            bad += memcmp(s_buf[back], s_ref, sizeof(s_ref)) != 0;
            if (++frames > 2) {
                for (int i = 0; i < redraw.count; i++) {
                    outside += !gfx_rect_contains_rect(layout.stage, redraw.rect[i]);
                }
                if (t >= kitt + BUDDY_KITT_IN_MS + 100 && t < leave) {
                    const int area = gfx_dirty_area(&redraw);
                    sweep_max = area > sweep_max ? area : sweep_max;
                    sweep_frames++;
                }
            }
            back ^= 1;
        }
        t += next + (k % 7 == 0 ? 3 : 0);
    }
    const gfx_rect_t face = {view.home_x0 + view.art->face.x * view.home_scale,
                             view.home_y0 + view.art->face.y * view.home_scale, view.art->face.w * view.home_scale,
                             view.art->face.h * view.home_scale};
    CHECK_EQ(bad, 0);
    CHECK_EQ(outside, 0);
    CHECK(sweep_frames > 20000 / 40); // smooth: about every BUDDY_KITT_FRAME_MS
    CHECK(sweep_max <= (face.w + 2 * ALIGN) * face.h); // the face screen only (x aligned to cache lines)
    CHECK(view.kitt.flags & GFX_SPRITE_HIDDEN);
    CHECK_EQ(brain.state, BUDDY_STATE_IDLE);
    printf("    kitt: %d frames, %d while sweeping, largest sweep redraw %d px (face screen %d px)\n", frames,
           sweep_frames, sweep_max, face.w * face.h);
}
