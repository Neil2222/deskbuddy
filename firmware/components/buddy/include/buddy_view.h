// Maps a pose onto scene sprites. Knows where the character stands and how big it is, nothing about behavior.
#pragma once

#include "buddy_art.h"
#include "gfx_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

// The telescopic arm and the cable are drawn procedurally into an indexed image covering this art-pixel area (from
// the mouth, where the hand brings a cigarette, out to the can).
#define BUDDY_RIG_X 14
#define BUDDY_RIG_Y 4
#define BUDDY_RIG_W 36
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
    // Mood effects.
    gfx_sprite_t part[BUDDY_PARTICLES];
    // KITT's scanner over the face screen (buddy_kitt.h). Its two alternating images borrow the rig's pixel
    // buffers: the rig is drawn only from props, which KITT's poses never have.
    gfx_sprite_t kitt;
    gfx_image_t kitt_img[2];
    const buddy_art_t *art;
    int scale;
    int x0, y0; // screen position of the art origin at lift 0
    int home_scale, home_x0, home_y0; // his place on the stage (the camera moves him, see buddy_view_camera)
    gfx_rect_t stage;
    bool closeup; // the pose's close-up holds the camera (buddy_pose_t.closeup)
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

// The camera: draws the character at `scale` (1..127) with his art origin at screen point (x0, y0), e.g. to zoom in
// on his face. Takes effect with the next buddy_view_apply(); the caller holds the camera from then on (a pose's
// close-up no longer moves it back). buddy_view_camera_home() puts him back on his stage.
void buddy_view_camera(buddy_view_t *v, int scale, int x0, int y0);
void buddy_view_camera_home(buddy_view_t *v);

#ifdef __cplusplus
}
#endif
