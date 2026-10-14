// Maps a pose onto scene sprites. Knows where the character stands and how big it is, nothing about behavior.
#pragma once

#include "buddy_art.h"
#include "gfx_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

// The telescopic arm and the cable are drawn procedurally into an indexed image covering this art-pixel area.
#define BUDDY_RIG_X 22
#define BUDDY_RIG_Y 4
#define BUDDY_RIG_W 28
#define BUDDY_RIG_H 32
// The drawing is published as two sprites, cut at this art column: the part next to the robot and the part out
// towards the can. The cut lies just right of the trembling robot, so the dirty rectangle of the far part stays
// separate from the robot's instead of being merged into one wide bounding box with it.
#define BUDDY_RIG_SPLIT 32

// Two alternating output images per part: a redrawn part always gets a different image pointer from the one on
// screen, so gfx_scene_collect() sees the change even when the bounds stay the same.
typedef struct {
    uint8_t px[2][BUDDY_RIG_W * BUDDY_RIG_H];
    gfx_image_t image[2];
    int cur;  // image in use, -1 = nothing drawn
    int x, y; // art position of image[cur]'s top-left corner
} buddy_rig_part_t;

typedef struct {
    uint8_t work[BUDDY_RIG_W * BUDDY_RIG_H];
    buddy_rig_part_t part[2]; // near, far
} buddy_rig_t;

typedef struct {
    gfx_sprite_t shadow, torso, head, antenna, eye[2], mouth;
    // Monster props.
    gfx_sprite_t can_shadow, can, port, rig[2], battery, alert, bolt[BUDDY_MAX_BOLTS], fx[BUDDY_FX_COUNT];
    const buddy_art_t *art;
    int scale;
    int x0, y0; // screen position of the art origin at lift 0
    uint32_t anim_start;
    buddy_rig_t rig_px;
} buddy_view_t;

// Places the character centred in `stage` (screen pixels) at the largest integer scale that keeps it within
// about 45 % of the stage width and 60 % of its height, and adds its sprites to the scene at z..z+4.
// Returns false if the art is unavailable, the stage is too small (including room for the props), or the scene
// is full.
bool buddy_view_init(buddy_view_t *v, gfx_scene_t *scene, gfx_rect_t stage, int z, uint32_t now_ms);

// Applies a pose. Returns ms until the view's own cosmetic animation (antenna light) changes next.
uint32_t buddy_view_apply(buddy_view_t *v, const buddy_pose_t *pose, uint32_t now_ms);

// Hides every sprite of the character and its props (e.g. for Office Mode). The next buddy_view_apply() shows
// the character again.
void buddy_view_hide(buddy_view_t *v);

// True when (x, y) touches the character as currently drawn.
bool buddy_view_hit(const buddy_view_t *v, int x, int y);

// A Monster can on a plate in the control panel: tap it to give DeskBuddy a drink.
typedef struct {
    gfx_sprite_t plate, can;
} buddy_button_t;

// Centres the button in `area` at the given scale (reduced if it does not fit), sprites at z and z+1.
bool buddy_button_init(buddy_button_t *b, gfx_scene_t *scene, gfx_rect_t area, int scale, int z);

// The plate shows while `visible`; the can on it while it is also available (it is gone while DeskBuddy is
// drinking it). Hidden buttons cannot be hit.
void buddy_button_apply(buddy_button_t *b, bool visible, bool available);

bool buddy_button_hit(const buddy_button_t *b, int x, int y);

#ifdef __cplusplus
}
#endif
