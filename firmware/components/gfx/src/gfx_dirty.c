#include "gfx_dirty.h"

void gfx_dirty_init(gfx_dirty_t *d, int screen_w, int screen_h, int x_align)
{
    d->count = 0;
    d->screen = (gfx_rect_t){0, 0, screen_w, screen_h};
    d->x_align = x_align > 0 ? x_align : 1;
}

void gfx_dirty_clear(gfx_dirty_t *d)
{
    d->count = 0;
}

static gfx_rect_t align_x(const gfx_dirty_t *d, gfx_rect_t r)
{
    const int a = d->x_align;
    const int x0 = r.x - r.x % a; // r.x >= 0 after clipping
    int x1 = (r.x + r.w + a - 1) / a * a;
    if (x1 > d->screen.x + d->screen.w) {
        x1 = d->screen.x + d->screen.w;
    }
    r.x = x0;
    r.w = x1 - x0;
    return r;
}

// Two rectangles are merged when they overlap, or when they touch such that their bounding box is exactly
// their combined area (merging then costs nothing and keeps the set small).
static bool mergeable(gfx_rect_t a, gfx_rect_t b)
{
    if (gfx_rect_overlaps(a, b)) {
        return true;
    }
    return gfx_rect_area(gfx_rect_union(a, b)) == gfx_rect_area(a) + gfx_rect_area(b);
}

static void remove_at(gfx_dirty_t *d, int i)
{
    d->rect[i] = d->rect[--d->count];
}

void gfx_dirty_add(gfx_dirty_t *d, gfx_rect_t r)
{
    r = gfx_rect_intersect(r, d->screen);
    if (gfx_rect_is_empty(r)) {
        return;
    }
    r = align_x(d, r);
    // Each pass either finishes or removes one rectangle, so this terminates within count + 1 passes.
    for (;;) {
        int hit = -1;
        for (int i = 0; i < d->count; i++) {
            if (gfx_rect_contains_rect(d->rect[i], r)) {
                return; // already covered
            }
            if (mergeable(d->rect[i], r)) {
                hit = i;
                break;
            }
        }
        if (hit < 0 && d->count < GFX_DIRTY_MAX) {
            d->rect[d->count++] = r;
            return;
        }
        if (hit < 0) {
            // Full: merge with the rectangle whose bounding box grows least.
            int best_growth = 0;
            for (int i = 0; i < d->count; i++) {
                const int growth = gfx_rect_area(gfx_rect_union(d->rect[i], r)) - gfx_rect_area(d->rect[i]);
                if (hit < 0 || growth < best_growth) {
                    hit = i;
                    best_growth = growth;
                }
            }
        }
        // The union may now overlap other rectangles: loop to merge those too.
        r = gfx_rect_union(d->rect[hit], r);
        remove_at(d, hit);
    }
}

void gfx_dirty_add_all(gfx_dirty_t *d, const gfx_dirty_t *src)
{
    for (int i = 0; i < src->count; i++) {
        gfx_dirty_add(d, src->rect[i]);
    }
}

int gfx_dirty_area(const gfx_dirty_t *d)
{
    int area = 0;
    for (int i = 0; i < d->count; i++) {
        area += gfx_rect_area(d->rect[i]);
    }
    return area;
}
