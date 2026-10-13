#include "buddy_brain.h"

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

// Wrap-around safe "now is at or after t".
static bool reached(uint32_t now, uint32_t t)
{
    return (int32_t)(now - t) >= 0;
}

// Parabolic hop: 0 at both ends, `height` halfway, rounded to whole art pixels.
static uint8_t hop(uint32_t t, uint32_t duration, uint32_t height)
{
    if (t >= duration) {
        return 0;
    }
    const uint64_t num = 4ull * height * t * (duration - t);
    const uint64_t den = (uint64_t)duration * duration;
    return (uint8_t)((num + den / 2) / den);
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

void buddy_brain_init(buddy_brain_t *b, uint32_t now_ms, uint32_t seed)
{
    b->state = BUDDY_STATE_IDLE;
    b->state_start = now_ms;
    b->rng = seed ? seed : 0x9E3779B9u;
    b->next_blink = now_ms + blink_delay(b);
}

void buddy_brain_poke(buddy_brain_t *b, uint32_t now_ms)
{
    b->state = BUDDY_STATE_SURPRISED;
    b->state_start = now_ms;
}

uint32_t buddy_brain_tick(buddy_brain_t *b, uint32_t now_ms, buddy_pose_t *pose)
{
    uint32_t t = now_ms - b->state_start;
    if (b->state == BUDDY_STATE_SURPRISED && t >= BUDDY_SURPRISE_MS) {
        b->state = BUDDY_STATE_HAPPY;
        b->state_start += BUDDY_SURPRISE_MS;
        t -= BUDDY_SURPRISE_MS;
    }
    if (b->state == BUDDY_STATE_HAPPY && t >= HAPPY_MS) {
        b->state = BUDDY_STATE_IDLE;
        b->state_start += HAPPY_MS;
        t -= HAPPY_MS;
        b->next_blink = now_ms + blink_delay(b);
    }

    pose->state = b->state;
    switch (b->state) {
    case BUDDY_STATE_SURPRISED:
        pose->eyes = BUDDY_EYES_WIDE;
        pose->mouth = BUDDY_MOUTH_OPEN;
        pose->antenna = BUDDY_ANTENNA_EXCITED;
        pose->lift = hop(t, BUDDY_SURPRISE_MS, SURPRISE_HEIGHT);
        return min_u32(BUDDY_MOTION_TICK_MS, BUDDY_SURPRISE_MS - t);

    case BUDDY_STATE_HAPPY:
        pose->eyes = BUDDY_EYES_HAPPY;
        pose->mouth = BUDDY_MOUTH_SMILE;
        pose->antenna = BUDDY_ANTENNA_EXCITED;
        pose->lift = hop(t % BUDDY_HAPPY_HOP_MS, BUDDY_HAPPY_HOP_MS, BUDDY_MAX_LIFT);
        return min_u32(BUDDY_MOTION_TICK_MS, HAPPY_MS - t);

    case BUDDY_STATE_IDLE:
    default:
        break;
    }

    // Idle: breathing bob plus random blinks. Blinks missed during a long gap are skipped, not replayed.
    if (reached(now_ms, b->next_blink + BLINK_MS)) {
        b->next_blink = now_ms + blink_delay(b);
    }
    pose->mouth = BUDDY_MOUTH_NEUTRAL;
    pose->antenna = BUDDY_ANTENNA_CALM;
    pose->lift = (uint8_t)((t / BUDDY_BOB_MS) & 1u);
    uint32_t next = BUDDY_BOB_MS - t % BUDDY_BOB_MS;

    pose->eyes = BUDDY_EYES_OPEN;
    if (reached(now_ms, b->next_blink)) {
        uint32_t bt = now_ms - b->next_blink;
        for (unsigned i = 0; i < sizeof(k_blink) / sizeof(k_blink[0]); i++) {
            if (bt < k_blink[i].ms) {
                pose->eyes = k_blink[i].eyes;
                next = min_u32(next, k_blink[i].ms - bt);
                break;
            }
            bt -= k_blink[i].ms;
        }
    } else {
        next = min_u32(next, b->next_blink - now_ms);
    }
    return next ? next : 1;
}
