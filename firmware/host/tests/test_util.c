#include "test_util.h"

uint32_t test_rand(uint32_t *state)
{
    uint32_t x = *state ? *state : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

int test_rand_range(uint32_t *state, int lo, int hi)
{
    return lo + (int)(test_rand(state) % (uint32_t)(hi - lo + 1));
}

bool test_dirty_covers(const gfx_dirty_t *d, int x, int y)
{
    for (int i = 0; i < d->count; i++) {
        if (gfx_rect_contains_point(d->rect[i], x, y)) {
            return true;
        }
    }
    return false;
}

const char *test_dirty_violation(const gfx_dirty_t *d)
{
    if (d->count < 0 || d->count > GFX_DIRTY_MAX) {
        return "count out of range";
    }
    for (int i = 0; i < d->count; i++) {
        const gfx_rect_t r = d->rect[i];
        if (gfx_rect_is_empty(r)) {
            return "empty rectangle stored";
        }
        if (!gfx_rect_contains_rect(d->screen, r)) {
            return "rectangle outside the screen";
        }
        if (r.x % d->x_align != 0) {
            return "x not aligned";
        }
        if ((r.x + r.w) % d->x_align != 0 && r.x + r.w != d->screen.x + d->screen.w) {
            return "right edge not aligned";
        }
        for (int j = i + 1; j < d->count; j++) {
            if (gfx_rect_overlaps(r, d->rect[j])) {
                return "rectangles overlap";
            }
        }
    }
    return NULL;
}

gfx_color_t test_background_at(int x, int y)
{
    return (gfx_color_t)(0x8000u | ((unsigned)y << 7) | (unsigned)x);
}

void test_background(void *ctx, gfx_color_t *dst, int x, int y, int w)
{
    (void)ctx;
    for (int i = 0; i < w; i++) {
        dst[i] = test_background_at(x + i, y);
    }
}

gfx_color_t test_reference_pixel(const gfx_scene_t *s, int x, int y)
{
    gfx_color_t c = test_background_at(x, y);
    for (int i = 0; i < s->count; i++) { // draw order: later wins
        const gfx_sprite_t *sp = s->sprite[i];
        if (!sp->image || (sp->flags & GFX_SPRITE_HIDDEN) || sp->scale == 0) {
            continue;
        }
        const int dx = x - sp->x, dy = y - sp->y;
        if (dx < 0 || dy < 0 || dx >= sp->image->w * sp->scale || dy >= sp->image->h * sp->scale) {
            continue;
        }
        int col = dx / sp->scale;
        if (sp->flags & GFX_SPRITE_FLIP_X) {
            col = sp->image->w - 1 - col;
        }
        const uint8_t idx = sp->image->px[(dy / sp->scale) * sp->image->w + col];
        if (idx != 0) {
            c = sp->palette->colors[idx];
        }
    }
    return c;
}
