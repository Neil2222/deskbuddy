// Moods: Buddy's ongoing state, shown as a looping animation whenever no reaction (a tap, a Monster sequence) is
// playing. A mood says how he feels; what he does in it is built from shared pieces (expressions, the telescopic
// hand, the floating effects in buddy_pose.h), so later moods and events reuse them instead of new copies of him:
//   idle     the original breathing, blinking Buddy
//   relaxed  calm and content: a broad smile, half-closed eyes, slow easy breathing
//   peukie   a smoke break: drags on a cigarette, blows smoke
//   angry    glaring and fuming, with fits of fury
//   sleepy   drooping, dozing off with floating Zs, yawning
//   working  concentrating hard, sweating, short pauses
//   happy    celebrating: hops, sparkles, swaying
//
// Pure logic, like buddy_brain.h. Each loop is cut into cycles of varying length; the brain keeps the clock.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buddy_pose.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUDDY_MOOD_IDLE,
    BUDDY_MOOD_RELAXED,
    BUDDY_MOOD_PEUKIE,
    BUDDY_MOOD_ANGRY,
    BUDDY_MOOD_SLEEPY,
    BUDDY_MOOD_WORKING,
    BUDDY_MOOD_HAPPY,
    BUDDY_MOOD_COUNT,
} buddy_mood_t;

// The stable names used by the serial protocol and the companion ("idle", "relaxed", ...).
const char *buddy_mood_name(buddy_mood_t mood);

// Looks a name up. Returns false (and leaves *out alone) for anything that is not a mood.
bool buddy_mood_parse(const char *name, buddy_mood_t *out);

// The face that stands for a mood in pictures of Buddy (the touchscreen menu, the companion's icons): a typical
// expression from its loop, whether the antenna light is on, and whether a cigarette hangs from his mouth.
typedef struct {
    buddy_eyes_t eyes;
    buddy_mouth_t mouth;
    bool lit;
    bool cig;
} buddy_face_t;

buddy_face_t buddy_mood_face(buddy_mood_t mood);

// Where a mood's loop is.
typedef struct {
    uint32_t t;     // ms into the current cycle
    uint32_t len;   // length of the current cycle (buddy_mood_cycle_ms)
    uint32_t n;     // cycle number since the mood started (or resumed after a reaction)
    uint32_t seed;  // varies the details of this cycle
    uint32_t total; // ms since the mood started
} buddy_mood_clock_t;

// How long a cycle with this seed lasts.
uint32_t buddy_mood_cycle_ms(buddy_mood_t mood, uint32_t seed);

// Writes the pose of a mood other than idle (idle's blinks live in the brain). Returns ms until it may change
// next (>= 1).
uint32_t buddy_mood_pose(buddy_mood_t mood, const buddy_mood_clock_t *clock, buddy_pose_t *pose);

#ifdef __cplusplus
}
#endif
