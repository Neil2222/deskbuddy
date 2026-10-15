// Character behavior: a small time-driven state machine that produces a pose.
//
// Pure logic: no rendering, no hardware, no clock of its own. Time is passed in as a free-running millisecond
// counter (wrap-around safe). Two layers: the mood (buddy_mood.h) is how he is, and stays until it is changed or
// its duration runs out; reactions (a tap, a Monster sequence) play on top of it and then hand back to the mood.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buddy_mood.h"
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
    BUDDY_EVENT_DRINK_WATER = 3,   // the activities in buddy_activity.h
    BUDDY_EVENT_SNEEZE = 4,
    BUDDY_EVENT_STARE = 5,
    BUDDY_EVENT_MEDITATE = 6,
    BUDDY_EVENT_ASK_PEUKIE = 7,    // "PEUKIE ROKEN?": he asks and waits for buddy_brain_answer()
} buddy_event_t;

typedef struct {
    buddy_state_t state;
    uint32_t state_start;
    uint32_t next_blink; // idle: when the next blink starts
    uint32_t rng;
    buddy_mood_t mood;
    uint32_t mood_start;
    uint32_t mood_end; // when a timed mood ends (mood_timed)...
    bool mood_timed;
    buddy_mood_t mood_back; // ...and the mood that follows it (idle, or what he was in before a smoke break)
    uint32_t cycle_start, cycle_len, cycle_n, cycle_seed; // where the mood's loop is
} buddy_brain_t;

void buddy_brain_init(buddy_brain_t *b, uint32_t now_ms, uint32_t seed);

// Delivers an event. Returns true when it started a reaction. Pokes restart the touch reaction; while an activity
// (buddy_activity.h, the Monster sequence among them) plays, every event is ignored, so it always plays to the end.
// Unknown events are ignored.
bool buddy_brain_event(buddy_brain_t *b, buddy_event_t ev, uint32_t now_ms);

// True while an activity plays (events are ignored then).
bool buddy_brain_busy(const buddy_brain_t *b, uint32_t now_ms);

// While he asks (BUDDY_STATE_ASK): ms until he gives up waiting (as if the answer were no); 0 when not asking.
uint32_t buddy_brain_asking(const buddy_brain_t *b, uint32_t now_ms);

// Answers his question. Yes: happy hops, then a smoke break (the peukie mood for BUDDY_SMOKE_BREAK_MS, then back to
// the mood he was in). No: briefly sad. Returns false when he is not asking (or the answer came too late).
bool buddy_brain_answer(buddy_brain_t *b, bool yes, uint32_t now_ms);

// Drops whatever the character is doing, including a running Monster sequence, and idles from now_ms on (used
// when he comes back from being paused, e.g. after Office Mode).
void buddy_brain_rest(buddy_brain_t *b, uint32_t now_ms);

// Sets the mood. duration_ms > 0: back to idle after that long; 0: until changed. Choosing a different mood
// starts its animation from the beginning; choosing the current one only changes its duration. A reaction that is
// playing finishes first (the new mood shows right after it).
void buddy_brain_set_mood(buddy_brain_t *b, buddy_mood_t mood, uint32_t duration_ms, uint32_t now_ms);

// The current mood at now_ms (a timed mood that has run out is idle again).
buddy_mood_t buddy_brain_mood(buddy_brain_t *b, uint32_t now_ms);

// Advances to now_ms and writes the pose to show. Returns how many ms until the pose may change next (>= 1).
uint32_t buddy_brain_tick(buddy_brain_t *b, uint32_t now_ms, buddy_pose_t *pose);

#ifdef __cplusplus
}
#endif
