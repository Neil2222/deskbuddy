// KITT: Buddy turns into a Knight Rider scanner, and stays one until a tap on him (or another mood) turns him back.
//
//   in    his eyes close and the face's glow dims; red traces race across the face screen, leaving it black; a
//         short flicker; the scanner's dark-red housing comes up and its light starts to sweep (BUDDY_KITT_IN_MS)
//   scan  the red light sweeps left and right with a fading tail, easing at both ends (BUDDY_KITT_SWEEP_MS a round
//         trip), for as long as he stays KITT. The antenna is lit red
//   out   the sweep slows to the middle and fades, the housing goes dark, red traces race back and bring his face
//         back; he opens his eyes (BUDDY_KITT_OUT_MS), and his mood carries on
//
// Pure logic and drawing: no clock of its own. buddy_brain.c plays it (BUDDY_STATE_KITT); the pose carries only the
// times (buddy_kitt_t), from which buddy_kitt_draw() draws the face screen's overlay (buddy_view.c shows it).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buddy_pose.h"
#include "gfx_image.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUDDY_KITT_IN_MS 2000u
#define BUDDY_KITT_OUT_MS 1800u
#define BUDDY_KITT_SWEEP_MS 1600u // the scanner's round trip, left to right and back
#define BUDDY_KITT_FRAME_MS 30u   // while something moves

// The pose t ms after he began turning into KITT; out: t at which he began turning back (0: not yet; t >= out
// plays the way back). Writes over the zeroed *pose. Returns ms until the pose may change next (>= 1).
uint32_t buddy_kitt_pose(uint32_t t, uint32_t out, buddy_pose_t *pose);

// Draws the overlay for the face screen into px (w x h indices of buddy_kitt_palette(), row by row; 0 transparent):
// w and h are the face screen's size in art pixels times 1 or 2 (finer than his art pixels where the scale allows).
void buddy_kitt_draw(const buddy_kitt_t *k, uint8_t *px, int w, int h);

const gfx_palette_t *buddy_kitt_palette(void);

#ifdef __cplusplus
}
#endif
