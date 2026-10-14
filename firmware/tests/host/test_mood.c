// Moods: names, switching, reactions playing on top and handing back, timed moods, pausing (Office Mode), what each
// mood visibly does, and that drawing them incrementally on two alternating buffers matches a full redraw.
#include <string.h>

#include "buddy_brain.h"
#include "buddy_monster.h"
#include "buddy_view.h"
#include "gfx_repair.h"
#include "layout.h"
#include "marquee.h"
#include "test.h"

static bool has_particle(const buddy_pose_t *p, buddy_part_kind_t kind)
{
    for (int i = 0; i < BUDDY_PARTICLES; i++) {
        if (p->props.particle[i].kind == kind) {
            return true;
        }
    }
    return false;
}

static bool no_mood_props(const buddy_pose_t *p)
{
    return !p->props.cig && !has_particle(p, BUDDY_PART_SMOKE) && !has_particle(p, BUDDY_PART_STEAM) &&
           !has_particle(p, BUDDY_PART_Z) && !has_particle(p, BUDDY_PART_SWEAT) &&
           !has_particle(p, BUDDY_PART_SPARKLE) && !has_particle(p, BUDDY_PART_ANGER);
}

void test_mood_names_round_trip(void)
{
    for (int m = 0; m < BUDDY_MOOD_COUNT; m++) {
        buddy_mood_t back = BUDDY_MOOD_COUNT;
        CHECK(buddy_mood_parse(buddy_mood_name((buddy_mood_t)m), &back));
        CHECK_EQ(back, m);
    }
    CHECK(strcmp(buddy_mood_name(BUDDY_MOOD_RELAXED), "relaxed") == 0);
    CHECK(strcmp(buddy_mood_name(BUDDY_MOOD_PEUKIE), "peukie") == 0);
    buddy_mood_t out = BUDDY_MOOD_HAPPY;
    CHECK(!buddy_mood_parse("grumpy", &out));
    CHECK(!buddy_mood_parse("Idle", &out));
    CHECK(!buddy_mood_parse("", &out));
    CHECK(!buddy_mood_parse(NULL, &out));
    CHECK_EQ(out, BUDDY_MOOD_HAPPY); // untouched on failure
}

void test_mood_switches_cleanly(void)
{
    buddy_brain_t b;
    buddy_pose_t p;
    buddy_brain_init(&b, 1000, 9);
    CHECK_EQ(buddy_brain_mood(&b, 1000), BUDDY_MOOD_IDLE);
    buddy_brain_tick(&b, 1000, &p);
    CHECK(no_mood_props(&p)); // idle: the original Buddy

    buddy_brain_set_mood(&b, BUDDY_MOOD_PEUKIE, 0, 2000);
    buddy_brain_tick(&b, 2000, &p);
    CHECK(p.props.cig && p.props.hand_from == BUDDY_HAND_REST);
    CHECK_EQ(p.eyes, BUDDY_EYES_LIDDED);

    // Choosing the same mood again does not restart it; a different one starts from its beginning.
    const uint32_t cycle = b.cycle_start;
    buddy_brain_set_mood(&b, BUDDY_MOOD_PEUKIE, 0, 2500);
    CHECK_EQ(b.cycle_start, cycle);
    buddy_brain_set_mood(&b, BUDDY_MOOD_ANGRY, 0, 3000);
    CHECK_EQ(b.mood_start, 3000);
    buddy_brain_tick(&b, 3000, &p);
    CHECK(!p.props.cig); // nothing of the smoke break is left
    CHECK_EQ(p.eyes, BUDDY_EYES_ANGRY);

    // Rapid changes: every switch shows the new mood right away.
    static const buddy_mood_t k_order[] = {BUDDY_MOOD_SLEEPY, BUDDY_MOOD_HAPPY, BUDDY_MOOD_WORKING,
                                           BUDDY_MOOD_PEUKIE, BUDDY_MOOD_RELAXED, BUDDY_MOOD_IDLE};
    static const buddy_eyes_t k_eyes[] = {BUDDY_EYES_HALF,   BUDDY_EYES_HAPPY,  BUDDY_EYES_FOCUS,
                                          BUDDY_EYES_LIDDED, BUDDY_EYES_LIDDED, BUDDY_EYES_OPEN};
    for (unsigned i = 0; i < sizeof(k_order) / sizeof(k_order[0]); i++) {
        const uint32_t t = 3100 + 40 * i;
        buddy_brain_set_mood(&b, k_order[i], 0, t);
        buddy_brain_tick(&b, t, &p);
        CHECK_EQ(p.state, BUDDY_STATE_IDLE);
        CHECK_EQ(p.eyes, k_eyes[i]);
    }
    CHECK(no_mood_props(&p));

    buddy_brain_set_mood(&b, BUDDY_MOOD_COUNT, 0, 4000); // not a mood: ignored
    CHECK_EQ(buddy_brain_mood(&b, 4000), BUDDY_MOOD_IDLE);
}

void test_mood_resumes_after_reactions(void)
{
    buddy_brain_t b;
    buddy_pose_t p;
    buddy_brain_init(&b, 0, 4);
    buddy_brain_set_mood(&b, BUDDY_MOOD_WORKING, 0, 100);

    // Surprise! (a tap, or a message arriving) interrupts the mood, then he gets back to work.
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_POKE, 1000));
    buddy_brain_tick(&b, 1100, &p);
    CHECK_EQ(p.state, BUDDY_STATE_SURPRISED);
    CHECK(no_mood_props(&p));
    CHECK_EQ(buddy_brain_mood(&b, 1100), BUDDY_MOOD_WORKING);
    const uint32_t back = 1000 + BUDDY_SURPRISE_MS + BUDDY_HAPPY_HOPS * BUDDY_HAPPY_HOP_MS;
    buddy_brain_tick(&b, back, &p);
    CHECK_EQ(p.state, BUDDY_STATE_IDLE);
    CHECK(p.eyes == BUDDY_EYES_FOCUS || p.eyes == BUDDY_EYES_SQUEEZE);

    // Monster during a smoke break: the cigarette goes while he drinks and comes back after.
    buddy_brain_set_mood(&b, BUDDY_MOOD_PEUKIE, 0, back);
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_DRINK_MONSTER, back + 500));
    buddy_brain_tick(&b, back + 3000, &p);
    CHECK_EQ(p.state, BUDDY_STATE_MONSTER);
    CHECK(!p.props.cig);
    // A mood change during the sequence waits for its end; the sequence is not cut short.
    buddy_brain_set_mood(&b, BUDDY_MOOD_SLEEPY, 0, back + 3500);
    buddy_brain_tick(&b, back + 4000, &p);
    CHECK_EQ(p.state, BUDDY_STATE_MONSTER);
    buddy_brain_tick(&b, back + 500 + BUDDY_MONSTER_MS, &p);
    CHECK_EQ(p.state, BUDDY_STATE_IDLE);
    CHECK_EQ(p.tint, BUDDY_TINT_DROWSY);
    CHECK_EQ(p.antenna, BUDDY_ANTENNA_DIM);
}

void test_mood_timed_returns_to_idle(void)
{
    buddy_brain_t b;
    buddy_pose_t p;
    const uint32_t t0 = UINT32_MAX - 2000; // across the clock wrap
    buddy_brain_init(&b, t0, 2);
    buddy_brain_set_mood(&b, BUDDY_MOOD_HAPPY, 5000, t0);
    uint32_t t = t0;
    while (t - t0 < 5000) {
        CHECK_EQ(buddy_brain_mood(&b, t), BUDDY_MOOD_HAPPY);
        const uint32_t wait = buddy_brain_tick(&b, t, &p);
        CHECK(wait >= 1 && t - t0 + wait <= 5000); // wakes up in time to end it
        t += wait;
    }
    CHECK_EQ(buddy_brain_mood(&b, t0 + 5000), BUDDY_MOOD_IDLE);
    buddy_brain_tick(&b, t0 + 5000, &p);
    CHECK(no_mood_props(&p));

    // Choosing the current mood again with a duration only times it; without one it stays.
    buddy_brain_set_mood(&b, BUDDY_MOOD_ANGRY, 0, t0 + 6000);
    buddy_brain_set_mood(&b, BUDDY_MOOD_ANGRY, 1000, t0 + 6500);
    CHECK_EQ(buddy_brain_mood(&b, t0 + 7499), BUDDY_MOOD_ANGRY);
    CHECK_EQ(buddy_brain_mood(&b, t0 + 7500), BUDDY_MOOD_IDLE);
    buddy_brain_set_mood(&b, BUDDY_MOOD_ANGRY, 1000, t0 + 8000);
    buddy_brain_set_mood(&b, BUDDY_MOOD_ANGRY, 0, t0 + 8100);
    CHECK_EQ(buddy_brain_mood(&b, t0 + 60000), BUDDY_MOOD_ANGRY);
}

void test_mood_survives_a_pause(void)
{
    // Office Mode pauses him (no ticks) and brings him back with buddy_brain_rest(); a mood chosen meanwhile shows
    // when he returns, from the start of its loop.
    buddy_brain_t b;
    buddy_pose_t p;
    buddy_brain_init(&b, 0, 6);
    buddy_brain_set_mood(&b, BUDDY_MOOD_SLEEPY, 0, 1000);
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_DRINK_MONSTER, 2000)); // dropped by the pause
    buddy_brain_set_mood(&b, BUDDY_MOOD_PEUKIE, 0, 30000);
    buddy_brain_rest(&b, 600000);
    buddy_brain_tick(&b, 600000, &p);
    CHECK_EQ(p.state, BUDDY_STATE_IDLE);
    CHECK(p.props.cig);
    CHECK_EQ(b.cycle_start, 600000);
}

// Plays a mood for a minute (ticking as the render loop does) and records what it showed.
typedef struct {
    bool cig, drag, at_mouth, kinds[BUDDY_PART_COUNT], fury, nod, wide, squeeze, shake, wobble, hop, tongue;
    bool eyes[BUDDY_EYES_COUNT], mouths[BUDDY_MOUTH_COUNT];
    int bounces; // how often he rose or sank
    uint32_t max_wait, min_wait;
} seen_t;

static seen_t play(buddy_mood_t mood)
{
    seen_t s = {.min_wait = UINT32_MAX};
    buddy_brain_t b;
    buddy_pose_t p;
    buddy_brain_init(&b, 0, 11);
    buddy_brain_set_mood(&b, mood, 0, 0);
    for (uint32_t t = 0, lift = 0; t < 60000;) {
        const uint32_t wait = buddy_brain_tick(&b, t, &p);
        s.bounces += p.lift != lift;
        lift = p.lift;
        s.mouths[p.mouth] = true;
        s.cig |= p.props.cig;
        s.drag |= p.props.ember == 2;
        s.at_mouth |= p.props.hand_to == BUDDY_HAND_MOUTH && p.props.hand_from == BUDDY_HAND_MOUTH;
        for (int i = 0; i < BUDDY_PARTICLES; i++) {
            s.kinds[p.props.particle[i].kind] = true;
        }
        s.fury |= p.tint == BUDDY_TINT_FURY;
        s.nod |= p.nod > 0;
        s.wide |= p.eyes == BUDDY_EYES_WIDE;
        s.squeeze |= p.eyes == BUDDY_EYES_SQUEEZE;
        s.shake |= p.shake != 0;
        s.wobble |= p.wobble != 0;
        s.hop |= p.lift >= 3;
        s.tongue |= p.mouth == BUDDY_MOUTH_TONGUE;
        s.eyes[p.eyes] = true;
        s.max_wait = wait > s.max_wait ? wait : s.max_wait;
        s.min_wait = wait < s.min_wait ? wait : s.min_wait;
        t += wait;
    }
    return s;
}

void test_mood_loops_show_their_character(void)
{
    const seen_t idle = play(BUDDY_MOOD_IDLE);

    const seen_t relaxed = play(BUDDY_MOOD_RELAXED);
    CHECK(!relaxed.cig && !relaxed.kinds[BUDDY_PART_SMOKE]); // no smoke break: that is Peukie
    CHECK(relaxed.eyes[BUDDY_EYES_LIDDED] && relaxed.mouths[BUDDY_MOUTH_SMILE] && !relaxed.mouths[BUDDY_MOUTH_SMIRK]);
    CHECK(relaxed.bounces > 0 && relaxed.bounces * 3 <= idle.bounces * 2); // calmer than idle

    const seen_t peukie = play(BUDDY_MOOD_PEUKIE);
    CHECK(peukie.cig && peukie.drag && peukie.at_mouth && peukie.kinds[BUDDY_PART_SMOKE]);
    CHECK(peukie.eyes[BUDDY_EYES_HAPPY]); // blissful while taking a drag

    const seen_t angry = play(BUDDY_MOOD_ANGRY);
    CHECK(angry.kinds[BUDDY_PART_STEAM] && angry.kinds[BUDDY_PART_ANGER] && angry.fury && angry.shake);
    CHECK(angry.eyes[BUDDY_EYES_ANGRY] && !angry.cig);

    const seen_t sleepy = play(BUDDY_MOOD_SLEEPY);
    CHECK(sleepy.kinds[BUDDY_PART_Z] && sleepy.nod && sleepy.wide && sleepy.eyes[BUDDY_EYES_CLOSED]);
    CHECK(!sleepy.shake);

    const seen_t working = play(BUDDY_MOOD_WORKING);
    CHECK(working.kinds[BUDDY_PART_SWEAT] && working.squeeze && working.tongue && working.eyes[BUDDY_EYES_FOCUS]);

    const seen_t happy = play(BUDDY_MOOD_HAPPY);
    CHECK(happy.kinds[BUDDY_PART_SPARKLE] && happy.hop && happy.wobble && happy.eyes[BUDDY_EYES_HAPPY]);

    // None of them busy-loops, and none of them freezes for long.
    const seen_t all[] = {idle, relaxed, peukie, angry, sleepy, working, happy};
    for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        CHECK(all[i].min_wait >= 1);
        CHECK(all[i].max_wait <= 1000);
    }
}

// --- Rendering ---------------------------------------------------------------------------------------------------

enum { W = 1024, H = 600, ALIGN = 32 };

static gfx_color_t s_buf[2][W * H], s_ref[W * H], s_strip[16 * 1024];

// Every mood in turn, switched fast and slow, with taps and a Monster sequence on top, as the render loop draws it:
// two alternating buffers repaired from dirty rectangles, each compared with a full redraw.
void test_mood_redraws_match_full_redraw(void)
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
    buddy_brain_init(&brain, 0, 8);
    int back = 0, frames = 0, bad = 0, full = 0, max_area = 0;
    const gfx_rect_t stage = layout.stage;
    for (uint32_t t = 0, k = 0; t < 140000; k++) {
        // 12 s per mood, then a burst of quick changes; a tap now and then, one Monster sequence during Peukie.
        const uint32_t slot = t / 12000;
        if (slot < BUDDY_MOOD_COUNT) {
            buddy_brain_set_mood(&brain, (buddy_mood_t)slot, 0, t);
        } else if (k % 3 == 0) {
            buddy_brain_set_mood(&brain, (buddy_mood_t)(k % BUDDY_MOOD_COUNT), 0, t);
        }
        if (k % 97 == 0) {
            buddy_brain_event(&brain, BUDDY_EVENT_POKE, t);
        }
        if (slot == BUDDY_MOOD_PEUKIE && t % 12000 > 6000 && brain.state == BUDDY_STATE_IDLE) {
            buddy_brain_event(&brain, BUDDY_EVENT_DRINK_MONSTER, t);
        }
        buddy_pose_t pose;
        uint32_t next = buddy_brain_tick(&brain, t, &pose);
        const uint32_t view_next = buddy_view_apply(&view, &pose, t);
        next = view_next < next ? view_next : next;

        // Everything he draws stays on the stage, and the floating effects stay clear of the LED sign above him
        // (placed as the firmware does), so a scrolling message is never redrawn together with them.
        const gfx_rect_t sign = {stage.x + (stage.w - MARQUEE_W) / 2, stage.y + 22, MARQUEE_W, MARQUEE_H};
        for (int i = 0; i < BUDDY_PARTICLES; i++) {
            const gfx_rect_t r = gfx_sprite_bounds(&view.part[i]);
            CHECK(gfx_rect_is_empty(r) || gfx_rect_contains_rect(stage, r));
            CHECK(gfx_rect_is_empty(gfx_rect_intersect(r, sign)));
        }
        for (int i = 0; i < 2; i++) {
            const gfx_rect_t r = gfx_sprite_bounds(&view.rig[i]);
            CHECK(gfx_rect_is_empty(r) || gfx_rect_contains_rect(stage, r));
        }

        gfx_dirty_clear(&changes);
        gfx_scene_collect(&scene, &changes);
        if (changes.count > 0) {
            gfx_repair_next(&repair, &changes, &redraw);
            const gfx_surface_t dst = {s_buf[back], W, H, W};
            gfx_scene_render(&scene, &redraw, &dst);
            gfx_scene_render(&scene, &all, &ref);
            bad += memcmp(s_buf[back], s_ref, sizeof(s_ref)) != 0;
            const int area = gfx_dirty_area(&redraw);
            if (++frames > 2) { // the first two frames are the boot-time full redraws
                full += area == W * H;
                max_area = area > max_area ? area : max_area;
            }
            back ^= 1;
        }
        t += next + (k % 5 == 0 ? 7 : 0); // rendering takes time too
    }
    printf("    moods: %d frames, max redraw %d px (screen %d)\n", frames, max_area, W * H);
    CHECK(frames > 500);
    CHECK_EQ(bad, 0);
    CHECK_EQ(full, 0);
    CHECK(max_area <= W * H / 4); // particles and faces stay small, incremental redraws
}
