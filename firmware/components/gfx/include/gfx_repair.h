// Double-buffer repair. With two frame buffers that alternate, the back buffer last showed the frame before the
// previous one, so it misses the previous frame's changes as well as the current ones. Redrawing the union of both
// brings it fully up to date without copying anything from the front buffer.
//
// Call gfx_repair_next() once per presented frame, and only for frames that are actually presented (skipped idle
// frames keep the history unchanged, which stays correct because the buffers only alternate on present).
#pragma once

#include "gfx_dirty.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gfx_dirty_t prev; // changes of the last presented frame
} gfx_repair_t;

void gfx_repair_init(gfx_repair_t *r, int screen_w, int screen_h, int x_align);

// Sets out (initialised with the same screen and alignment) to changes plus the previous frame's changes, then
// remembers changes for the next call.
void gfx_repair_next(gfx_repair_t *r, const gfx_dirty_t *changes, gfx_dirty_t *out);

#ifdef __cplusplus
}
#endif
