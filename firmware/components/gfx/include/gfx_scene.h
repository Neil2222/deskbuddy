// Scene compositor: a background plus z-ordered sprites, redrawn only inside dirty rectangles.
//
// Rendering composes each dirty rectangle in horizontal strips inside a caller-provided work buffer (fast internal
// RAM on the device): background first, then sprites in z order. Each finished row is copied once into the target
// surface, so the target (the PSRAM frame buffer) is only ever written, never read, and transparency and overdraw
// cost nothing there.
//
// Change tracking: callers mutate the public sprite fields directly. gfx_scene_collect() compares each sprite with
// what was last committed to the screen and reports the old and new bounds of everything that changed.
#pragma once

#include "gfx_dirty.h"
#include "gfx_image.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GFX_SCENE_MAX_SPRITES 56

#define GFX_SPRITE_HIDDEN (1u << 0)
#define GFX_SPRITE_FLIP_X (1u << 1)

typedef struct {
    // Public state.
    const gfx_image_t *image;
    const gfx_palette_t *palette;
    int x, y;      // top-left corner on screen, in screen pixels
    uint8_t scale; // integer scale factor, >= 1
    uint8_t flags; // GFX_SPRITE_*

    // Private: state as last committed by gfx_scene_collect().
    struct {
        const gfx_image_t *image;
        const gfx_palette_t *palette;
        gfx_rect_t bounds;
        uint8_t flags;
    } shown;
} gfx_sprite_t;

// Fills w pixels of screen row y, starting at screen column x.
typedef void (*gfx_background_fn)(void *ctx, gfx_color_t *dst, int x, int y, int w);

typedef struct {
    int w, h;
    gfx_background_fn background;
    void *background_ctx;
    gfx_color_t *strip;
    int strip_px;
    gfx_sprite_t *sprite[GFX_SCENE_MAX_SPRITES]; // in draw order
    int z[GFX_SCENE_MAX_SPRITES];
    int count;
    gfx_dirty_t pending; // invalidated regions not yet collected
} gfx_scene_t;

// strip is the composition work buffer; it must hold at least one full screen row (strip_px >= w).
// x_align must match the dirty sets passed to gfx_scene_collect(). Returns false on invalid arguments.
bool gfx_scene_init(gfx_scene_t *s, int w, int h, gfx_background_fn background, void *background_ctx,
                    gfx_color_t *strip, int strip_px, int x_align);

// Adds a sprite; higher z is drawn on top, equal z in insertion order. The sprite must stay valid while in the
// scene. Returns false when the scene is full.
bool gfx_scene_add(gfx_scene_t *s, gfx_sprite_t *sprite, int z);

// Marks a region for redraw, e.g. the whole screen on the first frame or after a background change.
void gfx_scene_invalidate(gfx_scene_t *s, gfx_rect_t r);

// Adds everything that changed since the last collect to out, then commits the current state as shown.
void gfx_scene_collect(gfx_scene_t *s, gfx_dirty_t *out);

// Redraws the regions in rects into dst (dst must be at least the scene size).
void gfx_scene_render(const gfx_scene_t *s, const gfx_dirty_t *rects, const gfx_surface_t *dst);

// Screen area a sprite currently covers; empty when hidden or without an image.
gfx_rect_t gfx_sprite_bounds(const gfx_sprite_t *sp);

#ifdef __cplusplus
}
#endif
