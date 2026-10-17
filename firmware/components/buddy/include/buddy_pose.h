// The character's pose: what the behavior wants to show, independent of how it is drawn.
//
// Everything is semantic ("the hand is at the can", "the cable runs from the can to the port"); the view turns it
// into geometry and sprites. An all-zero props block means "no extra props".
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUDDY_STATE_IDLE,      // no reaction running: the current mood plays (see buddy_mood.h)
    BUDDY_STATE_SURPRISED, // short reaction right after a touch
    BUDDY_STATE_HAPPY,     // follows the surprise, then back to idle
    BUDDY_STATE_MONSTER,   // drinking a Monster Energy can through a cable (see buddy_monster.h)
    BUDDY_STATE_WATER,     // the activities in buddy_activity.h: a glass of water (a bad idea for a robot)
    BUDDY_STATE_SNEEZE,    // a sneeze that blows his head off
    BUDDY_STATE_STARE,     // a staring contest with the viewer
    BUDDY_STATE_MEDITATE,  // floating in meditation, until a notification
    BUDDY_STATE_ASK,       // asks a question ("PEUKIE ROKEN?") and waits for an answer (buddy_brain_answer)
    BUDDY_STATE_SAD,       // the answer was no (or none came): briefly sad
    BUDDY_STATE_CONFIRM,   // asks to confirm a ZDMW click ("ZE DOEN MAAR WAT?"), waits for buddy_brain_answer
    BUDDY_STATE_ZDMW,      // PDC Manager confirmed the click: exasperated, briefly
    BUDDY_STATE_FOCUS,     // deep in thought, building a plan... until someone asks "do you have 1 sec?"
    BUDDY_STATE_KITT,      // turned into a Knight Rider scanner, until a tap or a mood brings him back (buddy_kitt.h)
} buddy_state_t;

typedef enum {
    BUDDY_EYES_OPEN,
    BUDDY_EYES_HALF,
    BUDDY_EYES_CLOSED,
    BUDDY_EYES_HAPPY,
    BUDDY_EYES_WIDE,
    BUDDY_EYES_BOLT,    // lightning symbols (overcharged)
    BUDDY_EYES_LIDDED,  // heavy upper lids: relaxed, content
    BUDDY_EYES_ANGRY,   // brows slanting down towards the nose (the right eye is mirrored)
    BUDDY_EYES_SQUEEZE, // squeezed shut: > < (mirrored)
    BUDDY_EYES_FOCUS,   // narrowed, concentrating
    BUDDY_EYES_COUNT,
} buddy_eyes_t;

typedef enum {
    BUDDY_MOUTH_NEUTRAL,
    BUDDY_MOUTH_SMILE,
    BUDDY_MOUTH_OPEN,
    BUDDY_MOUTH_ZAP,    // jittery squiggle while being charged
    BUDDY_MOUTH_GRIN,   // huge energized grin
    BUDDY_MOUTH_SMIRK,  // lopsided, satisfied
    BUDDY_MOUTH_FROWN,
    BUDDY_MOUTH_GRIT,   // clenched teeth
    BUDDY_MOUTH_TONGUE, // tongue out in concentration
    BUDDY_MOUTH_PUFF,   // small "o": blowing out smoke
    BUDDY_MOUTH_COUNT,
} buddy_mouth_t;

typedef enum {
    BUDDY_ANTENNA_CALM,
    BUDDY_ANTENNA_EXCITED,
    BUDDY_ANTENNA_OVERLOAD, // rapid red/green/white strobe
    BUDDY_ANTENNA_DIM,      // a short faint blink now and then (sleepy)
    BUDDY_ANTENNA_STEADY,   // lit red, without blinking (KITT)
    BUDDY_ANTENNA_COUNT,
} buddy_antenna_t;

// Color scheme of the robot itself.
typedef enum {
    BUDDY_TINT_NORMAL,
    BUDDY_TINT_GLOW,   // face glows neon green
    BUDDY_TINT_ZAP,    // electrocuted negative flash
    BUDDY_TINT_FURY,   // face screen flushes red
    BUDDY_TINT_DROWSY, // face glow turned down
    BUDDY_TINT_DARK,   // face screen switched off (going into Office Mode): the clock's background colour
    BUDDY_TINT_COUNT,
} buddy_tint_t;

#define BUDDY_MAX_LIFT 4  // art pixels: hops and jumps (his place on the stage leaves room for this much)
#define BUDDY_MAX_HOVER 6 // art pixels: floating (meditation)
#define BUDDY_MAX_POP 10  // art pixels: how far a sneeze can blow his head off his neck

// --- Monster props -------------------------------------------------------------------------------------------

typedef enum {
    BUDDY_PHASE_NONE,
    BUDDY_PHASE_DISCOVER,   // a can drops in, he notices it and gets excited
    BUDDY_PHASE_PREPARE,    // produces a cable and plugs one end into the can
    BUDDY_PHASE_CONNECT,    // plugs the other end into the port on his head
    BUDDY_PHASE_CHARGE,     // green energy flows, sparks, shaking, battery fills
    BUDDY_PHASE_OVERCHARGE, // far too much energy
    BUDDY_PHASE_FINISH,     // unplug, empty can, happy hops
    BUDDY_PHASE_COUNT,
} buddy_phase_t;

typedef enum {
    BUDDY_CAN_NONE,
    BUDDY_CAN_FULL,    // standing; how much is left is in can_level
    BUDDY_CAN_CRUSHED, // empty and crumpled
} buddy_can_t;

#define BUDDY_CAN_LEVELS 5 // can_level 0 (empty) .. 4 (full)

// Where the telescopic hand is.
typedef enum {
    BUDDY_HAND_HOME,  // retracted into the arm (not drawn)
    BUDDY_HAND_SHOW,  // raised in front of him
    BUDDY_HAND_CAN,   // at the top of the can
    BUDDY_HAND_PORT,  // at the charging port on the side of his head
    BUDDY_HAND_MOUTH, // holding something to his mouth
    BUDDY_HAND_REST,  // lowered beside his head, holding something
    BUDDY_HAND_SIP,   // holding a glass to his mouth
} buddy_hand_t;

typedef enum {
    BUDDY_CABLE_NONE,
    BUDDY_CABLE_COILED,      // rolled up in the hand
    BUDDY_CABLE_HELD,        // both ends in the hand, a loop hanging down
    BUDDY_CABLE_CAN_TO_HAND, // plugged into the can, other end in the hand
    BUDDY_CABLE_CAN_TO_PORT, // connected: can to head
    BUDDY_CABLE_DROPPED,     // plugged into the can, other end lying on the ground
} buddy_cable_t;

typedef enum {
    BUDDY_FX_NONE,
    BUDDY_FX_SPARK, // electric spark, frames 0..BUDDY_SPARK_FRAMES-1
    BUDDY_FX_POOF,  // dust cloud, frames 0..BUDDY_POOF_FRAMES-1
} buddy_fx_kind_t;

#define BUDDY_SPARK_FRAMES 3
#define BUDDY_POOF_FRAMES 2

typedef enum {
    BUDDY_SPOT_CAN_TOP,
    BUDDY_SPOT_CAN_BASE,
    BUDDY_SPOT_CAN,
    BUDDY_SPOT_PORT,
    BUDDY_SPOT_HAND,
    BUDDY_SPOT_NECK_L, // either side of where the head sits on the neck (moves with the head)
    BUDDY_SPOT_NECK_R,
    BUDDY_SPOT_FEET_L, // on the ground, either side of his feet
    BUDDY_SPOT_FEET_R,
} buddy_spot_t;

typedef struct {
    buddy_fx_kind_t kind;
    buddy_spot_t at;
    uint8_t frame;
} buddy_fx_t;

#define BUDDY_FX_COUNT 2
#define BUDDY_MAX_BOLTS 4
#define BUDDY_BATTERY_SEGMENTS 4
#define BUDDY_BATTERY_OVER (BUDDY_BATTERY_SEGMENTS + 2) // battery value of the first overflow frame (two frames)
#define BUDDY_BATTERY_FRAMES (BUDDY_BATTERY_OVER + 1)    // battery values 1..BUDDY_BATTERY_FRAMES

// --- Mood props: reusable effects ------------------------------------------------------------------------------

// Small floating effects. Any mood (and later any event) can use them; the view draws each kind the same way
// wherever it appears.
typedef enum {
    BUDDY_PART_NONE,
    BUDDY_PART_SMOKE,   // a curl of smoke: frames 0 (wisp) .. 2 (thinning out)
    BUDDY_PART_STEAM,   // a puff of steam: frames 0 (puff), 1 (fading)
    BUDDY_PART_Z,       // a floating Z: frames 0 (small), 1 (big)
    BUDDY_PART_SWEAT,   // a drop of sweat
    BUDDY_PART_SPARKLE, // a twinkling star: frames 0 (small), 1 (big)
    BUDDY_PART_ANGER,   // the cartoon anger mark: frames 0 (small), 1 (big)
    BUDDY_PART_COUNT,
} buddy_part_kind_t;

// Where a particle starts. Head points move with the head (shake, wobble, lift, nod), the others with the body.
typedef enum {
    BUDDY_FROM_EMBER,  // body: the tip of the cigarette in the resting hand
    BUDDY_FROM_MOUTH,  // head
    BUDDY_FROM_HEAD_L, // head: just outside the top left corner
    BUDDY_FROM_HEAD_R, // head: just outside the top right corner
    BUDDY_FROM_ABOVE,  // head: above, right of the antenna
    BUDDY_FROM_BROW,   // head: the left edge of the head, at forehead height
    BUDDY_FROM_COUNT,
} buddy_from_t;

typedef struct {
    uint8_t kind;   // buddy_part_kind_t; BUDDY_PART_NONE = slot unused
    uint8_t from;   // buddy_from_t
    uint8_t frame;
    int8_t dx, dy;  // art pixels from the start point (up is negative); the image is centred there
} buddy_particle_t;

#define BUDDY_PARTICLES 5
#define BUDDY_CIG_LENGTHS 4
#define BUDDY_GLASS_LEVELS 4 // props.glass 1 + water left: 1 (empty) .. BUDDY_GLASS_LEVELS (full)

typedef struct {
    buddy_phase_t phase;
    buddy_can_t can;
    uint8_t can_drop;  // art pixels above the ground while falling in
    uint8_t can_level; // 0..BUDDY_CAN_LEVELS-1
    buddy_hand_t hand_from, hand_to;
    uint8_t hand_mix; // 0 = at hand_from .. 255 = at hand_to
    buddy_cable_t cable;
    bool energized;   // green energy flows from the can to the head
    uint16_t flow;    // how far the energy pulses have travelled, in art pixels
    bool port;        // charging port open on the side of the head
    bool alert;       // "!" above the head
    uint8_t battery;  // 0 = hidden; 1 + filled segments; BUDDY_BATTERY_OVER(+1) = overflowing
    uint8_t bolts;    // lightning bolts around the head, 0..BUDDY_MAX_BOLTS
    uint16_t strike;  // changes whenever the bolts strike anew (selects their places and shapes)
    buddy_fx_t fx[BUDDY_FX_COUNT];
    // Mood props.
    bool cig;         // a cigarette in the hand (hand_from/hand_to/hand_mix say where the hand is)
    uint8_t cig_left; // how much is left: 0 (nearly a butt) .. BUDDY_CIG_LENGTHS-1 (fresh)
    uint8_t ember;    // 0 smouldering, 1 glowing, 2 lit up by a drag
    uint8_t glass;    // a glass in the hand: 0 none, 1 empty .. BUDDY_GLASS_LEVELS full
    buddy_particle_t particle[BUDDY_PARTICLES];
} buddy_props_t;

// What goes on in his head, drawn on the stage around him (FOCUS, after the MonkeyUser comic): the plan he builds,
// how it goes up in smoke, and the words around it.
typedef enum {
    BUDDY_SPEECH_NONE,
    BUDDY_SPEECH_HEY,       // someone off-stage: "HEY! DO YOU HAVE 1 SEC?"
    BUDDY_SPEECH_NEVERMIND, // ...and then: "NEVERMIND"
    BUDDY_SPEECH_WHAT,      // Buddy, lost: "WHAT WAS I DOING?"
} buddy_speech_t;

#define BUDDY_PLAN_STEPS 8

typedef struct {
    uint8_t plan;   // how much of the plan is drawn: 0 nothing .. BUDDY_PLAN_STEPS (nodes, arrows, a flowchart)
    uint8_t poof;   // the plan goes up in smoke: 0 not, 1..2 the burst
    uint8_t speech; // buddy_speech_t
    uint8_t drift;  // what is left of the plan floats about him: 0 not, 1.. each step a little further
} buddy_thought_t;

// KITT's scanner, drawn over the face screen (buddy_kitt.h draws it from these alone).
typedef struct {
    bool on;      // drawn at all
    uint32_t t;   // ms since he began turning into KITT
    uint32_t out; // t at which he began turning back; 0: not (yet)
} buddy_kitt_t;

typedef struct {
    buddy_state_t state;
    buddy_eyes_t eyes;
    buddy_mouth_t mouth;
    buddy_antenna_t antenna;
    uint8_t lift; // how far the robot is lifted off the ground, in art pixels (0..BUDDY_MAX_HOVER)
    int8_t shake;  // sideways offset of the whole robot in art pixels (-1..1)
    int8_t wobble; // extra sideways offset of the head alone (-1..1): trembling on the neck, cheap to redraw
    int8_t look;  // sideways eye offset within the face (-1..1)
    uint8_t nod;  // the head sinks onto the neck by this many art pixels (0..1): drooping, dozing off
    uint8_t pop;  // the head flies up off the neck by this many art pixels (0..BUDDY_MAX_POP): a sneeze
    uint8_t closeup; // the camera pushes in on his face: 0 his place on the stage .. 255 his head filling the stage
    uint8_t say;  // buddy_say_t (buddy_activity.h): a line for the LED sign, from the moment it appears; 0 none
    buddy_tint_t tint;
    buddy_props_t props;
    buddy_thought_t thought;
    buddy_kitt_t kitt;
} buddy_pose_t;

#ifdef __cplusplus
}
#endif
