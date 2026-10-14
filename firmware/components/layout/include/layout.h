// Screen layout: the character stage and the touchscreen control panel beside it.
// The rendering engine knows nothing about this; it only sees the background callback and sprite positions.
#pragma once

#include "gfx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gfx_rect_t screen;
    gfx_rect_t stage; // where the character lives
    gfx_rect_t panel; // the control panel (the touchscreen menu), including its divider
} layout_t;

// Splits the screen vertically. stage_percent (clamped to 10..100) is the stage's share of the width; the boundary
// is rounded to a multiple of align so stage and panel redraws never share a dirty-rectangle column.
layout_t layout_compute(int screen_w, int screen_h, int stage_percent, bool panel_left, int align);

// The panel without its divider: where the control panel's content goes.
gfx_rect_t layout_panel_content(const layout_t *l);

// gfx_background_fn for a layout_t (ctx): banded gradient on the stage, a flat panel, and a divider line.
void layout_background(void *ctx, gfx_color_t *dst, int x, int y, int w);

#ifdef __cplusplus
}
#endif
