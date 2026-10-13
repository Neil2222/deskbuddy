// Frame-based sprite animation: a sequence of images with per-frame durations.
#pragma once

#include "gfx_image.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const gfx_image_t *image;
    uint16_t ms; // how long this frame is shown
} gfx_anim_frame_t;

typedef struct {
    const gfx_anim_frame_t *frames;
    uint8_t count;
    bool loop; // otherwise the last frame is held once the sequence ends
} gfx_anim_t;

// Image to show elapsed_ms after the animation started. If next_change_ms is not NULL it receives the time until
// the image changes, or UINT32_MAX if it never changes again.
const gfx_image_t *gfx_anim_image(const gfx_anim_t *a, uint32_t elapsed_ms, uint32_t *next_change_ms);

#ifdef __cplusplus
}
#endif
