#include "gfx_scene.h"

#include <string.h>

bool gfx_scene_init(gfx_scene_t *s, int w, int h, gfx_background_fn background, void *background_ctx,
                    gfx_color_t *strip, int strip_px, int x_align)
{
    if (!s || w <= 0 || h <= 0 || !background || !strip || strip_px < w) {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->w = w;
    s->h = h;
    s->background = background;
    s->background_ctx = background_ctx;
    s->strip = strip;
    s->strip_px = strip_px;
    gfx_dirty_init(&s->pending, w, h, x_align);
    return true;
}

bool gfx_scene_add(gfx_scene_t *s, gfx_sprite_t *sprite, int z)
{
    if (s->count >= GFX_SCENE_MAX_SPRITES) {
        return false;
    }
    int i = s->count;
    while (i > 0 && s->z[i - 1] > z) {
        s->sprite[i] = s->sprite[i - 1];
        s->z[i] = s->z[i - 1];
        i--;
    }
    s->sprite[i] = sprite;
    s->z[i] = z;
    s->count++;
    memset(&sprite->shown, 0, sizeof(sprite->shown)); // nothing of it is on screen yet
    return true;
}

void gfx_scene_invalidate(gfx_scene_t *s, gfx_rect_t r)
{
    gfx_dirty_add(&s->pending, r);
}

gfx_rect_t gfx_sprite_bounds(const gfx_sprite_t *sp)
{
    if (!sp->image || (sp->flags & GFX_SPRITE_HIDDEN) || sp->scale == 0) {
        return (gfx_rect_t){0, 0, 0, 0};
    }
    return (gfx_rect_t){sp->x, sp->y, sp->image->w * sp->scale, sp->image->h * sp->scale};
}

void gfx_scene_collect(gfx_scene_t *s, gfx_dirty_t *out)
{
    gfx_dirty_add_all(out, &s->pending);
    gfx_dirty_clear(&s->pending);
    for (int i = 0; i < s->count; i++) {
        gfx_sprite_t *sp = s->sprite[i];
        const gfx_rect_t now = gfx_sprite_bounds(sp);
        const bool visible = !gfx_rect_is_empty(now);
        const bool was_visible = !gfx_rect_is_empty(sp->shown.bounds);
        if (!visible && !was_visible) {
            continue;
        }
        if (visible && was_visible && gfx_rect_equal(now, sp->shown.bounds) && sp->image == sp->shown.image &&
            sp->palette == sp->shown.palette && sp->flags == sp->shown.flags) {
            continue;
        }
        gfx_dirty_add(out, sp->shown.bounds);
        gfx_dirty_add(out, now);
        sp->shown.image = sp->image;
        sp->shown.palette = sp->palette;
        sp->shown.bounds = now;
        sp->shown.flags = sp->flags;
    }
}

// Draws the part of sp inside clip (screen coordinates) into buf, whose pixel [0] is screen (buf_x, buf_y).
static void draw_sprite(const gfx_sprite_t *sp, gfx_rect_t clip, gfx_color_t *buf, int buf_stride, int buf_x,
                        int buf_y)
{
    const gfx_image_t *img = sp->image;
    const gfx_color_t *colors = sp->palette->colors;
    const int scale = sp->scale;
    const bool flip = (sp->flags & GFX_SPRITE_FLIP_X) != 0;
    const int local_x = clip.x - sp->x;        // first column, in scaled sprite space
    const int first_src = local_x / scale;     // source column before flipping
    const int first_run = scale - local_x % scale;

    for (int y = clip.y; y < clip.y + clip.h; y++) {
        const uint8_t *src = img->px + (size_t)((y - sp->y) / scale) * img->w;
        gfx_color_t *out = buf + (size_t)(y - buf_y) * buf_stride + (clip.x - buf_x);
        int remaining = clip.w;
        int sx = first_src;
        int run = first_run;
        while (remaining > 0) {
            const int n = run < remaining ? run : remaining;
            const uint8_t idx = src[flip ? img->w - 1 - sx : sx];
            if (idx) {
                const gfx_color_t c = colors[idx];
                for (int k = 0; k < n; k++) {
                    out[k] = c;
                }
            }
            out += n;
            remaining -= n;
            sx++;
            run = scale;
        }
    }
}

static void render_rect(const gfx_scene_t *s, gfx_rect_t r, const gfx_surface_t *dst)
{
    // Sprites that touch this rectangle, in draw order.
    const gfx_sprite_t *hits[GFX_SCENE_MAX_SPRITES];
    gfx_rect_t bounds[GFX_SCENE_MAX_SPRITES];
    int n_hits = 0;
    for (int i = 0; i < s->count; i++) {
        const gfx_rect_t b = gfx_rect_intersect(gfx_sprite_bounds(s->sprite[i]), r);
        if (!gfx_rect_is_empty(b)) {
            hits[n_hits] = s->sprite[i];
            bounds[n_hits] = b;
            n_hits++;
        }
    }

    const int rows_per_strip = s->strip_px / r.w;
    for (int y0 = r.y; y0 < r.y + r.h; y0 += rows_per_strip) {
        const int rows = r.y + r.h - y0 < rows_per_strip ? r.y + r.h - y0 : rows_per_strip;
        for (int row = 0; row < rows; row++) {
            s->background(s->background_ctx, s->strip + (size_t)row * r.w, r.x, y0 + row, r.w);
        }
        const gfx_rect_t band = {r.x, y0, r.w, rows};
        for (int i = 0; i < n_hits; i++) {
            const gfx_rect_t clip = gfx_rect_intersect(bounds[i], band);
            if (!gfx_rect_is_empty(clip)) {
                draw_sprite(hits[i], clip, s->strip, r.w, r.x, y0);
            }
        }
        for (int row = 0; row < rows; row++) {
            gfx_color_t *line = dst->px + (size_t)(y0 + row) * dst->stride + r.x;
            memcpy(line, s->strip + (size_t)row * r.w, (size_t)r.w * sizeof(gfx_color_t));
            if (dst->flush) {
                dst->flush(line, r.w);
            }
        }
    }
}

void gfx_scene_render(const gfx_scene_t *s, const gfx_dirty_t *rects, const gfx_surface_t *dst)
{
    for (int i = 0; i < rects->count; i++) {
        const gfx_rect_t r = gfx_rect_intersect(rects->rect[i], (gfx_rect_t){0, 0, s->w, s->h});
        if (!gfx_rect_is_empty(r)) {
            render_rect(s, r, dst);
        }
    }
}
