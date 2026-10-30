// The PC load strip: one sprite at the bottom of the character stage, drawn like the Claude usage strip (usage_view.h)
// and stacked with it: in its place while that one is hidden, directly above it while it shows. One segment per stat
// the companion sent:
//
//   CPU [bar] 12%  |  RAM [bar] 45%  |  C: [bar] 85%  120 GB vrij
//
// the label, a small bar filled to the pct, the pct, and for the disk what is free (from 1000 GB on in TB, one
// decimal). The bar takes the colour of its pct (as usage_level: green below 50, yellow below 75, orange below 90, red
// from 90); the rest is the UI's neutral and dim text on a dark plate.
//
// Redrawn only when what it says changes (the sprite alternates between two pixel buffers): at most once per sys.set.
//
// Hardware independent; host-tested.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx_scene.h"
#include "sysinfo.h"
#include "usage_view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYSINFO_VIEW_GAP 2 // screen pixels between this strip and the usage strip below it
#define SYSINFO_VIEW_MEMORY USAGE_VIEW_MEMORY // bytes of pixels sysinfo_view_init() needs
#define SYSINFO_VIEW_KEY_MAX 48

typedef struct {
    gfx_sprite_t sprite;
    gfx_image_t image[2];
    uint8_t *px[2];
    int cur;                           // image in use
    int img_w;                         // image width: USAGE_VIEW_IMG_W, or less on a narrow stage
    int plate_x, plate_w;              // the plate drawn in image[cur] (image pixels)
    int y[2];                          // the sprite's y: in the usage strip's place, above it
    char shown[SYSINFO_VIEW_KEY_MAX];  // what image[cur] says; "": nothing yet
} sysinfo_view_t;

// Where the strip goes on this stage (screen pixels): in the usage strip's place, or raised above it. The plate is
// centred in it.
gfx_rect_t sysinfo_view_area(gfx_rect_t stage, bool raised);

// mem: SYSINFO_VIEW_MEMORY bytes, kept for the view's lifetime (read whenever the strip redraws: internal RAM if there
// is room). Adds the (hidden) strip to the scene at depth z. Returns false without memory or when the scene is full.
bool sysinfo_view_init(sysinfo_view_t *v, gfx_scene_t *scene, gfx_rect_t stage, uint8_t *mem, int z);

// Shows what s says (NULL, or no stat: hidden), raised above the usage strip or in its place.
void sysinfo_view_apply(sysinfo_view_t *v, const sysinfo_t *s, bool raised);

#ifdef __cplusplus
}
#endif
