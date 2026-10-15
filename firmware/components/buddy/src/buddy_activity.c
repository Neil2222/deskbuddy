#include "buddy_activity.h"

#include <stddef.h>

#include "buddy_brain.h" // BUDDY_MOTION_TICK_MS
#include "buddy_monster.h"
#include "buddy_motion.h"

static const char *const k_say[BUDDY_SAY_COUNT] = {
    [BUDDY_SAY_NONE] = "",
    [BUDDY_SAY_WATER] = "NOTE TO SELF: ROBOTS + WATER = NO",
    [BUDDY_SAY_OM] = "OMMMMMMMMM...",
    [BUDDY_SAY_REMATCH] = "BEST OF THREE?",
};

const char *buddy_say_text(buddy_say_t say)
{
    return k_say[say < BUDDY_SAY_COUNT ? say : BUDDY_SAY_NONE];
}

#define TICK BUDDY_MOTION_TICK_MS

static void emit(buddy_pose_t *p, int slot, buddy_part_kind_t kind, buddy_from_t from, int dx, int dy, int frame)
{
    p->props.particle[slot] = (buddy_particle_t){(uint8_t)kind, (uint8_t)from, (uint8_t)frame, (int8_t)dx, (int8_t)dy};
}

static void hand(buddy_props_t *pr, buddy_hand_t from, buddy_hand_t to, uint8_t mix)
{
    pr->hand_from = from;
    pr->hand_to = to;
    pr->hand_mix = mix;
}

// -1 / +1 alternating every `period` ms: trembling.
static int8_t jitter(uint32_t t, uint32_t period)
{
    return (t / period) % 2 ? 1 : -1;
}

// --- Water: a glass of water, for a robot ---------------------------------------------------------------------------

#define W_RAISED 450u   // the glass is up, he is proud of it
#define W_TO_MOUTH 700u
#define W_DRINK 1000u   // gulping
#define W_DRUNK 2000u
#define W_LOOK 2300u    // ...wait.
#define W_REALIZE 2700u // "!"
#define W_ZAP 2900u     // short circuit
#define W_SMOKE 3900u   // smoke from his ears
#define W_COUGH 4600u
#define W_GULP_MS 250u

static uint32_t water(uint32_t t, buddy_pose_t *p)
{
    buddy_props_t *pr = &p->props;
    p->eyes = BUDDY_EYES_OPEN;
    p->mouth = BUDDY_MOUTH_SMILE;
    p->antenna = BUDDY_ANTENNA_CALM;
    pr->glass = t < W_DRUNK ? BUDDY_GLASS_LEVELS : t < W_SMOKE ? 1 : 0; // dropped in the short circuit
    if (t < W_RAISED) {
        hand(pr, BUDDY_HAND_HOME, BUDDY_HAND_SHOW, buddy_ease(t, W_RAISED));
        p->eyes = BUDDY_EYES_HAPPY;
        return TICK;
    }
    if (t < W_TO_MOUTH) { // cheers!
        hand(pr, BUDDY_HAND_SHOW, BUDDY_HAND_SHOW, 0);
        p->eyes = BUDDY_EYES_HAPPY;
        p->mouth = BUDDY_MOUTH_GRIN;
        p->antenna = BUDDY_ANTENNA_EXCITED;
        p->lift = buddy_hop(t - W_RAISED, W_TO_MOUTH - W_RAISED, 1);
        return TICK;
    }
    if (t < W_DRINK) {
        hand(pr, BUDDY_HAND_SHOW, BUDDY_HAND_SIP, buddy_ease(t - W_TO_MOUTH, W_DRINK - W_TO_MOUTH));
        return TICK;
    }
    if (t < W_DRUNK) { // gulp, gulp, gulp: the glass empties, his head bobs with every swallow
        const uint32_t u = t - W_DRINK;
        hand(pr, BUDDY_HAND_SIP, BUDDY_HAND_SIP, 0);
        pr->glass = (uint8_t)(BUDDY_GLASS_LEVELS - u * BUDDY_GLASS_LEVELS / (W_DRUNK - W_DRINK));
        pr->glass = pr->glass < 1 ? 1 : pr->glass;
        p->eyes = BUDDY_EYES_CLOSED;
        p->mouth = BUDDY_MOUTH_NEUTRAL;
        p->nod = u % W_GULP_MS < 90 ? 1 : 0;
        return buddy_min_u32(u % W_GULP_MS < 90 ? 90 - u % W_GULP_MS : W_GULP_MS - u % W_GULP_MS, W_DRUNK - t);
    }
    if (t < W_LOOK) { // aaah
        hand(pr, BUDDY_HAND_SIP, BUDDY_HAND_SHOW, buddy_ease(t - W_DRUNK, W_LOOK - W_DRUNK));
        p->eyes = BUDDY_EYES_HAPPY;
        return TICK;
    }
    if (t < W_REALIZE) { // he looks at the empty glass
        hand(pr, BUDDY_HAND_SHOW, BUDDY_HAND_SHOW, 0);
        p->mouth = BUDDY_MOUTH_NEUTRAL;
        p->look = 1;
        return W_REALIZE - t;
    }
    if (t < W_ZAP) { // !!
        hand(pr, BUDDY_HAND_SHOW, BUDDY_HAND_SHOW, 0);
        p->eyes = BUDDY_EYES_WIDE;
        p->mouth = BUDDY_MOUTH_OPEN;
        p->antenna = BUDDY_ANTENNA_EXCITED;
        pr->alert = true;
        return W_ZAP - t;
    }
    if (t < W_SMOKE) { // short circuit
        const uint32_t u = t - W_ZAP;
        hand(pr, BUDDY_HAND_SHOW, BUDDY_HAND_HOME, buddy_ease(u, 150)); // the glass goes, quickly
        p->tint = (u / 90) % 2 ? BUDDY_TINT_NORMAL : BUDDY_TINT_ZAP;
        p->eyes = BUDDY_EYES_BOLT;
        p->mouth = BUDDY_MOUTH_ZAP;
        p->antenna = BUDDY_ANTENNA_OVERLOAD;
        p->shake = jitter(u, 50);
        p->wobble = jitter(u, 70);
        pr->bolts = 3;
        pr->strike = (uint16_t)(u / 100);
        pr->fx[0] = (buddy_fx_t){BUDDY_FX_SPARK, BUDDY_SPOT_PORT, (uint8_t)(u / 70 % BUDDY_SPARK_FRAMES)};
        pr->fx[1] = (buddy_fx_t){BUDDY_FX_SPARK, BUDDY_SPOT_NECK_L, (uint8_t)((u / 70 + 1) % BUDDY_SPARK_FRAMES)};
        return buddy_min_u32(10, W_SMOKE - t);
    }
    p->say = BUDDY_SAY_WATER;
    p->eyes = BUDDY_EYES_HALF;
    p->mouth = BUDDY_MOUTH_FROWN;
    if (t < W_COUGH) { // smoke from both ears
        const uint32_t u = t - W_SMOKE;
        p->tint = BUDDY_TINT_DROWSY;
        for (int k = 0; k < 2; k++) {
            if (u < (uint32_t)k * 300) {
                continue;
            }
            const uint32_t a = u - (uint32_t)k * 300;
            if (a < 700) {
                const int frame = a < 250 ? 0 : a < 500 ? 1 : 2, up = -(int)(a * 6 / 700);
                emit(p, 2 * k, BUDDY_PART_SMOKE, BUDDY_FROM_HEAD_L, -1 - (int)(a * 2 / 700), up, frame);
                emit(p, 2 * k + 1, BUDDY_PART_SMOKE, BUDDY_FROM_HEAD_R, 1 + (int)(a * 2 / 700), up, frame);
            }
        }
        return 50;
    }
    // Two coughs.
    const uint32_t u = (t - W_COUGH) % 200;
    if (u < 120) {
        p->mouth = BUDDY_MOUTH_OPEN;
        p->eyes = BUDDY_EYES_SQUEEZE;
        p->shake = u < 60 ? 1 : 0;
        emit(p, 4, BUDDY_PART_SMOKE, BUDDY_FROM_MOUTH, -1 - (int)(u / 40), 0, u < 60 ? 0 : 1);
    }
    return TICK;
}

// --- Sneeze: it blows his head off ---------------------------------------------------------------------------------

#define S_A 300u     // a...
#define S_AA 700u    // a....
#define S_CHOO 1100u // ACHOO
#define S_UP 150u    // after the sneeze: the head flies up...
#define S_HANG 350u  // ...hangs there...
#define S_DOWN 550u  // ...and drops back
#define S_DAZED 2300u

static uint32_t sneeze(uint32_t t, buddy_pose_t *p)
{
    buddy_props_t *pr = &p->props;
    p->eyes = BUDDY_EYES_OPEN;
    p->mouth = BUDDY_MOUTH_NEUTRAL;
    p->antenna = BUDDY_ANTENNA_CALM;
    if (t < S_A) { // an itch
        p->eyes = BUDDY_EYES_HALF;
        p->wobble = (t / 100) % 2 ? 1 : 0;
        return 100 - t % 100;
    }
    if (t < S_AA) {
        p->eyes = BUDDY_EYES_HALF;
        p->mouth = BUDDY_MOUTH_OPEN;
        p->lift = 1;
        return S_AA - t;
    }
    if (t < S_CHOO) {
        p->eyes = BUDDY_EYES_SQUEEZE;
        p->mouth = BUDDY_MOUTH_OPEN;
        p->antenna = BUDDY_ANTENNA_EXCITED;
        p->lift = 2;
        return S_CHOO - t;
    }
    const uint32_t landed = S_CHOO + S_DOWN;
    if (t < landed) {
        const uint32_t u = t - S_CHOO;
        p->mouth = BUDDY_MOUTH_OPEN;
        p->antenna = BUDDY_ANTENNA_EXCITED;
        p->eyes = u < S_UP ? BUDDY_EYES_SQUEEZE : BUDDY_EYES_WIDE;
        if (u < S_UP) {
            p->pop = (uint8_t)(BUDDY_MAX_POP * buddy_ease(u, S_UP) / 255);
        } else if (u < S_HANG) {
            p->pop = BUDDY_MAX_POP;
        } else { // falling, faster and faster
            const uint32_t f = u - S_HANG, d = S_DOWN - S_HANG;
            p->pop = (uint8_t)(BUDDY_MAX_POP - BUDDY_MAX_POP * f * f / (d * d));
        }
        // The spray, from his mouth (wherever his head is).
        for (int k = 0; k < 3; k++) {
            if (u < (uint32_t)k * 40) {
                continue;
            }
            const uint32_t a = u - (uint32_t)k * 40;
            if (a < 450) {
                emit(p, k, BUDDY_PART_STEAM, BUDDY_FROM_MOUTH, -2 - (int)(a * (6 + 2 * (uint32_t)k) / 450), k - 1,
                     a < 200 ? 0 : 1);
            }
        }
        return TICK;
    }
    if (t < S_DAZED) { // clunk
        const uint32_t u = t - landed;
        p->eyes = BUDDY_EYES_HALF;
        p->mouth = BUDDY_MOUTH_ZAP;
        p->wobble = jitter(u, 120);
        if (u < 300) {
            p->shake = jitter(u, 60);
            const uint8_t frame = u < 150 ? 0 : 1;
            pr->fx[0] = (buddy_fx_t){BUDDY_FX_POOF, BUDDY_SPOT_NECK_L, frame};
            pr->fx[1] = (buddy_fx_t){BUDDY_FX_POOF, BUDDY_SPOT_NECK_R, frame};
        }
        return 30;
    }
    p->eyes = BUDDY_EYES_HAPPY; // a sheepish smile
    p->mouth = BUDDY_MOUTH_SMILE;
    return BUDDY_SNEEZE_MS - t;
}

// --- Staring contest ------------------------------------------------------------------------------------------------

#define C_IN 200u     // the camera starts pushing in
#define C_CLOSE 1000u // his head fills the stage
#define C_TWITCH 2200u
#define C_DART 2500u
#define C_SWEAT 2700u
#define C_FORCE 2900u // eyes forced wide open
#define C_BLINK 3200u // he loses
#define C_FURY 3350u
#define C_OUT 3600u   // back to his place...
#define C_HOME 4300u  // ...fuming

static uint32_t stare(uint32_t t, buddy_pose_t *p)
{
    p->eyes = BUDDY_EYES_OPEN;
    p->mouth = BUDDY_MOUTH_NEUTRAL;
    p->antenna = BUDDY_ANTENNA_CALM;
    if (t < C_IN) { // challenge accepted
        p->eyes = BUDDY_EYES_FOCUS;
        p->antenna = BUDDY_ANTENNA_EXCITED;
        return C_IN - t;
    }
    if (t < C_CLOSE) {
        p->closeup = buddy_ease(t - C_IN, C_CLOSE - C_IN);
        p->closeup = p->closeup ? p->closeup : 1;
        p->eyes = BUDDY_EYES_FOCUS;
        return TICK;
    }
    if (t < C_BLINK) { // completely still...
        p->closeup = 255;
        uint32_t next = C_BLINK - t;
        if (t >= C_TWITCH && t < C_TWITCH + 80) { // an eye twitches
            p->eyes = BUDDY_EYES_HALF;
        }
        if (t >= C_DART && t < C_DART + 70) { // a glance aside
            p->look = 1;
        }
        if (t >= C_SWEAT) { // a drop of sweat runs down
            emit(p, 0, BUDDY_PART_SWEAT, BUDDY_FROM_BROW, 0, (int)((t - C_SWEAT) * 3 / (C_BLINK - C_SWEAT)), 0);
            next = 50;
        }
        if (t >= C_FORCE) { // eyes forced wide
            p->eyes = BUDDY_EYES_WIDE;
            p->mouth = BUDDY_MOUTH_GRIT;
        }
        const uint32_t edges[] = {C_TWITCH, C_TWITCH + 80, C_DART, C_DART + 70, C_SWEAT, C_FORCE};
        for (unsigned i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
            if (t < edges[i]) {
                next = buddy_min_u32(next, edges[i] - t);
            }
        }
        return next;
    }
    if (t < C_FURY) { // blink. He lost.
        p->closeup = 255;
        p->eyes = BUDDY_EYES_CLOSED;
        p->mouth = BUDDY_MOUTH_GRIT;
        return C_FURY - t;
    }
    p->eyes = BUDDY_EYES_ANGRY;
    p->mouth = BUDDY_MOUTH_FROWN;
    if (t < C_OUT) { // red with fury
        p->closeup = 255;
        p->tint = BUDDY_TINT_FURY;
        return C_OUT - t;
    }
    if (t < C_HOME) {
        p->closeup = (uint8_t)(255 - buddy_ease(t - C_OUT, C_HOME - C_OUT));
        p->mouth = BUDDY_MOUTH_GRIT;
        return TICK;
    }
    // Fuming, at his place.
    const uint32_t u = t - C_HOME;
    p->say = BUDDY_SAY_REMATCH;
    emit(p, 0, BUDDY_PART_ANGER, BUDDY_FROM_HEAD_R, 0, -(int)(u / 300 % 2), (int)(u / 200 % 2));
    const uint32_t a = u % 600;
    emit(p, 1, BUDDY_PART_STEAM, BUDDY_FROM_HEAD_L, -(int)(a * 2 / 600), -1 - (int)(a * 4 / 600), a < 300 ? 0 : 1);
    return 50;
}

// --- Meditation -----------------------------------------------------------------------------------------------------

#define M_RISE 300u
#define M_HOVER 1200u
#define M_BOB_MS 700u
#define M_PING 5800u // a notification!
#define M_FALL 6150u
#define M_THUD 6400u
#define M_UP 6700u
#define M_SPARKLE_MS 1500u

static uint32_t meditate(uint32_t t, buddy_pose_t *p)
{
    buddy_props_t *pr = &p->props;
    p->eyes = BUDDY_EYES_CLOSED;
    p->mouth = BUDDY_MOUTH_NEUTRAL;
    p->antenna = BUDDY_ANTENNA_DIM;
    if (t < M_RISE) { // eyes closed, deep breath
        return M_RISE - t;
    }
    if (t < M_HOVER) { // he floats up
        p->mouth = BUDDY_MOUTH_SMILE;
        p->lift = (uint8_t)(BUDDY_MAX_HOVER * buddy_ease(t - M_RISE, M_HOVER - M_RISE) / 255);
        return TICK;
    }
    if (t < M_PING) { // hovering in bliss, sparkles drifting up past him
        const uint32_t u = t - M_HOVER;
        p->say = BUDDY_SAY_OM;
        p->mouth = BUDDY_MOUTH_SMILE;
        p->lift = (uint8_t)(BUDDY_MAX_HOVER - (u / M_BOB_MS) % 2);
        static const struct {
            uint8_t from;
            int8_t dx;
        } k_at[3] = {{BUDDY_FROM_ABOVE, 1}, {BUDDY_FROM_HEAD_L, -2}, {BUDDY_FROM_HEAD_R, 2}};
        for (int k = 0; k < 3; k++) {
            const uint32_t a = (u + (uint32_t)k * 500) % M_SPARKLE_MS;
            emit(p, k, BUDDY_PART_SPARKLE, (buddy_from_t)k_at[k].from, k_at[k].dx, -(int)(a * 5 / M_SPARKLE_MS),
                 (int)(a / 250 % 2));
        }
        return buddy_min_u32(100, M_PING - t);
    }
    if (t < M_FALL) { // ping! a notification
        p->say = BUDDY_SAY_OM;
        p->lift = BUDDY_MAX_HOVER;
        p->eyes = BUDDY_EYES_WIDE;
        p->mouth = BUDDY_MOUTH_OPEN;
        p->antenna = BUDDY_ANTENNA_OVERLOAD;
        p->shake = jitter(t - M_PING, 50);
        return buddy_min_u32(50 - (t - M_PING) % 50, M_FALL - t);
    }
    if (t < M_THUD) { // down he goes, faster and faster
        const uint32_t f = t - M_FALL, d = M_THUD - M_FALL;
        p->lift = (uint8_t)(BUDDY_MAX_HOVER - BUDDY_MAX_HOVER * f * f / (d * d));
        p->eyes = BUDDY_EYES_WIDE;
        p->mouth = BUDDY_MOUTH_OPEN;
        p->antenna = BUDDY_ANTENNA_EXCITED;
        return TICK;
    }
    if (t < M_UP) { // thud
        const uint32_t u = t - M_THUD;
        p->eyes = BUDDY_EYES_SQUEEZE;
        p->mouth = BUDDY_MOUTH_GRIT;
        p->antenna = BUDDY_ANTENNA_CALM;
        if (u < 240) {
            p->shake = jitter(u, 60);
        }
        if (u < 300) {
            const uint8_t frame = u < 150 ? 0 : 1;
            pr->fx[0] = (buddy_fx_t){BUDDY_FX_POOF, BUDDY_SPOT_FEET_L, frame};
            pr->fx[1] = (buddy_fx_t){BUDDY_FX_POOF, BUDDY_SPOT_FEET_R, frame};
        }
        return 30;
    }
    // Sheepish: a glance aside, as if nothing happened.
    p->antenna = BUDDY_ANTENNA_CALM;
    p->eyes = t < M_UP + 300 ? BUDDY_EYES_HALF : BUDDY_EYES_OPEN;
    p->mouth = BUDDY_MOUTH_SMIRK;
    p->look = -1;
    return t < M_UP + 300 ? M_UP + 300 - t : BUDDY_MEDITATE_MS - t;
}

// --- Dispatch -------------------------------------------------------------------------------------------------------

uint32_t buddy_activity_ms(buddy_state_t state)
{
    switch (state) {
    case BUDDY_STATE_MONSTER:
        return BUDDY_MONSTER_MS;
    case BUDDY_STATE_WATER:
        return BUDDY_WATER_MS;
    case BUDDY_STATE_SNEEZE:
        return BUDDY_SNEEZE_MS;
    case BUDDY_STATE_STARE:
        return BUDDY_STARE_MS;
    case BUDDY_STATE_MEDITATE:
        return BUDDY_MEDITATE_MS;
    default:
        return 0;
    }
}

uint32_t buddy_activity_pose(buddy_state_t state, uint32_t t, buddy_pose_t *pose)
{
    uint32_t next;
    switch (state) {
    case BUDDY_STATE_MONSTER:
        return buddy_monster_pose(t, pose);
    case BUDDY_STATE_WATER:
        next = water(t, pose);
        break;
    case BUDDY_STATE_SNEEZE:
        next = sneeze(t, pose);
        break;
    case BUDDY_STATE_STARE:
        next = stare(t, pose);
        break;
    case BUDDY_STATE_MEDITATE:
        next = meditate(t, pose);
        break;
    default:
        return UINT32_MAX;
    }
    const uint32_t left = buddy_activity_ms(state) - t;
    next = buddy_min_u32(next, left);
    return next ? next : 1;
}

const char *buddy_activity_doing(buddy_state_t state)
{
    switch (state) {
    case BUDDY_STATE_MONSTER:
        return "charging!";
    case BUDDY_STATE_WATER:
        return "drinking";
    case BUDDY_STATE_SNEEZE:
        return "sneezing";
    case BUDDY_STATE_STARE:
        return "staring";
    case BUDDY_STATE_MEDITATE:
        return "meditating";
    default:
        return NULL;
    }
}
