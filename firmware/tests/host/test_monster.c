// The Monster Energy sequence: phase order and timing, event handling while it runs, the story it tells, and that
// drawing it incrementally on two alternating frame buffers always matches a full redraw.
#include <string.h>

#include "buddy_brain.h"
#include "buddy_monster.h"
#include "buddy_view.h"
#include "gfx_repair.h"
#include "layout.h"
#include "test.h"

static const buddy_phase_t k_order[] = {BUDDY_PHASE_DISCOVER, BUDDY_PHASE_PREPARE, BUDDY_PHASE_CONNECT,
                                        BUDDY_PHASE_CHARGE, BUDDY_PHASE_OVERCHARGE, BUDDY_PHASE_FINISH};
static const uint32_t k_starts[] = {BUDDY_MONSTER_DISCOVER_AT, BUDDY_MONSTER_PREPARE_AT,
                                    BUDDY_MONSTER_CONNECT_AT, BUDDY_MONSTER_CHARGE_AT,
                                    BUDDY_MONSTER_OVERCHARGE_AT, BUDDY_MONSTER_FINISH_AT};
#define N_PHASES (sizeof(k_order) / sizeof(k_order[0]))

void test_monster_phases_run_in_order_then_idle(void)
{
    _Static_assert(BUDDY_MONSTER_MS >= 5000 && BUDDY_MONSTER_MS <= 7000, "Monster sequence length");
    for (unsigned i = 0; i < N_PHASES; i++) {
        CHECK_EQ(buddy_monster_phase(k_starts[i]), k_order[i]);
        if (k_starts[i] > 0) {
            CHECK_EQ(buddy_monster_phase(k_starts[i] - 1), k_order[i - 1]);
        }
    }
    CHECK_EQ(buddy_monster_phase(BUDDY_MONSTER_MS - 1), BUDDY_PHASE_FINISH);
    CHECK_EQ(buddy_monster_phase(BUDDY_MONSTER_MS), BUDDY_PHASE_NONE);

    // Through the brain, the way the render loop drives it (jump to each reported change), across a clock wrap.
    buddy_brain_t b;
    buddy_pose_t p;
    const uint32_t t0 = UINT32_MAX - 3000;
    buddy_brain_init(&b, t0 - 500, 5);
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_DRINK_MONSTER, t0));
    unsigned seen = 0;
    uint32_t t = t0;
    while (t - t0 < BUDDY_MONSTER_MS) {
        const uint32_t wait = buddy_brain_tick(&b, t, &p);
        CHECK_EQ(p.state, BUDDY_STATE_MONSTER);
        CHECK(wait >= 1 && wait <= 50); // at least 20 frames per second, never a busy loop
        if (seen < N_PHASES && p.props.phase == k_order[seen]) {
            CHECK_EQ(t - t0, k_starts[seen]); // each phase is entered exactly on time
            seen++;
        }
        CHECK(seen > 0 && p.props.phase == k_order[seen - 1]); // never skips back or ahead
        t += wait;
    }
    CHECK_EQ(seen, N_PHASES);
    CHECK_EQ(t - t0, BUDDY_MONSTER_MS); // the last wait lands exactly on the end
    buddy_brain_tick(&b, t, &p);
    CHECK_EQ(p.state, BUDDY_STATE_IDLE);
    CHECK_EQ(p.props.phase, BUDDY_PHASE_NONE);
    CHECK_EQ(p.props.can, BUDDY_CAN_NONE);
    CHECK_EQ(p.tint, BUDDY_TINT_NORMAL);

    // A late tick (the render loop slept through the end) also lands in idle.
    buddy_brain_init(&b, 0, 5);
    buddy_brain_event(&b, BUDDY_EVENT_DRINK_MONSTER, 100);
    buddy_brain_tick(&b, 100 + BUDDY_MONSTER_MS + 2500, &p);
    CHECK_EQ(p.state, BUDDY_STATE_IDLE);
}

void test_monster_ignores_events_until_finished(void)
{
    buddy_brain_t b;
    buddy_pose_t p;
    buddy_brain_init(&b, 0, 9);

    // Starts from a touch reaction too.
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_POKE, 1000));
    buddy_brain_tick(&b, 1200, &p);
    CHECK_EQ(p.state, BUDDY_STATE_SURPRISED);
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_DRINK_MONSTER, 1200));
    buddy_brain_tick(&b, 1200, &p);
    CHECK_EQ(p.props.phase, BUDDY_PHASE_DISCOVER);

    // Hammering the screen and the button mid-show changes nothing: the sequence keeps its own clock.
    for (uint32_t t = 1300; t < 1200 + BUDDY_MONSTER_MS; t += 97) {
        CHECK(!buddy_brain_event(&b, BUDDY_EVENT_POKE, t));
        CHECK(!buddy_brain_event(&b, BUDDY_EVENT_DRINK_MONSTER, t));
        buddy_brain_tick(&b, t, &p);
        CHECK_EQ(p.state, BUDDY_STATE_MONSTER);
        CHECK_EQ(p.props.phase, buddy_monster_phase(t - 1200));
    }
    CHECK(!buddy_brain_event(&b, (buddy_event_t)99, 2000)); // unknown event ids are ignored

    // Right at the end the robot listens again, even before the next tick.
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_POKE, 1200 + BUDDY_MONSTER_MS));
    buddy_brain_tick(&b, 1200 + BUDDY_MONSTER_MS, &p);
    CHECK_EQ(p.state, BUDDY_STATE_SURPRISED);
    CHECK(buddy_brain_event(&b, (buddy_event_t)99, 1200 + BUDDY_MONSTER_MS) == false);
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_DRINK_MONSTER, 1300 + BUDDY_MONSTER_MS)); // and drinks again
}

// The beats the user asked for, checked on the poses: can, cable, connection, charging, overcharge, finish.
void test_monster_tells_the_story(void)
{
    buddy_pose_t p;
    bool can_landed = false, alerted = false, coiled = false, plugged_can = false, plugged_head = false;
    bool bolt_eyes = false, overload = false, zapped = false, crushed = false;
    int prev_battery = 0, prev_level = BUDDY_CAN_LEVELS - 1, finish_hops = 0, prev_lift = 0;
    for (uint32_t t = 0; t < BUDDY_MONSTER_MS; t += 5) {
        buddy_monster_pose(t, &p);
        const buddy_props_t *pr = &p.props;
        CHECK(p.lift <= BUDDY_MAX_LIFT && p.shake >= -1 && p.shake <= 1 && p.look >= -1 && p.look <= 1);
        CHECK(p.wobble >= -1 && p.wobble <= 1);
        CHECK(pr->bolts <= BUDDY_MAX_BOLTS && pr->battery <= BUDDY_BATTERY_FRAMES);
        CHECK(pr->can_level < BUDDY_CAN_LEVELS);
        can_landed |= pr->phase == BUDDY_PHASE_DISCOVER && pr->can == BUDDY_CAN_FULL && pr->can_drop == 0;
        alerted |= pr->alert;
        coiled |= pr->cable == BUDDY_CABLE_COILED;
        plugged_can |= pr->cable == BUDDY_CABLE_CAN_TO_HAND;

        // The cable stays visible from the moment it appears until the can disappears.
        if (pr->phase >= BUDDY_PHASE_PREPARE && pr->can != BUDDY_CAN_NONE) {
            CHECK(pr->cable != BUDDY_CABLE_NONE);
        }
        switch (pr->phase) {
        case BUDDY_PHASE_DISCOVER:
            CHECK_EQ(pr->cable, BUDDY_CABLE_NONE);
            break;
        case BUDDY_PHASE_CHARGE:
            plugged_head = true;
            CHECK_EQ(pr->cable, BUDDY_CABLE_CAN_TO_PORT);
            CHECK(pr->energized && pr->port && pr->bolts >= 1 && p.tint == BUDDY_TINT_GLOW);
            CHECK(pr->battery >= prev_battery && pr->can_level <= prev_level); // fills while the can drains
            prev_battery = pr->battery;
            prev_level = pr->can_level;
            break;
        case BUDDY_PHASE_OVERCHARGE:
            CHECK_EQ(pr->cable, BUDDY_CABLE_CAN_TO_PORT);
            CHECK(pr->energized && pr->bolts == BUDDY_MAX_BOLTS && pr->can_level == 0);
            CHECK(pr->battery >= BUDDY_BATTERY_OVER);
            bolt_eyes |= p.eyes == BUDDY_EYES_BOLT;
            overload |= p.antenna == BUDDY_ANTENNA_OVERLOAD;
            zapped |= p.tint == BUDDY_TINT_ZAP;
            break;
        case BUDDY_PHASE_FINISH:
            CHECK(!pr->energized && pr->cable != BUDDY_CABLE_CAN_TO_PORT);
            if (t >= BUDDY_MONSTER_FINISH_AT + 120) {
                CHECK(pr->can != BUDDY_CAN_FULL);
            }
            crushed |= pr->can == BUDDY_CAN_CRUSHED;
            finish_hops += prev_lift == 0 && p.lift > 0;
            break;
        default:
            break;
        }
        prev_lift = p.lift;
    }
    CHECK(can_landed && alerted && coiled && plugged_can && plugged_head);
    CHECK_EQ(prev_battery, 1 + BUDDY_BATTERY_SEGMENTS); // full by the end of charging
    CHECK_EQ(prev_level, 0);                           // can empty
    CHECK(bolt_eyes && overload && zapped && crushed);
    CHECK(finish_hops >= 3);

    // The last frame already looks like the end: no props left, glowing grin on the ground.
    buddy_monster_pose(BUDDY_MONSTER_MS - 1, &p);
    CHECK(p.props.can == BUDDY_CAN_NONE && p.props.cable == BUDDY_CABLE_NONE && p.props.bolts == 0);
    CHECK(!p.props.port && p.props.battery == 0 && p.lift == 0);
    CHECK_EQ(p.mouth, BUDDY_MOUTH_GRIN);
}

// --- Rendering ---------------------------------------------------------------------------------------------------

enum { W = 1024, H = 600, ALIGN = 32 };

static gfx_color_t s_buf[2][W * H], s_ref[W * H], s_strip[16 * 1024];

typedef struct {
    int frames, full_frames, max_area, bad_frames;
    uint32_t max_at; // ms into the sequence
    long long area_sum;
} render_stats_t;

// Plays idle, then the whole sequence (with touches hammering it), then idle again, like the render loop does:
// two alternating buffers repaired from dirty rectangles, each compared with a full redraw.
void test_monster_redraws_match_full_redraw(void)
{
    static gfx_scene_t scene;
    static buddy_view_t view;
    static gfx_dirty_t changes, redraw;
    static gfx_repair_t repair;
    static layout_t layout;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(buddy_view_init(&view, &scene, layout.stage, 10, 0));
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_repair_init(&repair, W, H, ALIGN);
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
    memset(s_buf, 0, sizeof(s_buf));

    gfx_dirty_t all;
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    const gfx_surface_t ref = {s_ref, W, H, W};

    buddy_brain_t brain;
    buddy_brain_init(&brain, 0, 3);
    render_stats_t st = {0};
    int back = 0, drinks = 0;
    bool started = false;
    uint32_t began = 0; // when the sequence actually started (the first frame at or after `start`)
    const uint32_t start = 1500, end = start + BUDDY_MONSTER_MS + 1500;
    for (uint32_t t = 0, k = 0; t < end; k++) {
        if (!started && t >= start) {
            // Started from the menu or the companion; the robot itself still gets poked.
            CHECK(buddy_brain_event(&brain, BUDDY_EVENT_DRINK_MONSTER, t));
            started = true;
            began = t;
        }
        if (started && t - began < BUDDY_MONSTER_MS && k % 7 == 0) {
            drinks += buddy_brain_event(&brain, k % 2 ? BUDDY_EVENT_POKE : BUDDY_EVENT_DRINK_MONSTER, t);
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
            st.bad_frames += memcmp(s_buf[back], s_ref, sizeof(s_ref)) != 0;
            const int area = gfx_dirty_area(&redraw);
            st.frames++;
            if (st.frames > 2) { // the first two frames are the boot-time full redraws
                st.full_frames += area == W * H;
                if (area > st.max_area) {
                    st.max_area = area;
                    st.max_at = t - began;
                }
                st.area_sum += area;
            }
            back ^= 1;
        }
        t += next + (k % 5 == 0 ? 7 : 0); // rendering takes time too
    }
    CHECK(started);
    CHECK_EQ(drinks, 0);
    CHECK(st.frames > 100);
    CHECK_EQ(st.bad_frames, 0);
    CHECK_EQ(st.full_frames, 0);
    printf("    monster: %d frames, redraw avg %lld max %d px at %d ms (screen %d)\n", st.frames,
           st.area_sum / (st.frames - 2), st.max_area, (int)st.max_at, W * H);
    CHECK(st.max_area <= W * H / 4); // stays incremental: no large redraws while the screen is lit
}
