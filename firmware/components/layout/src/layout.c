#include "layout.h"

#define DIVIDER_PX 2

layout_t layout_compute(int screen_w, int screen_h, int stage_percent, bool panel_left, int align)
{
    if (stage_percent < 10) {
        stage_percent = 10;
    }
    if (stage_percent > 100) {
        stage_percent = 100;
    }
    if (align < 1) {
        align = 1;
    }
    int stage_w = (screen_w * stage_percent / 100 + align / 2) / align * align;
    if (stage_w > screen_w) {
        stage_w = screen_w;
    }
    const int panel_w = screen_w - stage_w;
    layout_t l = {.screen = {0, 0, screen_w, screen_h}};
    if (panel_left) {
        l.panel = (gfx_rect_t){0, 0, panel_w, screen_h};
        l.stage = (gfx_rect_t){panel_w, 0, stage_w, screen_h};
    } else {
        l.stage = (gfx_rect_t){0, 0, stage_w, screen_h};
        l.panel = (gfx_rect_t){stage_w, 0, panel_w, screen_h};
    }
    return l;
}

gfx_rect_t layout_panel_content(const layout_t *l)
{
    gfx_rect_t r = l->panel;
    if (gfx_rect_is_empty(r)) {
        return r;
    }
    r.w -= DIVIDER_PX; // the divider sits on the panel side of the boundary (see layout_background)
    if (r.x >= l->stage.x) {
        r.x += DIVIDER_PX;
    }
    return r;
}

#define STAGE_BANDS 8

// Stage background: flat horizontal bands from (30, 36, 62) at the top to (54, 64, 100) at the bottom. Bands
// rather than a smooth ramp: RGB565 steps red, green and blue on different rows, which shows as tinted stripes.
static gfx_color_t stage_color(const layout_t *l, int y)
{
    const int h = l->stage.h > 0 ? l->stage.h : 1;
    const int band = (y - l->stage.y) * STAGE_BANDS / h;
    const int d = STAGE_BANDS - 1;
    return GFX_RGB(30 + 24 * band / d, 36 + 28 * band / d, 62 + 38 * band / d);
}

// Fills the part of screen columns [from, to) that falls inside the row segment dst covers ([x, x + w)).
static void fill_span(gfx_color_t *dst, int x, int w, int from, int to, gfx_color_t c)
{
    if (from < x) {
        from = x;
    }
    if (to > x + w) {
        to = x + w;
    }
    for (int sx = from; sx < to; sx++) {
        dst[sx - x] = c;
    }
}

void layout_background(void *ctx, gfx_color_t *dst, int x, int y, int w)
{
    const layout_t *l = ctx;
    fill_span(dst, x, w, l->stage.x, l->stage.x + l->stage.w, stage_color(l, y));
    if (gfx_rect_is_empty(l->panel)) {
        return;
    }
    fill_span(dst, x, w, l->panel.x, l->panel.x + l->panel.w, GFX_RGB(20, 23, 36));
    // The divider sits on the panel side of the boundary, so the stage keeps its full width.
    const int div_x = l->panel.x < l->stage.x ? l->stage.x - DIVIDER_PX : l->panel.x;
    fill_span(dst, x, w, div_x, div_x + DIVIDER_PX, GFX_RGB(64, 74, 108));
}
