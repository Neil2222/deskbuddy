// The character's pose: what the behavior wants to show, independent of how it is drawn.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUDDY_STATE_IDLE,
    BUDDY_STATE_SURPRISED, // short reaction right after a touch
    BUDDY_STATE_HAPPY,     // follows the surprise, then back to idle
} buddy_state_t;

typedef enum {
    BUDDY_EYES_OPEN,
    BUDDY_EYES_HALF,
    BUDDY_EYES_CLOSED,
    BUDDY_EYES_HAPPY,
    BUDDY_EYES_WIDE,
    BUDDY_EYES_COUNT,
} buddy_eyes_t;

typedef enum {
    BUDDY_MOUTH_NEUTRAL,
    BUDDY_MOUTH_SMILE,
    BUDDY_MOUTH_OPEN,
    BUDDY_MOUTH_COUNT,
} buddy_mouth_t;

typedef enum {
    BUDDY_ANTENNA_CALM,
    BUDDY_ANTENNA_EXCITED,
} buddy_antenna_t;

#define BUDDY_MAX_LIFT 4 // art pixels

typedef struct {
    buddy_state_t state;
    buddy_eyes_t eyes;
    buddy_mouth_t mouth;
    buddy_antenna_t antenna;
    uint8_t lift; // how far the robot is lifted off the ground, in art pixels (0..BUDDY_MAX_LIFT)
} buddy_pose_t;

#ifdef __cplusplus
}
#endif
