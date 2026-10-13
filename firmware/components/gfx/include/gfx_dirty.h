// Dirty-rectangle set: the screen regions that must be redrawn.
//
// Invariants after every call:
// - every rectangle lies inside the screen;
// - rectangles are pairwise disjoint, so each pixel is redrawn at most once;
// - x and w are multiples of x_align (except where clipped by the right screen edge);
// - the set covers every rectangle that was added (merging only ever grows coverage).
// With x_align = 32 on RGB565, every redraw starts and ends on a 64-byte PSRAM cache line.
#pragma once

#include "gfx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GFX_DIRTY_MAX 16

typedef struct {
    gfx_rect_t rect[GFX_DIRTY_MAX];
    int count;
    gfx_rect_t screen;
    int x_align;
} gfx_dirty_t;

void gfx_dirty_init(gfx_dirty_t *d, int screen_w, int screen_h, int x_align);
void gfx_dirty_clear(gfx_dirty_t *d);

// Adds r (clipped to the screen). Overlapping rectangles are merged into their bounding box; when the set is
// full, r is merged with the rectangle whose bounding box grows least.
void gfx_dirty_add(gfx_dirty_t *d, gfx_rect_t r);

// Adds every rectangle of src.
void gfx_dirty_add_all(gfx_dirty_t *d, const gfx_dirty_t *src);

// Total number of pixels covered.
int gfx_dirty_area(const gfx_dirty_t *d);

#ifdef __cplusplus
}
#endif
