#include "buddy_brain.h"

#include "buddy_activity.h"
#include "buddy_motion.h"

static const struct {
    buddy_eyes_t eyes;
    uint16_t ms;
} k_blink[] = {
    {BUDDY_EYES_HALF, 50},
    {BUDDY_EYES_CLOSED, 80},
    {BUDDY_EYES_HALF, 50},
};
#define BLINK_MS 180u
#define HAPPY_MS ((uint32_t)BUDDY_HAPPY_HOPS * BUDDY_HAPPY_HOP_MS)
#define SURPRISE_HEIGHT 3u

static uint32_t rng_next(buddy_brain_t *b)
{
    uint32_t x = b->rng; // xorshift32
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    b->rng = x;
    return x;
}

static uint32_t blink_delay(buddy_brain_t *b)
{
    return BUDDY_BLINK_MIN_MS + rng_next(b) % (BUDDY_BLINK_MAX_MS - BUDDY_BLINK_MIN_MS + 1);
}

// Starts the mood's loop over from a fresh cycle.
static void start_cycle(buddy_brain_t *b, uint32_t now_ms)
{
    b->cycle_start = now_ms;
    b->cycle_n = 0;
    b->cycle_seed = rng_next(b);
    b->cycle_len = buddy_mood_cycle_ms(b->mood, b->cycle_seed);
}

void buddy_brain_init(buddy_brain_t *b, uint32_t now_ms, uint32_t seed)
{
    b->state = BUDDY_STATE_IDLE;
    b->state_start = now_ms;
    b->rng = seed ? seed : 0x9E3779B9u;
    b->next_blink = now_ms + blink_delay(b);
    b->mood = BUDDY_MOOD_IDLE;
    b->mood_start = now_ms;
    b->mood_timed = false;
    b->mood_back = BUDDY_MOOD_IDLE;
    start_cycle(b, now_ms);
}

static void enter(buddy_brain_t *b, buddy_state_t state, uint32_t now_ms)
{
    b->state = state;
    b->state_start = now_ms;
}

bool buddy_brain_busy(const buddy_brain_t *b, uint32_t now_ms)
{
    const uint32_t ms = buddy_activity_ms(b->state);
    return ms && now_ms - b->state_start < ms;
}

bool buddy_brain_event(buddy_brain_t *b, buddy_event_t ev, uint32_t now_ms)
{
    if (buddy_brain_busy(b, now_ms)) {
        return false; // the show goes on
    }
    switch (ev) {
    case BUDDY_EVENT_POKE:
        enter(b, BUDDY_STATE_SURPRISED, now_ms);
        return true;
    case BUDDY_EVENT_DRINK_MONSTER:
        enter(b, BUDDY_STATE_MONSTER, now_ms);
        return true;
    case BUDDY_EVENT_DRINK_WATER:
        enter(b, BUDDY_STATE_WATER, now_ms);
        return true;
    case BUDDY_EVENT_SNEEZE:
        enter(b, BUDDY_STATE_SNEEZE, now_ms);
        return true;
    case BUDDY_EVENT_STARE:
        enter(b, BUDDY_STATE_STARE, now_ms);
        return true;
    case BUDDY_EVENT_MEDITATE:
        enter(b, BUDDY_STATE_MEDITATE, now_ms);
        return true;
    case BUDDY_EVENT_ASK_PEUKIE:
        enter(b, BUDDY_STATE_ASK, now_ms);
        return true;
    default:
        return false;
    }
}

// No reaction: the mood plays from the start of a cycle, so he resumes it calmly (not halfway through a drag).
static void resume_mood(buddy_brain_t *b, uint32_t now_ms)
{
    b->next_blink = now_ms + blink_delay(b);
    start_cycle(b, now_ms);
}

void buddy_brain_rest(buddy_brain_t *b, uint32_t now_ms)
{
    enter(b, BUDDY_STATE_IDLE, now_ms);
    resume_mood(b, now_ms);
}

void buddy_brain_set_mood(buddy_brain_t *b, buddy_mood_t mood, uint32_t duration_ms, uint32_t now_ms)
{
    if (mood >= BUDDY_MOOD_COUNT) {
        return;
    }
    if (mood != buddy_brain_mood(b, now_ms)) {
        b->mood = mood;
        b->mood_start = now_ms;
        resume_mood(b, now_ms);
    }
    b->mood_timed = duration_ms > 0 && mood != BUDDY_MOOD_IDLE;
    b->mood_end = now_ms + duration_ms;
    b->mood_back = BUDDY_MOOD_IDLE;
}

uint32_t buddy_brain_asking(const buddy_brain_t *b, uint32_t now_ms)
{
    const uint32_t t = now_ms - b->state_start;
    return b->state == BUDDY_STATE_ASK && t < BUDDY_ASK_MS ? BUDDY_ASK_MS - t : 0;
}

bool buddy_brain_answer(buddy_brain_t *b, bool yes, uint32_t now_ms)
{
    if (!buddy_brain_asking(b, now_ms)) {
        return false;
    }
    if (!yes) {
        enter(b, BUDDY_STATE_SAD, now_ms);
        return true;
    }
    // A smoke break, then back to how he was (if he was smoking already, he just carries on).
    const buddy_mood_t before = buddy_brain_mood(b, now_ms);
    if (before != BUDDY_MOOD_PEUKIE) {
        buddy_brain_set_mood(b, BUDDY_MOOD_PEUKIE, BUDDY_SMOKE_BREAK_MS, now_ms);
        b->mood_back = before;
    }
    enter(b, BUDDY_STATE_HAPPY, now_ms); // happy hops first
    return true;
}

buddy_mood_t buddy_brain_mood(buddy_brain_t *b, uint32_t now_ms)
{
    if (b->mood_timed && buddy_reached(now_ms, b->mood_end)) {
        b->mood_timed = false;
        b->mood = b->mood_back;
        b->mood_back = BUDDY_MOOD_IDLE;
        b->mood_start = b->mood_end;
        resume_mood(b, now_ms);
    }
    return b->mood;
}

// The mood's pose. Cycles missed during a long pause are not replayed: the loop starts over.
static uint32_t mood_tick(buddy_brain_t *b, uint32_t now_ms, buddy_pose_t *pose)
{
    for (int skipped = 0; now_ms - b->cycle_start >= b->cycle_len; skipped++) {
        if (skipped == 4) {
            start_cycle(b, now_ms);
            break;
        }
        b->cycle_start += b->cycle_len;
        b->cycle_n++;
        b->cycle_seed = rng_next(b);
        b->cycle_len = buddy_mood_cycle_ms(b->mood, b->cycle_seed);
    }
    const buddy_mood_clock_t clock = {now_ms - b->cycle_start, b->cycle_len, b->cycle_n, b->cycle_seed,
                                      now_ms - b->mood_start};
    uint32_t next = buddy_mood_pose(b->mood, &clock, pose);
    next = buddy_min_u32(next, b->cycle_len - clock.t);
    if (b->mood_timed) {
        next = buddy_min_u32(next, b->mood_end - now_ms);
    }
    return next ? next : 1;
}

uint32_t buddy_brain_tick(buddy_brain_t *b, uint32_t now_ms, buddy_pose_t *pose)
{
    uint32_t t = now_ms - b->state_start;
    if (b->state == BUDDY_STATE_SURPRISED && t >= BUDDY_SURPRISE_MS) {
        b->state = BUDDY_STATE_HAPPY;
        b->state_start += BUDDY_SURPRISE_MS;
        t -= BUDDY_SURPRISE_MS;
    }
    if (b->state == BUDDY_STATE_ASK && t >= BUDDY_ASK_MS) { // no answer in time: as if it were no
        b->state = BUDDY_STATE_SAD;
        b->state_start += BUDDY_ASK_MS;
        t -= BUDDY_ASK_MS;
    }
    const uint32_t act = buddy_activity_ms(b->state);
    if ((b->state == BUDDY_STATE_HAPPY && t >= HAPPY_MS) || (act && t >= act)) {
        const uint32_t ms = b->state == BUDDY_STATE_HAPPY ? HAPPY_MS : act;
        b->state = BUDDY_STATE_IDLE;
        b->state_start += ms;
        t -= ms;
        resume_mood(b, now_ms);
    }

    *pose = (buddy_pose_t){.state = b->state};
    switch (b->state) {
    case BUDDY_STATE_SURPRISED:
        pose->eyes = BUDDY_EYES_WIDE;
        pose->mouth = BUDDY_MOUTH_OPEN;
        pose->antenna = BUDDY_ANTENNA_EXCITED;
        pose->lift = buddy_hop(t, BUDDY_SURPRISE_MS, SURPRISE_HEIGHT);
        return buddy_min_u32(BUDDY_MOTION_TICK_MS, BUDDY_SURPRISE_MS - t);

    case BUDDY_STATE_HAPPY:
        pose->eyes = BUDDY_EYES_HAPPY;
        pose->mouth = BUDDY_MOUTH_SMILE;
        pose->antenna = BUDDY_ANTENNA_EXCITED;
        pose->lift = buddy_hop(t % BUDDY_HAPPY_HOP_MS, BUDDY_HAPPY_HOP_MS, BUDDY_MAX_LIFT);
        return buddy_min_u32(BUDDY_MOTION_TICK_MS, HAPPY_MS - t);

    case BUDDY_STATE_IDLE:
        break;
    default: // an activity
        return buddy_activity_pose(b->state, t, pose);
    }
    if (buddy_brain_mood(b, now_ms) != BUDDY_MOOD_IDLE) {
        return mood_tick(b, now_ms, pose);
    }

    // The idle mood: breathing bob plus random blinks. Blinks missed during a long gap are skipped, not replayed.
    // The bob runs on the mood's clock, so it starts afresh when he comes back from a reaction or another mood.
    t = now_ms - b->cycle_start;
    if (buddy_reached(now_ms, b->next_blink + BLINK_MS)) {
        b->next_blink = now_ms + blink_delay(b);
    }
    pose->mouth = BUDDY_MOUTH_NEUTRAL;
    pose->antenna = BUDDY_ANTENNA_CALM;
    pose->lift = (uint8_t)((t / BUDDY_BOB_MS) & 1u);
    uint32_t next = BUDDY_BOB_MS - t % BUDDY_BOB_MS;

    pose->eyes = BUDDY_EYES_OPEN;
    if (buddy_reached(now_ms, b->next_blink)) {
        uint32_t bt = now_ms - b->next_blink;
        for (unsigned i = 0; i < sizeof(k_blink) / sizeof(k_blink[0]); i++) {
            if (bt < k_blink[i].ms) {
                pose->eyes = k_blink[i].eyes;
                next = buddy_min_u32(next, k_blink[i].ms - bt);
                break;
            }
            bt -= k_blink[i].ms;
        }
    } else {
        next = buddy_min_u32(next, b->next_blink - now_ms);
    }
    return next ? next : 1;
}
