// FOCUS (after MonkeyUser's "Focus"): the plan grows, the interruption, the POOF, "NEVERMIND", the void; and the
// thoughts drawn behind him (menu_thought) redraw cleanly and stay on the stage.
#include <string.h>

#include "buddy_activity.h"
#include "buddy_brain.h"
#include "buddy_view.h"
#include "gfx_repair.h"
#include "layout.h"
#include "menu_thought.h"
#include "test.h"

enum { W = 1024, H = 600, ALIGN = 32 };

// The comic, panel by panel: each phase comes once and in order.
void test_focus_tells_the_comic_in_order(void)
{
    buddy_event_t ev;
    CHECK(buddy_act_parse("focus", &ev) && ev == BUDDY_EVENT_FOCUS); // act name=focus, hello acts=
    enum { PLAN, HEY, POOF, NEVERMIND, VOID, AFTER, PHASES };
    uint32_t first[PHASES];
    for (int i = 0; i < PHASES; i++) {
        first[i] = UINT32_MAX;
    }
    int last_plan = 0, last_drift = 0, max_lift = 0;
    bool tongue = false, glare = false;
    for (uint32_t t = 0; t < BUDDY_FOCUS_MS; t += 10) {
        buddy_pose_t p = {.state = BUDDY_STATE_FOCUS};
        buddy_activity_pose(BUDDY_STATE_FOCUS, t, &p);
        const buddy_thought_t *th = &p.thought;
        CHECK(th->plan >= last_plan || th->poof || th->drift); // the plan only grows, until it is gone
        CHECK(th->drift >= last_drift || th->drift == 0);       // the pieces only drift on (until they are gone)
        CHECK(p.lift <= BUDDY_MAX_HOVER && p.shake >= -1 && p.shake <= 1 && p.wobble >= -1 && p.wobble <= 1);
        CHECK(!(th->poof && th->drift) && !(th->plan && (th->poof || th->drift)));
        last_plan = th->plan;
        last_drift = th->drift;
        max_lift = p.lift > max_lift ? p.lift : max_lift;
        tongue |= p.mouth == BUDDY_MOUTH_TONGUE;
        glare |= th->speech == BUDDY_SPEECH_HEY && p.eyes == BUDDY_EYES_ANGRY && p.look == 1;
        const int phase = th->drift                                   ? VOID
                          : th->speech == BUDDY_SPEECH_NEVERMIND      ? NEVERMIND
                          : th->poof                                  ? POOF
                          : th->speech == BUDDY_SPEECH_HEY            ? HEY
                          : th->plan                                  ? PLAN
                          : t > 1000                                  ? AFTER
                                                                      : -1;
        if (phase >= 0 && first[phase] == UINT32_MAX) {
            first[phase] = t;
        }
        if (th->plan == BUDDY_PLAN_STEPS) {
            CHECK(first[PLAN] < t);
        }
    }
    CHECK(first[PLAN] < first[HEY] && first[HEY] < first[POOF] && first[POOF] < first[NEVERMIND]);
    CHECK(first[NEVERMIND] < first[VOID] && first[VOID] < first[AFTER]);
    CHECK_EQ(last_plan, 0);
    CHECK(tongue && glare && max_lift > BUDDY_MAX_LIFT); // he concentrates, glares, and floats in the void

    // The whole plan is up before the interruption; "WHAT WAS I DOING?" comes in the void.
    buddy_pose_t p = {.state = BUDDY_STATE_FOCUS};
    buddy_activity_pose(BUDDY_STATE_FOCUS, first[HEY] - 1, &p);
    CHECK_EQ(p.thought.plan, BUDDY_PLAN_STEPS);
    bool what = false;
    for (uint32_t t = first[VOID]; t < first[AFTER]; t += 10) {
        buddy_pose_t q = {.state = BUDDY_STATE_FOCUS};
        buddy_activity_pose(BUDDY_STATE_FOCUS, t, &q);
        what |= q.thought.speech == BUDDY_SPEECH_WHAT;
    }
    CHECK(what);
}

static gfx_color_t s_strip[16 * 1024];
static gfx_color_t s_buf[2][W * H];
static gfx_color_t s_ref[W * H];
static uint8_t s_thought_mem[MENU_THOUGHT_MEMORY];

// The render loop's way, twice through FOCUS with taps hammering it: every buffer equals a full redraw, nothing is
// drawn outside the stage, the thoughts redraw only when they change, and are gone afterwards.
void test_focus_thoughts_redraw_on_the_stage(void)
{
    static gfx_scene_t scene;
    static buddy_view_t view;
    static menu_thought_t thought;
    static gfx_dirty_t changes, redraw, all;
    static gfx_repair_t repair;
    static layout_t layout;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(buddy_view_init(&view, &scene, layout.stage, 10, 0));
    const int head_cx = view.home_x0 + view.art->w * view.home_scale / 2;
    CHECK(!menu_thought_init(&thought, &scene, layout.stage, head_cx, NULL, MENU_THOUGHT_MEMORY, 5));
    CHECK(menu_thought_init(&thought, &scene, layout.stage, head_cx, s_thought_mem, sizeof(s_thought_mem), 5));
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
    memset(s_buf, 0, sizeof(s_buf));
    const gfx_surface_t ref = {s_ref, W, H, W};

    buddy_brain_t brain;
    buddy_brain_init(&brain, 0, 4);
    buddy_brain_set_mood(&brain, BUDDY_MOOD_WORKING, 0, 0);
    int back = 0, frames = 0, bad = 0, outside = 0, drawn = 0, focus = 0;
    buddy_thought_t prev = {0};
    const uint32_t end = 2 * (BUDDY_FOCUS_MS + 1000);
    for (uint32_t t = 0, k = 0; t < end; k++) {
        if (focus < 2 && t >= 500 + (uint32_t)focus * (BUDDY_FOCUS_MS + 1000) && !buddy_brain_busy(&brain, t)) {
            CHECK(buddy_brain_event(&brain, BUDDY_EVENT_FOCUS, t));
            focus++;
        }
        if (k % 7 == 0) {
            CHECK(!buddy_brain_event(&brain, BUDDY_EVENT_POKE, t) || !buddy_brain_busy(&brain, t));
        }
        buddy_pose_t pose;
        uint32_t next = buddy_brain_tick(&brain, t, &pose);
        const uint32_t view_next = buddy_view_apply(&view, &pose, t);
        next = view_next < next ? view_next : next;
        menu_thought_apply(&thought, &pose.thought);
        drawn += memcmp(&pose.thought, &prev, sizeof(prev)) != 0;
        prev = pose.thought;
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
            }
            back ^= 1;
        }
        CHECK(thought.visible == (memcmp(&pose.thought, &(buddy_thought_t){0}, sizeof(pose.thought)) != 0));
        t += (next < 40 ? next : 40) + (k % 5 == 0 ? 3 : 0);
    }
    CHECK_EQ(focus, 2);
    CHECK_EQ(bad, 0);
    CHECK_EQ(outside, 0);
    CHECK(drawn > 20 && drawn < 120); // redrawn when it changes: the plan's steps, the burst, the drifting pieces
    CHECK(!thought.visible && gfx_rect_is_empty(gfx_sprite_bounds(&thought.sprite)));
    CHECK(gfx_rect_contains_rect(layout.stage, (gfx_rect_t){thought.sprite.x, thought.sprite.y,
                                                            MENU_THOUGHT_W * MENU_THOUGHT_SCALE,
                                                            MENU_THOUGHT_H * MENU_THOUGHT_SCALE}));
    CHECK_EQ(buddy_brain_mood(&brain, end), BUDDY_MOOD_WORKING);
    printf("    focus: %d frames, thoughts redrawn %d times\n", frames, drawn);
}
