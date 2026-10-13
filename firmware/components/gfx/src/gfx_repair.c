#include "gfx_repair.h"

void gfx_repair_init(gfx_repair_t *r, int screen_w, int screen_h, int x_align)
{
    gfx_dirty_init(&r->prev, screen_w, screen_h, x_align);
}

void gfx_repair_next(gfx_repair_t *r, const gfx_dirty_t *changes, gfx_dirty_t *out)
{
    gfx_dirty_clear(out);
    gfx_dirty_add_all(out, changes);
    gfx_dirty_add_all(out, &r->prev);
    r->prev = *changes;
}
