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
//   ask       "PEUKIE ROKEN?": he holds out a cigarette and looks hopefully from NEE to JA (the buttons are drawn by
//             the touchscreen menu, menu_ask.h); impatient towards the end. buddy_brain_answer() ends it: yes is
//             happy hops and a smoke break (the peukie mood), no (or a minute without an answer) makes him sad
//   confirm   "ZE DOEN MAAR WAT?": the same waiting (a smirk, no cigarette) before a ZDMW click goes to PDC Manager
//             through the companion (menu_ask.h shows the question; pdc.h owns the request). NEE or 15 s: nothing
//   zdmw      PDC Manager confirmed the click: a jolt, then a rant (angry eyes, red face, waving hand, bouncing,
//             anger marks), then a sigh. About 2.6 s
//   focus     after MonkeyUser's "Focus": deep in thought he builds a plan (A -> B -> C, a flowchart, E) in the air
//             above him, tongue out. "HEY! DO YOU HAVE 1 SEC?" from off-stage; he glares... POOF, the plan goes up
//             in smoke. "NEVERMIND". Then he floats, blank, among the pieces: "WHAT WAS I DOING?" (pose.thought;
//             menu_thought.h draws it). About 9 s
//
// Pure logic: no rendering, no clock of its own. Adding one: a buddy_state_t, a buddy_event_t, its length and pose
// function here, and an item in the touchscreen menu (menu_items.c).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buddy_brain.h"
#include "buddy_mood.h"
#include "buddy_pose.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUDDY_WATER_MS 5000u
#define BUDDY_SNEEZE_MS 2700u
#define BUDDY_STARE_MS 5200u
#define BUDDY_MEDITATE_MS 7400u
#define BUDDY_ASK_MS 60000u          // "PEUKIE ROKEN?": how long he waits for an answer (then: as if no)
#define BUDDY_SAD_MS 3000u           // after no
#define BUDDY_SMOKE_BREAK_MS 120000u // after yes: the peukie mood for this long, then back to his mood
#define BUDDY_CONFIRM_MS 15000u      // "ZE DOEN MAAR WAT?": how long the confirmation waits (then: as if NEE)
#define BUDDY_ZDMW_MS 2600u          // PDC Manager confirmed the click: his exasperated reaction
#define BUDDY_FOCUS_MS 9000u

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

// The activities by the names the protocol uses (act name=, hello acts=, evt act name=), in display order: the shows
// and "ask_peukie" (the question). The Monster sequence and the tap reaction have their own commands (monster, poke)
// and are not listed.
#define BUDDY_ACTS 6
const char *buddy_act_name(int i);                       // i < BUDDY_ACTS
bool buddy_act_parse(const char *name, buddy_event_t *out); // false (and *out untouched) for an unknown name
const char *buddy_act_event_name(buddy_event_t ev);      // the name of an event that is one of them; NULL otherwise

// The face that stands for an activity in pictures of Buddy (the touchscreen menu's icons, the companion's tiles).
buddy_face_t buddy_act_face(buddy_event_t ev);

#ifdef __cplusplus
}
#endif
