#include "buddy_game.h"

#include "buddy_brain.h"
#include "buddy_motion.h"

#define TICK BUDDY_MOTION_TICK_MS
#define READY_HOP_MS 450u
#define WATCH_CYCLE_MS 3600u // a blink and a glance at the board in every cycle
#define WATCH_BLINK_AT 1400u
#define WATCH_BLINK_MS 160u
#define WATCH_GLANCE_AT 2500u
#define WATCH_GLANCE_MS 600u
#define THINK_DART_MS 260u   // his eyes go to the board and back
#define THINK_TONGUE_AT 300u
#define WON_HOP_MS 450u
#define WON_HOPS 2u
#define WON_SPARKLE_MS 200u
#define LOST_SHAKE_MS 450u
#define SULK_CYCLE_MS 4200u  // sulking: looking away, glaring back at the board now and then
#define SULK_GLARE_AT 2800u
#define SULK_GLARE_MS 700u
#define DREW_DART_MS 300u
#define DREW_DART_END 1500u
#define PUZZLE_CYCLE_MS 3000u // puzzled: a little head wobble now and then
#define PUZZLE_WOBBLE_AT 2000u
#define PUZZLE_WOBBLE_MS 300u

// The next moment something changes: the earliest of the edges still ahead of t.
static void edge(uint32_t *next, uint32_t t, uint32_t at)
{
    if (at > t && at - t < *next) {
        *next = at - t;
    }
}

static void emit(buddy_pose_t *p, int slot, buddy_part_kind_t kind, buddy_from_t from, int dx, int dy, int frame)
{
    p->props.particle[slot] = (buddy_particle_t){(uint8_t)kind, (uint8_t)from, (uint8_t)frame, (int8_t)dx, (int8_t)dy};
}

// Breathing: the same slow bob as when he idles.
static uint32_t bob(uint32_t t, buddy_pose_t *p)
{
    p->lift = (uint8_t)((t / BUDDY_BOB_MS) & 1u);
    return BUDDY_BOB_MS - t % BUDDY_BOB_MS;
}

static uint32_t watch(const buddy_game_t *g, uint32_t t, buddy_pose_t *p)
{
    p->eyes = g->attitude == BUDDY_GAME_SMUG ? BUDDY_EYES_LIDDED
              : g->attitude == BUDDY_GAME_KEEN ? BUDDY_EYES_FOCUS
                                               : BUDDY_EYES_OPEN;
    p->mouth = g->attitude == BUDDY_GAME_SMUG ? BUDDY_MOUTH_SMIRK
               : g->attitude == BUDDY_GAME_KEEN ? BUDDY_MOUTH_GRIT
                                                : BUDDY_MOUTH_NEUTRAL;
    p->antenna = g->attitude == BUDDY_GAME_KEEN ? BUDDY_ANTENNA_EXCITED : BUDDY_ANTENNA_CALM;
    uint32_t next = bob(t, p);
    const uint32_t c = t % WATCH_CYCLE_MS;
    if (c >= WATCH_BLINK_AT && c < WATCH_BLINK_AT + WATCH_BLINK_MS) {
        p->eyes = BUDDY_EYES_CLOSED;
    }
    if (c >= WATCH_GLANCE_AT && c < WATCH_GLANCE_AT + WATCH_GLANCE_MS) {
        p->look = g->side; // a look at the board
    }
    edge(&next, c, WATCH_BLINK_AT);
    edge(&next, c, WATCH_BLINK_AT + WATCH_BLINK_MS);
    edge(&next, c, WATCH_GLANCE_AT);
    edge(&next, c, WATCH_GLANCE_AT + WATCH_GLANCE_MS);
    edge(&next, c, WATCH_CYCLE_MS);
    return next;
}

static uint32_t ready(const buddy_game_t *g, uint32_t t, buddy_pose_t *p)
{
    if (t >= READY_HOP_MS) {
        return watch(g, t, p);
    }
    p->eyes = BUDDY_EYES_HAPPY;
    p->mouth = BUDDY_MOUTH_SMILE;
    p->antenna = BUDDY_ANTENNA_EXCITED;
    p->lift = buddy_hop(t, READY_HOP_MS, 2);
    return buddy_min_u32(TICK, READY_HOP_MS - t);
}

static uint32_t think(const buddy_game_t *g, uint32_t t, buddy_pose_t *p)
{
    p->eyes = BUDDY_EYES_FOCUS;
    p->mouth = t < THINK_TONGUE_AT ? BUDDY_MOUTH_NEUTRAL : BUDDY_MOUTH_TONGUE;
    p->antenna = BUDDY_ANTENNA_CALM;
    p->look = (t / THINK_DART_MS) % 3 == 2 ? 0 : g->side; // mostly on the board, now and then back at you
    uint32_t next = THINK_DART_MS - t % THINK_DART_MS;
    edge(&next, t, THINK_TONGUE_AT);
    return next;
}

static uint32_t won(const buddy_game_t *g, uint32_t t, buddy_pose_t *p)
{
    if (t >= BUDDY_GAME_REACT_MS) { // smug and self-satisfied, waiting for the rematch
        const buddy_game_t smug = {BUDDY_GAME_WATCH, BUDDY_GAME_SMUG, g->side, 0};
        return watch(&smug, t - BUDDY_GAME_REACT_MS, p);
    }
    p->eyes = BUDDY_EYES_HAPPY;
    p->mouth = BUDDY_MOUTH_GRIN;
    p->antenna = BUDDY_ANTENNA_EXCITED;
    uint32_t next = BUDDY_GAME_REACT_MS - t;
    if (t < WON_HOPS * WON_HOP_MS) {
        p->lift = buddy_hop(t % WON_HOP_MS, WON_HOP_MS, BUDDY_MAX_LIFT);
        next = buddy_min_u32(next, TICK);
    }
    // Sparkles twinkle on both sides of his head, out of step.
    const uint32_t s = t / WON_SPARKLE_MS;
    emit(p, 0, BUDDY_PART_SPARKLE, BUDDY_FROM_HEAD_L, 0, -(int)(s % 3), (int)(s & 1u));
    emit(p, 1, BUDDY_PART_SPARKLE, BUDDY_FROM_HEAD_R, 0, -(int)((s + 1) % 3), (int)((s + 1) & 1u));
    return buddy_min_u32(next, WON_SPARKLE_MS - t % WON_SPARKLE_MS);
}

static uint32_t lost(const buddy_game_t *g, uint32_t t, buddy_pose_t *p)
{
    uint32_t next;
    if (t < LOST_SHAKE_MS) { // frustrated
        p->eyes = BUDDY_EYES_SQUEEZE;
        p->mouth = BUDDY_MOUTH_GRIT;
        p->antenna = BUDDY_ANTENNA_EXCITED;
        p->shake = (t / 60) & 1u ? 1 : -1;
        return buddy_min_u32(60 - t % 60, LOST_SHAKE_MS - t);
    }
    if (t < BUDDY_GAME_REACT_MS) { // disappointed: he droops, sighs, sheds a tear
        p->eyes = BUDDY_EYES_HALF;
        p->mouth = t >= 1500 && t < 1800 ? BUDDY_MOUTH_PUFF : BUDDY_MOUTH_FROWN;
        p->antenna = BUDDY_ANTENNA_DIM;
        p->tint = BUDDY_TINT_DROWSY;
        p->nod = 1;
        next = BUDDY_GAME_REACT_MS - t;
        edge(&next, t, 1500);
        edge(&next, t, 1800);
        edge(&next, t, 700); // the tear runs from here...
        edge(&next, t, 2300); // ...to here
        if (t >= 700 && t < 2300) {
            emit(p, 0, BUDDY_PART_SWEAT, BUDDY_FROM_BROW, 8, 4 + (int)((t - 700) * 4 / 1600), 0);
            next = buddy_min_u32(next, 400 - (t - 700) % 400); // it runs one art pixel lower every 400 ms
        }
        return next;
    }
    // Sulking (disappointed, a little annoyed): he looks away from the board, and glares back at it now and then.
    const uint32_t c = (t - BUDDY_GAME_REACT_MS) % SULK_CYCLE_MS;
    p->eyes = BUDDY_EYES_HALF;
    p->mouth = BUDDY_MOUTH_FROWN;
    p->antenna = BUDDY_ANTENNA_DIM;
    p->look = (int8_t)-g->side;
    if (c >= SULK_GLARE_AT && c < SULK_GLARE_AT + SULK_GLARE_MS) {
        p->eyes = BUDDY_EYES_ANGRY;
        p->look = g->side;
    }
    next = SULK_CYCLE_MS - c;
    edge(&next, c, SULK_GLARE_AT);
    edge(&next, c, SULK_GLARE_AT + SULK_GLARE_MS);
    return next;
}

static uint32_t drew(const buddy_game_t *g, uint32_t t, buddy_pose_t *p)
{
    p->props.query = t >= 300;
    p->antenna = BUDDY_ANTENNA_CALM;
    uint32_t next = UINT32_MAX;
    edge(&next, t, 300);
    if (t < BUDDY_GAME_REACT_MS) { // confused: his eyes dart about, his head wobbles
        p->eyes = BUDDY_EYES_HALF;
        p->mouth = t < 900 ? BUDDY_MOUTH_OPEN : BUDDY_MOUTH_NEUTRAL;
        edge(&next, t, 900);
        if (t < DREW_DART_END) {
            p->look = (t / DREW_DART_MS) & 1u ? 1 : -1;
            p->wobble = (t / (DREW_DART_MS / 2)) & 1u ? 1 : -1;
            edge(&next, t, (t / (DREW_DART_MS / 2) + 1) * (DREW_DART_MS / 2));
        }
        edge(&next, t, BUDDY_GAME_REACT_MS);
        return next;
    }
    // Puzzled: staring at the board, the "?" still up, the head tilting now and then.
    const uint32_t c = (t - BUDDY_GAME_REACT_MS) % PUZZLE_CYCLE_MS;
    p->eyes = BUDDY_EYES_OPEN;
    p->mouth = BUDDY_MOUTH_NEUTRAL;
    p->look = g->side;
    if (c >= PUZZLE_WOBBLE_AT && c < PUZZLE_WOBBLE_AT + PUZZLE_WOBBLE_MS) {
        p->wobble = g->side;
    }
    next = PUZZLE_CYCLE_MS - c;
    edge(&next, c, PUZZLE_WOBBLE_AT);
    edge(&next, c, PUZZLE_WOBBLE_AT + PUZZLE_WOBBLE_MS);
    return next;
}

uint32_t buddy_game_pose(const buddy_game_t *g, uint32_t t, buddy_pose_t *pose)
{
    uint32_t next;
    switch (g->face) {
    case BUDDY_GAME_READY:
        next = ready(g, t, pose);
        break;
    case BUDDY_GAME_THINK:
        next = think(g, t, pose);
        break;
    case BUDDY_GAME_WON:
        next = won(g, t, pose);
        break;
    case BUDDY_GAME_LOST:
        next = lost(g, t, pose);
        break;
    case BUDDY_GAME_DREW:
        next = drew(g, t, pose);
        break;
    default:
        next = watch(g, t, pose);
        break;
    }
    return next ? next : 1;
}
