// The Mechanical Skeleton's drawing code, for the development PC only (not built into the firmware): it draws the
// face's static artwork, which tools/host-test.ps1 -Assets packs into office_skeleton_assets.c and the host tests
// compare with that file.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "office_skeleton.h"
#include "office_skeleton_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    office_skeleton_layout_t layout;
    uint8_t *px[OFFICE_SKELETON_IMAGES]; // art pixels (palette indices), layout.w[i] x layout.h[i]
    uint8_t *frame;                      // the full-screen frame, frame_w x frame_h
    int frame_w, frame_h;
} office_skeleton_art_t;

// Draws everything (heap memory; free with office_skeleton_art_free). Slow on purpose: supersampled and shaded.
bool office_skeleton_art_draw(office_skeleton_art_t *a);

void office_skeleton_art_free(office_skeleton_art_t *a);

#ifdef __cplusplus
}
#endif
