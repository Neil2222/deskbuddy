#include "buddy_monster.h"

#include "buddy_motion.h"

// The sequence is animated on a 50 ms frame clock (20 frames per second, the pace of pixel-art animation). Moving
// on every 20 ms tick instead would present up to the full 33 Hz panel rate, and that much rendering measurably
// lengthens the scanout's PSRAM reads (bounce fills). Every beat time below is a multiple of FRAME_MS.
#define FRAME_MS 50u
#define CAN_DROP_MAX 26u // the can falls in from this many art pixels above the ground
#define CAN_FALL_MS 300u
#define FLOW_MS 50u      // ms per art pixel of energy flow while charging
#define CHARGE_MS (BUDDY_MONSTER_OVERCHARGE_AT - BUDDY_MONSTER_CHARGE_AT)
#define FINISH_HOP_MS 400u
#define FINISH_HOPS 3u

static const struct {
    buddy_phase_t phase;
    uint32_t at;
} k_phases[] = {
    {BUDDY_PHASE_DISCOVER, BUDDY_MONSTER_DISCOVER_AT},
    {BUDDY_PHASE_PREPARE, BUDDY_MONSTER_PREPARE_AT},
    {BUDDY_PHASE_CONNECT, BUDDY_MONSTER_CONNECT_AT},
    {BUDDY_PHASE_CHARGE, BUDDY_MONSTER_CHARGE_AT},
    {BUDDY_PHASE_OVERCHARGE, BUDDY_MONSTER_OVERCHARGE_AT},
    {BUDDY_PHASE_FINISH, BUDDY_MONSTER_FINISH_AT},
};
#define N_PHASES (sizeof(k_phases) / sizeof(k_phases[0]))

buddy_phase_t buddy_monster_phase(uint32_t t)
{
    if (t >= BUDDY_MONSTER_MS) {
        return BUDDY_PHASE_NONE;
    }
    buddy_phase_t p = BUDDY_PHASE_NONE;
    for (unsigned i = 0; i < N_PHASES && t >= k_phases[i].at; i++) {
        p = k_phases[i].phase;
    }
    return p;
}

// Trembling: a short repeating sideways pattern, one step every `step_ms`.
static int8_t jitter(uint32_t t, uint32_t step_ms)
{
    static const int8_t k_pattern[] = {0, 1, 0, -1, 1, -1, 0, 1};
    return k_pattern[(t / step_ms) % sizeof(k_pattern)];
}

static void hand(buddy_props_t *pr, buddy_hand_t from, buddy_hand_t to, uint32_t t, uint32_t duration)
{
    pr->hand_from = from;
    pr->hand_to = to;
    pr->hand_mix = buddy_ease(t, duration);
}

static void fx(buddy_props_t *pr, int slot, buddy_fx_kind_t kind, buddy_spot_t at, uint32_t t, uint32_t frame_ms,
               uint8_t frames)
{
    const uint32_t frame = t / frame_ms;
    if (frame < frames) {
        pr->fx[slot] = (buddy_fx_t){kind, at, (uint8_t)frame};
    }
}

// The can drops in and he notices it: glance, look back at you, double take, "!", hop, excited wiggle.
static void discover(uint32_t t, buddy_pose_t *p)
{
    buddy_props_t *pr = &p->props;
    if (t < CAN_FALL_MS) {
        const uint32_t left = CAN_FALL_MS - t; // accelerating fall: height ~ (time left)^2
        pr->can_drop = (uint8_t)((CAN_DROP_MAX * left * left + CAN_FALL_MS * CAN_FALL_MS / 2) /
                                 (CAN_FALL_MS * CAN_FALL_MS));
    } else {
        pr->can_drop = buddy_hop(t - CAN_FALL_MS, 120, 1); // little bounce on landing
        fx(pr, 0, BUDDY_FX_POOF, BUDDY_SPOT_CAN_BASE, t - CAN_FALL_MS, 100, BUDDY_POOF_FRAMES);
    }

    if (t >= 350 && t < 500) {
        p->look = 1; // glance
    } else if (t >= 550) {
        p->look = 1; // double take
    }
    if (t >= 550 && t < 900) {
        pr->alert = t < 1000;
        p->eyes = BUDDY_EYES_WIDE;
        p->mouth = BUDDY_MOUTH_OPEN;
        p->antenna = BUDDY_ANTENNA_EXCITED;
        p->lift = buddy_hop(t - 550, 350, 3);
    } else if (t >= 900) {
        pr->alert = t < 1000;
        p->eyes = BUDDY_EYES_HAPPY;
        p->mouth = BUDDY_MOUTH_GRIN;
        p->antenna = BUDDY_ANTENNA_EXCITED;
        p->shake = (int8_t)(((t - 900) / 100) & 1u ? 1 : -1); // can't wait
    }
}

// Produces a coiled cable ("ta-da"), reaches over and plugs one end into the can, brings the other end back.
static void prepare(uint32_t t, buddy_pose_t *p)
{
    buddy_props_t *pr = &p->props;
    p->look = 1;
    p->antenna = BUDDY_ANTENNA_EXCITED;
    if (t < 450) {
        hand(pr, BUDDY_HAND_HOME, BUDDY_HAND_SHOW, t, 300);
        pr->cable = BUDDY_CABLE_COILED;
        p->eyes = BUDDY_EYES_HAPPY;
        p->mouth = BUDDY_MOUTH_SMILE;
        if (t >= 300) {
            fx(pr, 0, BUDDY_FX_SPARK, BUDDY_SPOT_HAND, t - 300, 50, BUDDY_SPARK_FRAMES);
        }
    } else if (t < 800) {
        hand(pr, BUDDY_HAND_SHOW, BUDDY_HAND_CAN, t - 450, 350);
        pr->cable = BUDDY_CABLE_HELD;
        p->eyes = BUDDY_EYES_OPEN; // concentrating
    } else {
        // Click: the first plug is in the can. Then the hand comes back with the other end.
        hand(pr, BUDDY_HAND_CAN, BUDDY_HAND_SHOW, t < 950 ? 0 : t - 950, 350);
        pr->cable = BUDDY_CABLE_CAN_TO_HAND;
        fx(pr, 0, BUDDY_FX_SPARK, BUDDY_SPOT_CAN_TOP, t - 800, 50, BUDDY_SPARK_FRAMES);
        p->eyes = t < 950 ? BUDDY_EYES_WIDE : BUDDY_EYES_HAPPY;
        p->mouth = t < 950 ? BUDDY_MOUTH_OPEN : BUDDY_MOUTH_SMILE;
        pr->port = t >= 1100; // the port on his head slides open
    }
}

// Squints in anticipation, plugs the cable into his head: click, jolt.
static void connect(uint32_t t, buddy_pose_t *p)
{
    buddy_props_t *pr = &p->props;
    pr->port = true;
    if (t < 250) {
        hand(pr, BUDDY_HAND_SHOW, BUDDY_HAND_PORT, t, 250);
        pr->cable = BUDDY_CABLE_CAN_TO_HAND;
        p->eyes = BUDDY_EYES_HALF;
        p->antenna = BUDDY_ANTENNA_EXCITED;
        return;
    }
    hand(pr, BUDDY_HAND_PORT, BUDDY_HAND_HOME, t < 300 ? 0 : t - 300, 200);
    pr->cable = BUDDY_CABLE_CAN_TO_PORT;
    fx(pr, 0, BUDDY_FX_SPARK, BUDDY_SPOT_PORT, t - 250, 50, BUDDY_SPARK_FRAMES);
    p->eyes = BUDDY_EYES_WIDE;
    p->mouth = BUDDY_MOUTH_OPEN;
    p->antenna = BUDDY_ANTENNA_EXCITED;
    p->wobble = t < 350 ? -1 : 0; // the click jolts his head
}

static void charging_cable(buddy_props_t *pr, uint16_t flow)
{
    pr->port = true;
    pr->cable = BUDDY_CABLE_CAN_TO_PORT;
    pr->energized = true;
    pr->flow = flow;
}

// Energy flows from the can into his head: the battery fills while the can drains.
static void charge(uint32_t t, buddy_pose_t *p)
{
    buddy_props_t *pr = &p->props;
    charging_cable(pr, (uint16_t)(t / FLOW_MS));
    p->tint = BUDDY_TINT_GLOW;
    p->eyes = BUDDY_EYES_WIDE;
    p->mouth = BUDDY_MOUTH_ZAP;
    p->antenna = BUDDY_ANTENNA_EXCITED;
    p->wobble = (int8_t)((t / 100) & 1u); // the head trembles on its neck; overcharge rattles it both ways

    const uint32_t filled = buddy_min_u32(BUDDY_BATTERY_SEGMENTS, t * (BUDDY_BATTERY_SEGMENTS + 1) / CHARGE_MS);
    pr->battery = (uint8_t)(1 + filled);
    pr->can_level = (uint8_t)(BUDDY_CAN_LEVELS - 1 - filled);
    pr->bolts = t < 400 ? 1 : 2;
    pr->strike = (uint16_t)(t / 100);
    if ((t / 100) % 4 != 3) { // sizzling port
        pr->fx[0] = (buddy_fx_t){BUDDY_FX_SPARK, BUDDY_SPOT_PORT, (uint8_t)((t / 100) % BUDDY_SPARK_FRAMES)};
    }
}

// Far too much: lightning eyes, strobing antenna, three negative "zap" flashes, vibrating in place. The flashes are
// kept to about three per second (they cover the whole robot).
static void overcharge(uint32_t t, buddy_pose_t *p)
{
    buddy_props_t *pr = &p->props;
    charging_cable(pr, (uint16_t)(CHARGE_MS / FLOW_MS + t / 25));
    p->tint = t % 250 < 100 && t < 600 ? BUDDY_TINT_ZAP : BUDDY_TINT_GLOW;
    p->eyes = BUDDY_EYES_BOLT;
    p->mouth = (t / 100) & 1u ? BUDDY_MOUTH_OPEN : BUDDY_MOUTH_ZAP;
    p->antenna = BUDDY_ANTENNA_OVERLOAD;
    p->wobble = jitter(t, FRAME_MS);

    pr->can_level = 0;
    pr->battery = (uint8_t)(BUDDY_BATTERY_OVER + ((t / 100) & 1u));
    pr->bolts = BUDDY_MAX_BOLTS;
    pr->strike = (uint16_t)(1000 + t / FRAME_MS);
    pr->fx[0] = (buddy_fx_t){BUDDY_FX_SPARK, BUDDY_SPOT_PORT, (uint8_t)((t / 50) % BUDDY_SPARK_FRAMES)};
    pr->fx[1] = (buddy_fx_t){BUDDY_FX_SPARK, BUDDY_SPOT_CAN_TOP, (uint8_t)((t / 100 + 1) % BUDDY_SPARK_FRAMES)};
}

// Pop: the plug flies out, the can crumples, three happy hops, the can vanishes, a last huge grin.
static void finish(uint32_t t, buddy_pose_t *p)
{
    buddy_props_t *pr = &p->props;
    const uint32_t hops_at = 150, poof_at = 1150, gone_at = 1250, settle_at = hops_at + FINISH_HOPS * FINISH_HOP_MS;
    p->tint = BUDDY_TINT_GLOW;
    p->antenna = BUDDY_ANTENNA_EXCITED;
    pr->port = t < 250;
    pr->battery = t < hops_at ? 1 + BUDDY_BATTERY_SEGMENTS : 0;
    pr->can_level = 0;
    pr->can = t < 100 ? BUDDY_CAN_FULL : BUDDY_CAN_CRUSHED;
    pr->cable = BUDDY_CABLE_DROPPED;
    fx(pr, 0, BUDDY_FX_SPARK, BUDDY_SPOT_PORT, t, 50, BUDDY_SPARK_FRAMES);

    if (t < hops_at) {
        p->eyes = BUDDY_EYES_WIDE;
        p->mouth = BUDDY_MOUTH_OPEN;
        p->wobble = t < 100 ? 1 : 0; // recoil
    } else {
        p->eyes = BUDDY_EYES_HAPPY;
        p->mouth = BUDDY_MOUTH_GRIN;
        if (t < settle_at) {
            p->lift = buddy_hop((t - hops_at) % FINISH_HOP_MS, FINISH_HOP_MS, BUDDY_MAX_LIFT);
            pr->bolts = ((t - hops_at) / 150) % 3 == 0 ? 1 : 0; // leftover crackle
            pr->strike = (uint16_t)(2000 + (t - hops_at) / 150);
        }
    }
    if (t >= poof_at) {
        fx(pr, 1, BUDDY_FX_POOF, BUDDY_SPOT_CAN, t - poof_at, 100, BUDDY_POOF_FRAMES);
    }
    if (t >= gone_at) {
        pr->can = BUDDY_CAN_NONE;
        pr->cable = BUDDY_CABLE_NONE;
    }
}

uint32_t buddy_monster_pose(uint32_t now, buddy_pose_t *p)
{
    const uint32_t t = now - now % FRAME_MS; // the frame on screen
    *p = (buddy_pose_t){
        .state = BUDDY_STATE_MONSTER,
        .eyes = BUDDY_EYES_OPEN,
        .mouth = BUDDY_MOUTH_NEUTRAL,
        .antenna = BUDDY_ANTENNA_CALM,
    };
    buddy_props_t *pr = &p->props;
    pr->phase = buddy_monster_phase(t);
    pr->can = BUDDY_CAN_FULL;
    pr->can_level = BUDDY_CAN_LEVELS - 1;

    switch (pr->phase) {
    case BUDDY_PHASE_DISCOVER:
        discover(t - BUDDY_MONSTER_DISCOVER_AT, p);
        break;
    case BUDDY_PHASE_PREPARE:
        prepare(t - BUDDY_MONSTER_PREPARE_AT, p);
        break;
    case BUDDY_PHASE_CONNECT:
        connect(t - BUDDY_MONSTER_CONNECT_AT, p);
        break;
    case BUDDY_PHASE_CHARGE:
        charge(t - BUDDY_MONSTER_CHARGE_AT, p);
        break;
    case BUDDY_PHASE_OVERCHARGE:
        overcharge(t - BUDDY_MONSTER_OVERCHARGE_AT, p);
        break;
    case BUDDY_PHASE_FINISH:
        finish(t - BUDDY_MONSTER_FINISH_AT, p);
        break;
    default:
        pr->can = BUDDY_CAN_NONE;
        return 1;
    }
    // The next frame, or the end of the sequence. The phase boundaries are frame boundaries, so this lands on them.
    return buddy_min_u32(FRAME_MS - now % FRAME_MS, BUDDY_MONSTER_MS - now);
}
