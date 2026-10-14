// Character behavior: a small time-driven state machine that produces a pose.
//
// Pure logic: no rendering, no hardware, no clock of its own. Time is passed in as a free-running millisecond
// counter (wrap-around safe). Future behavior (needs, moods, external events) plugs in here, not in the renderer.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buddy_pose.h"

#ifdef __cplusplus
extern "C" {
#endif

// Timings, in ms.
#define BUDDY_BOB_MS 700          // idle breathing: lift toggles between 0 and 1
#define BUDDY_BLINK_MIN_MS 2000   // idle time between blinks
#define BUDDY_BLINK_MAX_MS 6000
#define BUDDY_SURPRISE_MS 450     // one quick hop with wide eyes
#define BUDDY_HAPPY_HOP_MS 450
#define BUDDY_HAPPY_HOPS 3
#define BUDDY_MOTION_TICK_MS 20   // update rate while hopping

// Things that can happen to the character, from any source: the touchscreen, a script, and later the Windows
// companion over USB. The numeric values are part of that external interface: never renumber or reuse a value.
typedef enum {
    BUDDY_EVENT_POKE = 1,          // touched: surprise, then happy hops
    BUDDY_EVENT_DRINK_MONSTER = 2, // drinks a can of Monster Energy through a charging cable
} buddy_event_t;

typedef struct {
    buddy_state_t state;
    uint32_t state_start;
    uint32_t next_blink; // idle: when the next blink starts
    uint32_t rng;
} buddy_brain_t;

void buddy_brain_init(buddy_brain_t *b, uint32_t now_ms, uint32_t seed);

// Delivers an event. Returns true when it started a reaction. Pokes restart the touch reaction; while the Monster
// sequence runs every event is ignored, so it always plays to the end. Unknown events are ignored.
bool buddy_brain_event(buddy_brain_t *b, buddy_event_t ev, uint32_t now_ms);

// Drops whatever the character is doing, including a running Monster sequence, and idles from now_ms on (used
// when he comes back from being paused, e.g. after Office Mode).
void buddy_brain_rest(buddy_brain_t *b, uint32_t now_ms);

// Advances to now_ms and writes the pose to show. Returns how many ms until the pose may change next (>= 1).
uint32_t buddy_brain_tick(buddy_brain_t *b, uint32_t now_ms, buddy_pose_t *pose);

#ifdef __cplusplus
}
#endif
