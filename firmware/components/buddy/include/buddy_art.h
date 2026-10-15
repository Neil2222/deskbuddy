// Placeholder robot artwork (programmer art), decoded from ASCII at startup.
//
// All coordinates are in art pixels relative to the art origin (top-left of the antenna row, lift 0). The real
// character later replaces this module; the view and behavior only rely on buddy_art_t.
#pragma once

#include "buddy_pose.h"
#include "gfx_anim.h"
#include "gfx_image.h"

#ifdef __cplusplus
extern "C" {
#endif

// Palette indices the view draws with directly (procedural arm and cable).
enum {
    BUDDY_PX_CLEAR = 0,
    BUDDY_PX_OUTLINE = 1,
    BUDDY_PX_BODY = 2,
    BUDDY_PX_BODY_SHADE = 3,
    BUDDY_PX_WHITE = 4,
    BUDDY_PX_SCREEN = 5, // the face screen
    BUDDY_PX_YELLOW = 8,
    BUDDY_PX_NEON = 12,
    BUDDY_PX_CABLE = 18,
};

#define BUDDY_PART_FRAMES 3 // most frames any particle kind has
#define BUDDY_EMBERS 3      // buddy_props_t.ember values

#define BUDDY_BOLT_SHAPES 2
#define BUDDY_BOLT_SLOTS 6

typedef struct {
    int x, y;
} buddy_point_t;

// Props for the Monster sequence. Points marked "body" move with the robot (lift, shake), "head" ones also with
// the head (wobble, nod); the others stand on the ground.
typedef struct {
    gfx_image_t can[BUDDY_CAN_LEVELS], can_crushed, can_shadow;
    gfx_image_t port, plug, coil, hand, alert;
    gfx_image_t bolt[BUDDY_BOLT_SHAPES];
    gfx_image_t spark[BUDDY_SPARK_FRAMES], poof[BUDDY_POOF_FRAMES];
    gfx_image_t battery[BUDDY_BATTERY_FRAMES]; // indexed by buddy_props_t.battery - 1

    // Mood props: the cigarette by length and ember, and the particle kinds (part_frames[kind] frames each).
    gfx_image_t cig[BUDDY_CIG_LENGTHS][BUDDY_EMBERS];
    gfx_image_t part[BUDDY_PART_COUNT][BUDDY_PART_FRAMES];
    uint8_t part_frames[BUDDY_PART_COUNT];

    buddy_point_t can_at;     // top-left of the full can
    buddy_point_t plug_can;   // top-left of the plug standing in the can
    buddy_point_t hand_can;   // hand position that holds a plug just above plug_can
    buddy_point_t port_at;    // head
    buddy_point_t plug_port;  // head: top-left of the plug in the port
    buddy_point_t arm_root;   // body: where the telescopic arm comes out
    buddy_point_t hand_show;  // body
    buddy_point_t hand_port;  // head: hand position that holds a plug just beside plug_port
    buddy_point_t hand_mouth; // head: hand position that holds the cigarette's filter in the mouth
    buddy_point_t hand_rest;  // body: the hand lowered beside the head
    buddy_point_t from[BUDDY_FROM_COUNT]; // particle start points (BUDDY_FROM_EMBER is worked out from hand_rest)
    buddy_point_t battery_at;
    buddy_point_t alert_at;   // body
    buddy_point_t bolt_at[BUDDY_BOLT_SLOTS]; // body
    int ground_y;  // lowest art row on the ground (bottom of the feet)
    int cable_len; // art pixels
} buddy_props_art_t;

// A cigarette is held this many art pixels right of its filter end, one row above the hand point (between the
// claw's prongs); its glowing tip is the rightmost pixel.
#define BUDDY_CIG_GRIP 2

typedef struct {
    gfx_image_t head, torso, antenna_on, antenna_off, antenna_green, antenna_white, shadow, shadow_small;
    gfx_image_t eyes[BUDDY_EYES_COUNT];
    bool eye_mirror[BUDDY_EYES_COUNT]; // the right eye shows the image flipped (asymmetric expressions)
    gfx_image_t mouth[BUDDY_MOUTH_COUNT];
    gfx_palette_t palette;               // props and everything else
    gfx_palette_t tint[BUDDY_TINT_COUNT]; // the robot's own parts, by buddy_tint_t
    gfx_anim_t antenna[BUDDY_ANTENNA_COUNT];

    // The body is two images, so the head can wobble on its own: head (antenna stem down to the bottom of the face
    // box) and torso (neck down to the feet), stacked at head_at and torso_at.
    buddy_point_t head_at, torso_at, antenna_at, eye_at[2], mouth_at, shadow_at;
    int w, h; // art bounding box at lift 0, including the shadow
    buddy_props_art_t props;
} buddy_art_t;

// Decodes the artwork on the first call. Returns NULL if the built-in art is malformed (a programming error,
// caught by the host tests).
const buddy_art_t *buddy_art_get(void);

#ifdef __cplusplus
}
#endif
