#include "gfx_anim.h"

const gfx_image_t *gfx_anim_image(const gfx_anim_t *a, uint32_t elapsed_ms, uint32_t *next_change_ms)
{
    uint32_t total = 0;
    for (int i = 0; i < a->count; i++) {
        total += a->frames[i].ms;
    }
    if (a->count == 0) {
        if (next_change_ms) {
            *next_change_ms = UINT32_MAX;
        }
        return NULL;
    }
    if (a->count == 1 || total == 0 || (!a->loop && elapsed_ms >= total)) {
        if (next_change_ms) {
            *next_change_ms = UINT32_MAX;
        }
        return a->frames[a->count - 1].image;
    }
    uint32_t t = a->loop ? elapsed_ms % total : elapsed_ms;
    for (int i = 0; i < a->count; i++) {
        if (t < a->frames[i].ms) {
            if (next_change_ms) {
                *next_change_ms = a->frames[i].ms - t;
            }
            return a->frames[i].image;
        }
        t -= a->frames[i].ms;
    }
    // Unreachable: t < total. Keep the compiler and the caller safe anyway.
    if (next_change_ms) {
        *next_change_ms = UINT32_MAX;
    }
    return a->frames[a->count - 1].image;
}
