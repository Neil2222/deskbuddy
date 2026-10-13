// Character behavior, artwork and placement, plus the screen layout.
#include <string.h>

#include "buddy_brain.h"
#include "buddy_view.h"
#include "layout.h"
#include "test.h"
#include "test_util.h"

void test_buddy_art_decodes_within_palette(void)
{
    const buddy_art_t *a = buddy_art_get();
    CHECK(a != NULL);
    CHECK(buddy_art_get() == a); // decoded once
    CHECK_EQ(a->body.w, 28);
    CHECK_EQ(a->body.h, 31);
    const gfx_image_t *all[] = {&a->body, &a->antenna_on, &a->antenna_off, &a->shadow, &a->shadow_small,
                                &a->eyes[0], &a->eyes[1], &a->eyes[2], &a->eyes[3], &a->eyes[4],
                                &a->mouth[0], &a->mouth[1], &a->mouth[2]};
    for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        for (int p = 0; p < all[i]->w * all[i]->h; p++) {
            CHECK(all[i]->px[p] < a->palette.count);
        }
    }
    // Interchangeable frames share a size, so swapping them redraws a fixed area.
    for (int i = 1; i < BUDDY_EYES_COUNT; i++) {
        CHECK(a->eyes[i].w == a->eyes[0].w && a->eyes[i].h == a->eyes[0].h);
    }
    for (int i = 1; i < BUDDY_MOUTH_COUNT; i++) {
        CHECK(a->mouth[i].w == a->mouth[0].w && a->mouth[i].h == a->mouth[0].h);
    }
    CHECK(a->shadow.w == a->shadow_small.w && a->shadow.h == a->shadow_small.h);
    // Face parts sit on the face screen (body rows 6..13 = art rows 10..17, columns 6..21).
    for (int e = 0; e < 2; e++) {
        CHECK(a->eye_at[e].x >= 6 && a->eye_at[e].x + a->eyes[0].w <= 22);
        CHECK(a->eye_at[e].y >= 10 && a->eye_at[e].y + a->eyes[0].h <= 18);
    }
    CHECK(a->mouth_at.x >= 6 && a->mouth_at.x + a->mouth[0].w <= 22);
    CHECK(a->mouth_at.y >= 10 && a->mouth_at.y + a->mouth[0].h <= 18);
}

void test_buddy_poke_runs_surprise_happy_idle(void)
{
    buddy_brain_t b;
    buddy_pose_t p;
    buddy_brain_init(&b, 1000, 1);
    buddy_brain_tick(&b, 2000, &p);
    CHECK_EQ(p.state, BUDDY_STATE_IDLE);

    buddy_brain_poke(&b, 2000);
    CHECK(buddy_brain_tick(&b, 2000, &p) >= 1);
    CHECK_EQ(p.state, BUDDY_STATE_SURPRISED);
    CHECK_EQ(p.eyes, BUDDY_EYES_WIDE);
    CHECK_EQ(p.mouth, BUDDY_MOUTH_OPEN);
    buddy_brain_tick(&b, 2000 + BUDDY_SURPRISE_MS / 2, &p);
    CHECK_EQ(p.lift, 3); // top of the surprise hop
    buddy_brain_tick(&b, 2000 + BUDDY_SURPRISE_MS - 1, &p);
    CHECK_EQ(p.state, BUDDY_STATE_SURPRISED);

    const uint32_t happy = 2000 + BUDDY_SURPRISE_MS;
    buddy_brain_tick(&b, happy, &p);
    CHECK_EQ(p.state, BUDDY_STATE_HAPPY);
    CHECK_EQ(p.eyes, BUDDY_EYES_HAPPY);
    CHECK_EQ(p.mouth, BUDDY_MOUTH_SMILE);
    CHECK_EQ(p.antenna, BUDDY_ANTENNA_EXCITED);
    int max_lift = 0, hops = 0, prev_lift = 0;
    for (uint32_t t = happy; t < happy + BUDDY_HAPPY_HOPS * BUDDY_HAPPY_HOP_MS; t += 5) {
        buddy_brain_tick(&b, t, &p);
        CHECK_EQ(p.state, BUDDY_STATE_HAPPY);
        CHECK(p.lift <= BUDDY_MAX_LIFT);
        max_lift = p.lift > max_lift ? p.lift : max_lift;
        hops += prev_lift == 0 && p.lift > 0;
        prev_lift = p.lift;
    }
    CHECK_EQ(max_lift, BUDDY_MAX_LIFT);
    CHECK_EQ(hops, BUDDY_HAPPY_HOPS);

    buddy_brain_tick(&b, happy + BUDDY_HAPPY_HOPS * BUDDY_HAPPY_HOP_MS, &p);
    CHECK_EQ(p.state, BUDDY_STATE_IDLE);
    CHECK_EQ(p.antenna, BUDDY_ANTENNA_CALM);

    // A poke while happy restarts the reaction.
    buddy_brain_poke(&b, 9000);
    buddy_brain_tick(&b, 9000 + BUDDY_SURPRISE_MS + 10, &p);
    CHECK_EQ(p.state, BUDDY_STATE_HAPPY);
    buddy_brain_poke(&b, 9500);
    buddy_brain_tick(&b, 9500, &p);
    CHECK_EQ(p.state, BUDDY_STATE_SURPRISED);
}

// Runs the brain like the render loop does: jump straight to each reported change. Counts blinks (closed eyes)
// and breathing toggles over `duration` ms starting at `start`.
static void run_idle(uint32_t start, uint32_t duration, uint32_t seed, int *blinks, int *breaths, uint32_t *max_wait)
{
    buddy_brain_t b;
    buddy_pose_t p;
    buddy_brain_init(&b, start, seed);
    *blinks = *breaths = 0;
    *max_wait = 0;
    buddy_eyes_t prev_eyes = BUDDY_EYES_OPEN;
    int prev_lift = 0;
    for (uint32_t t = start; t - start < duration;) {
        const uint32_t wait = buddy_brain_tick(&b, t, &p);
        *blinks += p.eyes == BUDDY_EYES_CLOSED && prev_eyes != BUDDY_EYES_CLOSED;
        *breaths += p.lift != prev_lift;
        *max_wait = wait > *max_wait ? wait : *max_wait;
        prev_eyes = p.eyes;
        prev_lift = p.lift;
        if (wait == 0) {
            *max_wait = UINT32_MAX; // must never happen: would spin the render loop
            return;
        }
        t += wait;
    }
}

void test_buddy_idle_blinks_and_breathes(void)
{
    int blinks, breaths;
    uint32_t max_wait;
    run_idle(0, 60000, 99, &blinks, &breaths, &max_wait);
    CHECK(blinks >= 60000 / (BUDDY_BLINK_MAX_MS + 180)); // at least one blink per max interval
    CHECK(blinks <= 60000 / BUDDY_BLINK_MIN_MS + 1);
    CHECK(breaths >= 60000 / BUDDY_BOB_MS - 2 && breaths <= 60000 / BUDDY_BOB_MS + 1);
    CHECK(max_wait <= BUDDY_BOB_MS);
}

void test_buddy_survives_clock_wraparound(void)
{
    int blinks, breaths;
    uint32_t max_wait;
    run_idle(UINT32_MAX - 5000, 30000, 7, &blinks, &breaths, &max_wait);
    CHECK(blinks >= 30000 / (BUDDY_BLINK_MAX_MS + 180));
    CHECK(breaths >= 30000 / BUDDY_BOB_MS - 2);
    CHECK(max_wait <= BUDDY_BOB_MS);

    buddy_brain_t b;
    buddy_pose_t p;
    buddy_brain_init(&b, UINT32_MAX - 100, 3);
    buddy_brain_poke(&b, UINT32_MAX - 100);
    buddy_brain_tick(&b, (uint32_t)(UINT32_MAX - 100 + BUDDY_SURPRISE_MS), &p); // crosses zero
    CHECK_EQ(p.state, BUDDY_STATE_HAPPY);
}

void test_buddy_view_fits_stage_and_hits(void)
{
    static gfx_color_t strip[1024 * 4];
    static gfx_scene_t scene;
    static buddy_view_t v;
    const gfx_rect_t stage = {0, 0, 672, 600};
    CHECK(gfx_scene_init(&scene, 1024, 600, test_background, NULL, strip, 1024 * 4, 32));
    CHECK(buddy_view_init(&v, &scene, stage, 10, 0));
    CHECK_EQ(v.scale, 8);
    CHECK_EQ(scene.count, 6);

    const gfx_rect_t body = gfx_sprite_bounds(&v.body);
    const int cx = body.x + body.w / 2, cy = body.y + body.h / 2;
    CHECK(buddy_view_hit(&v, cx, cy));
    CHECK(!buddy_view_hit(&v, 5, 5));
    CHECK(!buddy_view_hit(&v, 900, cy)); // control-panel side

    buddy_pose_t pose = {BUDDY_STATE_HAPPY, BUDDY_EYES_HAPPY, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_EXCITED, 0};
    const int shadow_y = v.shadow.y;
    for (int lift = 0; lift <= BUDDY_MAX_LIFT; lift++) {
        pose.lift = (uint8_t)lift;
        buddy_view_apply(&v, &pose, 0);
        CHECK_EQ(v.body.y, body.y - lift * v.scale);
        CHECK_EQ(v.shadow.y, shadow_y); // the shadow stays on the ground
        CHECK(v.eye[0].image == &v.art->eyes[BUDDY_EYES_HAPPY]);
        gfx_sprite_t *parts[] = {&v.shadow, &v.body, &v.antenna, &v.eye[0], &v.eye[1], &v.mouth};
        for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
            CHECK(gfx_rect_contains_rect(stage, gfx_sprite_bounds(parts[i])));
        }
    }
    CHECK(v.shadow.image == &v.art->shadow_small); // high hop: smaller shadow

    // A tiny stage cannot hold the character.
    static gfx_scene_t small;
    CHECK(gfx_scene_init(&small, 1024, 600, test_background, NULL, strip, 1024 * 4, 32));
    CHECK(!buddy_view_init(&v, &small, (gfx_rect_t){0, 0, 20, 20}, 0, 0));
}

void test_layout_splits_screen(void)
{
    const layout_t right = layout_compute(1024, 600, 65, false, 32);
    CHECK_EQ(right.stage.x, 0);
    CHECK_EQ(right.stage.w, 672); // 65 % = 665.6, rounded to the 32-px grid
    CHECK_EQ(right.panel.x, 672);
    CHECK_EQ(right.panel.w, 352);
    CHECK_EQ(right.stage.h, 600);

    const layout_t left = layout_compute(1024, 600, 65, true, 32);
    CHECK_EQ(left.panel.x, 0);
    CHECK_EQ(left.panel.w, 352);
    CHECK_EQ(left.stage.x, 352);

    const layout_t full = layout_compute(1024, 600, 150, false, 32);
    CHECK_EQ(full.stage.w, 1024);
    CHECK(gfx_rect_is_empty(full.panel));

    // The background paints the stage gradient, the panel, and a divider on the panel side.
    gfx_color_t row[1024];
    layout_background((void *)&right, row, 0, 300, 1024);
    CHECK(row[0] == row[671]);   // stage: one color per row
    CHECK(row[672] != row[671]); // divider
    CHECK(row[673] == row[672]);
    CHECK(row[674] != row[673]); // panel
    CHECK(row[674] == row[1023]);
    gfx_color_t top, bottom;
    layout_background((void *)&right, &top, 10, 0, 1);
    layout_background((void *)&right, &bottom, 10, 599, 1);
    CHECK(top != bottom); // gradient
    gfx_color_t seg[8];
    layout_background((void *)&right, seg, 670, 300, 8); // partial span across the boundary
    for (int i = 0; i < 8; i++) {
        CHECK_EQ(seg[i], row[670 + i]);
    }
}
