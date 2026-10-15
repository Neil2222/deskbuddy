// Activities: short shows Buddy puts on top of his mood, then hands back to it (like the Monster sequence in
// buddy_monster.h, which counts as one too). Each is a fixed choreography, a pure function of the time since it
// started, built from the shared pieces in buddy_pose.h:
//
//   water     proudly raises a glass, gulps it down, looks at it... remembers he is a robot. Short circuit: sparks,
//             bolts, electrocuted flashes; smoke from both ears, a cough. The sign: NOTE TO SELF: ROBOTS + WATER = NO
//   sneeze    an itch, "a... a...", ACHOO: his head flies off his neck, hangs there, drops back with a clunk and a
//             puff of dust; a dazed wobble, a sheepish smile
//   stare     a staring contest: the camera pushes in until his head fills the stage; he stares, completely still,
//             an eye twitches, a drop of sweat, eyes forced wide... he blinks. Red with fury, back to his place,
//             fuming. The sign: BEST OF THREE?
//   meditate  eyes closed, he floats up and hovers, sparkles drifting by. The sign: OMMMMM. A notification jolts him:
//             wide eyes, strobing antenna, and he drops to the ground with a thud
//
// Pure logic: no rendering, no clock of its own. Adding one: a buddy_state_t, a buddy_event_t, its length and pose
// function here, and an item in the touchscreen menu (menu_items.c).
#pragma once

#include <stdint.h>

#include "buddy_pose.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUDDY_WATER_MS 5000u
#define BUDDY_SNEEZE_MS 2700u
#define BUDDY_STARE_MS 5200u
#define BUDDY_MEDITATE_MS 7400u

// Lines Buddy puts on the LED sign (buddy_pose_t.say).
typedef enum {
    BUDDY_SAY_NONE,
    BUDDY_SAY_WATER,
    BUDDY_SAY_OM,
    BUDDY_SAY_REMATCH,
    BUDDY_SAY_COUNT,
} buddy_say_t;

const char *buddy_say_text(buddy_say_t say);

// How long the activity played in `state` lasts (the Monster sequence included); 0 for a state that is not one.
uint32_t buddy_activity_ms(buddy_state_t state);

// Writes the pose t ms into the activity (t < buddy_activity_ms(state)) over the zeroed *pose (its state set).
// Returns ms until the pose may change next (>= 1).
uint32_t buddy_activity_pose(buddy_state_t state, uint32_t t, buddy_pose_t *pose);

// What he is doing, for the touchscreen menu's footer ("drinking"); NULL when `state` is not an activity.
const char *buddy_activity_doing(buddy_state_t state);

#ifdef __cplusplus
}
#endif
