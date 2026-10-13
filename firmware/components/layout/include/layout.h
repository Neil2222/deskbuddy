// Screen layout: the character stage and the area reserved for the future touchscreen control panel.
// The rendering engine knows nothing about this; it only sees the background callback and sprite positions.
#pragma once

#include "gfx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gfx_rect_t screen;
    gfx_rect_t stage; // where the character lives
    gfx_rect_t panel; // reserved for the control panel (empty background for now)
} layout_t;

// Splits the screen vertically. stage_percent (clamped to 10..100) is the stage's share of the width; the boundary
// is rounded to a multiple of align so stage and panel redraws never share a dirty-rectangle column.
layout_t layout_compute(int screen_w, int screen_h, int stage_percent, bool panel_left, int align);

// gfx_background_fn for a layout_t (ctx): banded gradient on the stage, a flat panel, and a divider line.
void layout_background(void *ctx, gfx_color_t *dst, int x, int y, int w);

#ifdef __cplusplus
}
#endif
