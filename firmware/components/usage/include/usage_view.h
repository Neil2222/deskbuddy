// The Claude usage strip: one sprite at the bottom of the character stage, in the same place in normal mode and in
// Clock Mode (below the LED sign, which sits higher in both, and below Buddy's NEE/JA buttons). One segment per window
// the companion sent, the 5-hour one first:
//
//   5U [bar] 37% > 130%  2:30  |  7D [bar] 20% > 90%  3d 2u
//
// the label, a small bar filled to min(pct, 100) %, the pct now, the prediction for the end of the window (only when
// there is one) and the countdown to its reset (only while the device clock is set; usage.h). The bar and the
// prediction take the colour of their pct (green below 50, yellow below 75, orange below 90, red from 90); the rest is
// the UI's neutral and dim text on a dark plate. The sign's 5x7 font (marquee.h) at scale 2.
//
// The strip is redrawn only when what it says changes (the sprite alternates between two pixel buffers, so the scene
// sees the change and redraws just the strip), which is at most once a minute for the countdowns.
//
// Hardware independent; host-tested.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx_scene.h"
#include "usage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USAGE_VIEW_SCALE 2
#define USAGE_VIEW_IMG_W 320 // image pixels at most (the stage's width, if that is less)...
#define USAGE_VIEW_IMG_H 11  // ...edge, padding, the font's 7 rows, padding, edge
#define USAGE_VIEW_H (USAGE_VIEW_IMG_H * USAGE_VIEW_SCALE)
#define USAGE_VIEW_BOTTOM 2 // screen pixels between the strip and the bottom of the stage
#define USAGE_VIEW_MEMORY (2 * USAGE_VIEW_IMG_W * USAGE_VIEW_IMG_H) // bytes of pixels usage_view_init() needs
#define USAGE_VIEW_KEY_MAX 64

typedef struct {
    gfx_sprite_t sprite;
    gfx_image_t image[2];
    uint8_t *px[2];
    int cur;                         // image in use
    int img_w;                       // image width: USAGE_VIEW_IMG_W, or less on a narrow stage
    int plate_x, plate_w;            // the plate drawn in image[cur] (image pixels)
    char shown[USAGE_VIEW_KEY_MAX];  // what image[cur] says; "": nothing yet
} usage_view_t;

// Where the strip goes on this stage (screen pixels); the plate is centred in it.
gfx_rect_t usage_view_area(gfx_rect_t stage);

// mem: USAGE_VIEW_MEMORY bytes, kept for the view's lifetime (read whenever the strip redraws: internal RAM if there
// is room). Adds the (hidden) strip to the scene at depth z. Returns false without memory or when the scene is full.
bool usage_view_init(usage_view_t *v, gfx_scene_t *scene, gfx_rect_t stage, uint8_t *mem, int z);

// Shows what u says (NULL, or no window: hidden). utc_ms: the device clock (UTC ms), or < 0 while it is not set (no
// countdowns). Returns ms until the strip changes on its own (a countdown); UINT32_MAX when it does not.
uint32_t usage_view_apply(usage_view_t *v, const usage_t *u, int64_t utc_ms);

#ifdef __cplusplus
}
#endif
