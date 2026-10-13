// Maps a pose onto scene sprites. Knows where the character stands and how big it is, nothing about behavior.
#pragma once

#include "buddy_art.h"
#include "gfx_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gfx_sprite_t shadow, body, antenna, eye[2], mouth;
    const buddy_art_t *art;
    int scale;
    int x0, y0; // screen position of the art origin at lift 0
    uint32_t anim_start;
} buddy_view_t;

// Places the character centred in `stage` (screen pixels) at the largest integer scale that keeps it within
// about 45 % of the stage width and 60 % of its height, and adds its sprites to the scene at z..z+2.
// Returns false if the art is unavailable, the stage is too small, or the scene is full.
bool buddy_view_init(buddy_view_t *v, gfx_scene_t *scene, gfx_rect_t stage, int z, uint32_t now_ms);

// Applies a pose. Returns ms until the view's own cosmetic animation (antenna light) changes next.
uint32_t buddy_view_apply(buddy_view_t *v, const buddy_pose_t *pose, uint32_t now_ms);

// True when (x, y) touches the character as currently drawn.
bool buddy_view_hit(const buddy_view_t *v, int x, int y);

#ifdef __cplusplus
}
#endif
