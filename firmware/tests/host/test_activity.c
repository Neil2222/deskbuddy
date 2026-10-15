#include <string.h>

#include "buddy_activity.h"
#include "buddy_brain.h"
#include "buddy_view.h"
#include "gfx_repair.h"
#include "layout.h"
#include "test.h"

enum { W = 1024, H = 600, ALIGN = 32 };

static const struct {
    buddy_event_t ev;
    buddy_state_t state;
    uint32_t ms;
    const char *doing;
} k_acts[] = {
    {BUDDY_EVENT_DRINK_WATER, BUDDY_STATE_WATER, BUDDY_WATER_MS, "drinking"},
    {BUDDY_EVENT_SNEEZE, BUDDY_STATE_SNEEZE, BUDDY_SNEEZE_MS, "sneezing"},
    {BUDDY_EVENT_STARE, BUDDY_STATE_STARE, BUDDY_STARE_MS, "staring"},
    {BUDDY_EVENT_MEDITATE, BUDDY_STATE_MEDITATE, BUDDY_MEDITATE_MS, "meditating"},
    {BUDDY_EVENT_ZDMW, BUDDY_STATE_ZDMW, BUDDY_ZDMW_MS, "fuming"}, // PDC Manager confirmed a click
    {BUDDY_EVENT_FOCUS, BUDDY_STATE_FOCUS, BUDDY_FOCUS_MS, "focusing"},
};
#define N_ACTS (int)(sizeof(k_acts) / sizeof(k_acts[0]))

void test_activity_plays_to_the_end_then_the_mood_resumes(void)
{
    for (int i = 0; i < N_ACTS; i++) {
        buddy_brain_t b;
        buddy_brain_init(&b, 0, 11);
        buddy_brain_set_mood(&b, BUDDY_MOOD_RELAXED, 0, 0);
        buddy_pose_t pose;
        buddy_brain_tick(&b, 900, &pose);
        CHECK(!buddy_brain_busy(&b, 900));
        CHECK(buddy_brain_event(&b, k_acts[i].ev, 1000));
        CHECK_EQ(buddy_activity_ms(k_acts[i].state), k_acts[i].ms);
        CHECK(strcmp(buddy_activity_doing(k_acts[i].state), k_acts[i].doing) == 0);
        // While it plays, nothing else starts: not a tap, not a Monster, not itself again.
        for (uint32_t t = 1000; t < 1000 + k_acts[i].ms; t += 97) {
            buddy_brain_tick(&b, t, &pose);
            CHECK_EQ(pose.state, k_acts[i].state);
            CHECK(buddy_brain_busy(&b, t));
            CHECK(!buddy_brain_event(&b, BUDDY_EVENT_POKE, t));
            CHECK(!buddy_brain_event(&b, BUDDY_EVENT_DRINK_MONSTER, t));
            CHECK(!buddy_brain_event(&b, k_acts[i].ev, t));
        }
        // Then his mood, as it was; and he takes requests again.
        buddy_brain_tick(&b, 1000 + k_acts[i].ms, &pose);
        CHECK_EQ(pose.state, BUDDY_STATE_IDLE);
        CHECK(!buddy_brain_busy(&b, 1000 + k_acts[i].ms));
        CHECK_EQ(buddy_brain_mood(&b, 1000 + k_acts[i].ms), BUDDY_MOOD_RELAXED);
        CHECK(pose.closeup == 0 && pose.pop == 0 && pose.say == 0 && pose.props.glass == 0);
        CHECK(pose.thought.plan == 0 && pose.thought.poof == 0 && pose.thought.speech == 0 && pose.thought.drift == 0);
        CHECK(buddy_brain_event(&b, BUDDY_EVENT_POKE, 1000 + k_acts[i].ms + 10));
    }
    CHECK(buddy_activity_doing(BUDDY_STATE_IDLE) == NULL);
    CHECK_EQ(buddy_activity_ms(BUDDY_STATE_HAPPY), 0);
}

// Plays activity `state` and records what happens, every 10 ms.
typedef struct {
    int first_full_glass, glass_at_mouth_min, empty_glass, zap, alert, say, glass_gone;
    int pop_max, pop_back, neck_poof, feet_poof, closeup_full, blink_close, closeup_back;
    int lift_max, overload_up, landed;
} story_t;

static story_t play(buddy_state_t state)
{
    story_t s;
    memset(&s, -1, sizeof(s));
    s.pop_max = s.lift_max = 0;
    for (uint32_t t = 0; t < buddy_activity_ms(state); t += 10) {
        buddy_pose_t p = {.state = state};
        buddy_activity_pose(state, t, &p);
        const int now = (int)t;
        const buddy_props_t *pr = &p.props;
        if (pr->glass == BUDDY_GLASS_LEVELS && s.first_full_glass < 0) {
            s.first_full_glass = now;
        }
        if (pr->hand_from == BUDDY_HAND_SIP && pr->hand_to == BUDDY_HAND_SIP) {
            s.glass_at_mouth_min = s.glass_at_mouth_min < 0 || pr->glass < s.glass_at_mouth_min ? pr->glass
                                                                                               : s.glass_at_mouth_min;
        }
        if (pr->glass == 1 && s.empty_glass < 0) {
            s.empty_glass = now;
        }
        if (pr->alert && s.alert < 0) {
            s.alert = now;
        }
        if (p.tint == BUDDY_TINT_ZAP && s.zap < 0) {
            s.zap = now;
        }
        if (p.say && s.say < 0) {
            s.say = now;
        }
        if (s.first_full_glass >= 0 && pr->glass == 0 && s.glass_gone < 0) {
            s.glass_gone = now;
        }
        s.pop_max = p.pop > s.pop_max ? p.pop : s.pop_max;
        if (s.pop_max == BUDDY_MAX_POP && p.pop == 0 && s.pop_back < 0) {
            s.pop_back = now;
        }
        for (int i = 0; i < BUDDY_FX_COUNT; i++) {
            const buddy_fx_t *f = &pr->fx[i];
            if (f->kind == BUDDY_FX_POOF && (f->at == BUDDY_SPOT_NECK_L || f->at == BUDDY_SPOT_NECK_R) &&
                s.neck_poof < 0) {
                s.neck_poof = now;
            }
            if (f->kind == BUDDY_FX_POOF && (f->at == BUDDY_SPOT_FEET_L || f->at == BUDDY_SPOT_FEET_R) &&
                s.feet_poof < 0) {
                s.feet_poof = now;
            }
        }
        if (p.closeup == 255 && s.closeup_full < 0) {
            s.closeup_full = now;
        }
        if (p.closeup == 255 && p.eyes == BUDDY_EYES_CLOSED && s.blink_close < 0) {
            s.blink_close = now;
        }
        if (s.closeup_full >= 0 && p.closeup == 0 && s.closeup_back < 0) {
            s.closeup_back = now;
        }
        s.lift_max = p.lift > s.lift_max ? p.lift : s.lift_max;
        if (p.antenna == BUDDY_ANTENNA_OVERLOAD && p.lift == BUDDY_MAX_HOVER && s.overload_up < 0) {
            s.overload_up = now;
        }
        if (s.overload_up >= 0 && p.lift == 0 && s.landed < 0) {
            s.landed = now;
        }
    }
    return s;
}

void test_activity_water_short_circuits_him(void)
{
    const story_t s = play(BUDDY_STATE_WATER);
    CHECK(s.first_full_glass >= 0);
    CHECK_EQ(s.glass_at_mouth_min, 1);                // he drinks it all, at his mouth
    CHECK(s.empty_glass > s.first_full_glass);
    CHECK(s.alert > s.empty_glass);                   // ...wait!
    CHECK(s.zap > s.alert);                           // short circuit
    CHECK(s.glass_gone > s.empty_glass);              // the glass is put away
    CHECK(s.say > s.zap);                             // NOTE TO SELF
    CHECK(strcmp(buddy_say_text(BUDDY_SAY_WATER), "NOTE TO SELF: ROBOTS + WATER = NO") == 0);
}

void test_activity_sneeze_blows_his_head_off(void)
{
    const story_t s = play(BUDDY_STATE_SNEEZE);
    CHECK_EQ(s.pop_max, BUDDY_MAX_POP);
    CHECK(s.pop_back > 0);          // it comes back down
    CHECK(s.neck_poof >= s.pop_back); // with a puff of dust where it lands
    CHECK(s.say < 0);
}

void test_activity_stare_pushes_in_and_he_loses(void)
{
    const story_t s = play(BUDDY_STATE_STARE);
    CHECK(s.closeup_full > 0);
    CHECK(s.blink_close > s.closeup_full); // he blinks, face to face
    CHECK(s.closeup_back > s.blink_close); // back to his place
    CHECK(s.say >= s.closeup_back);        // BEST OF THREE? once he is back
}

void test_activity_meditation_floats_until_a_notification(void)
{
    const story_t s = play(BUDDY_STATE_MEDITATE);
    CHECK_EQ(s.lift_max, BUDDY_MAX_HOVER);
    CHECK(s.say > 0 && s.say < s.overload_up); // OMMM while he hovers
    CHECK(s.overload_up > 0);                  // ping!
    CHECK(s.landed > s.overload_up);           // down he goes
    CHECK(s.feet_poof >= s.landed);            // thud
}

// The render loop's way, with two alternating buffers repaired from dirty rectangles: every activity in turn, with
// taps hammering it. Every buffer must equal a full redraw, and nothing is drawn outside the stage (not even the
// stare's close-up); afterwards he stands on his place again.
static gfx_color_t s_strip[16 * 1024];
static gfx_color_t s_buf[2][W * H];
static gfx_color_t s_ref[W * H];

void test_activity_redraws_match_full_redraw_on_the_stage(void)
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
    buddy_brain_init(&brain, 0, 9);
    buddy_brain_set_mood(&brain, BUDDY_MOOD_HAPPY, 0, 0);
    int back = 0, frames = 0, bad = 0, max_area = 0, act = 0, outside = 0;
    uint32_t start = 500;
    bool closeup_seen = false;
    const uint32_t end = 500 + BUDDY_WATER_MS + BUDDY_SNEEZE_MS + BUDDY_STARE_MS + BUDDY_MEDITATE_MS + BUDDY_ZDMW_MS +
                         BUDDY_FOCUS_MS + N_ACTS * 600 + 1000;
    for (uint32_t t = 0, k = 0; t < end; k++) {
        if (act < N_ACTS && t >= start && !buddy_brain_busy(&brain, t)) {
            CHECK(buddy_brain_event(&brain, k_acts[act].ev, t));
            start = t + k_acts[act].ms + 600;
            act++;
        }
        if (k % 9 == 0) {
            buddy_brain_event(&brain, BUDDY_EVENT_POKE, t); // ignored while he is busy
        }
        buddy_pose_t pose;
        uint32_t next = buddy_brain_tick(&brain, t, &pose);
        closeup_seen |= pose.closeup == 255;
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
                const int area = gfx_dirty_area(&redraw);
                max_area = area > max_area ? area : max_area;
            }
            back ^= 1;
        }
        t += (next < 40 ? next : 40) + (k % 5 == 0 ? 3 : 0);
    }
    CHECK_EQ(act, N_ACTS);
    CHECK(closeup_seen);
    CHECK(frames > 300);
    CHECK_EQ(bad, 0);
    CHECK_EQ(outside, 0); // the menu's panel is never touched
    printf("    activities: %d frames, largest redraw %d px (stage %d px)\n", frames, max_area,
           layout.stage.w * layout.stage.h);
    CHECK(view.scale == view.home_scale && view.x0 == view.home_x0 && view.y0 == view.home_y0);
}
