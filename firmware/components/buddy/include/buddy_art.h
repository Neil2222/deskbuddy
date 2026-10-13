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

typedef struct {
    int x, y;
} buddy_point_t;

typedef struct {
    gfx_image_t body, antenna_on, antenna_off, shadow, shadow_small;
    gfx_image_t eyes[BUDDY_EYES_COUNT];
    gfx_image_t mouth[BUDDY_MOUTH_COUNT];
    gfx_palette_t palette;
    gfx_anim_t antenna[2]; // indexed by buddy_antenna_t

    buddy_point_t body_at, antenna_at, eye_at[2], mouth_at, shadow_at;
    int w, h; // art bounding box at lift 0, including the shadow
} buddy_art_t;

// Decodes the artwork on the first call. Returns NULL if the built-in art is malformed (a programming error,
// caught by the host tests).
const buddy_art_t *buddy_art_get(void);

#ifdef __cplusplus
}
#endif
