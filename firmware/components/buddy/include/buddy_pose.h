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
    BUDDY_STATE_IDLE,
    BUDDY_STATE_SURPRISED, // short reaction right after a touch
    BUDDY_STATE_HAPPY,     // follows the surprise, then back to idle
    BUDDY_STATE_MONSTER,   // drinking a Monster Energy can through a cable (see buddy_monster.h)
} buddy_state_t;

typedef enum {
    BUDDY_EYES_OPEN,
    BUDDY_EYES_HALF,
    BUDDY_EYES_CLOSED,
    BUDDY_EYES_HAPPY,
    BUDDY_EYES_WIDE,
    BUDDY_EYES_BOLT, // lightning symbols (overcharged)
    BUDDY_EYES_COUNT,
} buddy_eyes_t;

typedef enum {
    BUDDY_MOUTH_NEUTRAL,
    BUDDY_MOUTH_SMILE,
    BUDDY_MOUTH_OPEN,
    BUDDY_MOUTH_ZAP,  // jittery squiggle while being charged
    BUDDY_MOUTH_GRIN, // huge energized grin
    BUDDY_MOUTH_COUNT,
} buddy_mouth_t;

typedef enum {
    BUDDY_ANTENNA_CALM,
    BUDDY_ANTENNA_EXCITED,
    BUDDY_ANTENNA_OVERLOAD, // rapid red/green/white strobe
    BUDDY_ANTENNA_COUNT,
} buddy_antenna_t;

// Color scheme of the robot itself.
typedef enum {
    BUDDY_TINT_NORMAL,
    BUDDY_TINT_GLOW, // face glows neon green
    BUDDY_TINT_ZAP,  // electrocuted negative flash
    BUDDY_TINT_COUNT,
} buddy_tint_t;

#define BUDDY_MAX_LIFT 4 // art pixels

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
    BUDDY_HAND_HOME, // retracted into the arm (not drawn)
    BUDDY_HAND_SHOW, // raised in front of him
    BUDDY_HAND_CAN,  // at the top of the can
    BUDDY_HAND_PORT, // at the charging port on the side of his head
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
} buddy_props_t;

typedef struct {
    buddy_state_t state;
    buddy_eyes_t eyes;
    buddy_mouth_t mouth;
    buddy_antenna_t antenna;
    uint8_t lift; // how far the robot is lifted off the ground, in art pixels (0..BUDDY_MAX_LIFT)
    int8_t shake;  // sideways offset of the whole robot in art pixels (-1..1)
    int8_t wobble; // extra sideways offset of the head alone (-1..1): trembling on the neck, cheap to redraw
    int8_t look;  // sideways eye offset within the face (-1..1)
    buddy_tint_t tint;
    buddy_props_t props;
} buddy_pose_t;

#ifdef __cplusplus
}
#endif
