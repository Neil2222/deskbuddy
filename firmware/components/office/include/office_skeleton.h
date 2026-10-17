// Clock Mode's Mechanical Skeleton face: a miniature steampunk movement around a dark enamel timeplate.
//
// Layered brass and bronze wheels with teeth, curved crossings, steel pinions and jewelled pivots turn in front of
// a frosted bronze frame, under a cock over the escapement; HH:MM stands in four recessed windows on the timeplate,
// the date on a plaque under
// it. Everything is procedural pixel art at scale 2, rendered once by office_skeleton_init() (no image assets).
//
// Movement, all derived from the wall-clock time (so a late frame simply shows the right position):
//   every second   the escape wheel advances a quarter tooth and the anchor rocks to the other side
//   every 5 s      the left train advances a quarter tooth; the right train every 15 s, never in the same second
//                  (a reduction gear train; and only one train's area is redrawn at a time)
//   minute change  the changed digits roll up in their windows like a mechanical counter (two in-between frames)
//   on the hour    the whole movement whirs for a moment (OFFICE_SKELETON_FLOURISH_STEPS quick steps)
// Every wheel needs only OFFICE_SKELETON_FRAMES images: meshing wheels share their tooth movement, so a quarter
// tooth is the same step for all of them, and four quarters bring each wheel back to the same picture.
//
// Layering (z, z + 1, ...): the frame; the wheels and the anchor; the cock; the timeplate with its plaque; the
// digits and the date. The timeplate covers every digit window, so no wheel ever shows through the time.
//
// Hardware independent like office_clock: the caller supplies the time, the scene and the memory.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "gfx_scene.h"
#include "office_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OFFICE_SKELETON_MEMORY (560 * 1024) // bytes office_skeleton_init() needs (pixels; PSRAM is fine)
#define OFFICE_SKELETON_GEARS 7             // six train wheels and the escape wheel
#define OFFICE_SKELETON_FRAMES 4            // quarter-tooth positions per wheel
#define OFFICE_SKELETON_TICK_FRAME_MS 60    // the minute roll: two in-between frames this long
#define OFFICE_SKELETON_FLOURISH_STEPS 20   // on the hour: quick steps (a multiple of FRAMES: no jump afterwards)
#define OFFICE_SKELETON_FLOURISH_MS 80      // ...one every this many ms

typedef struct {
    float x, y; // centre on screen (pixels)
    float r;    // radius of the shadow it casts on the background (pixels)
} office_skeleton_shadow_t;

typedef struct {
    gfx_sprite_t frame, gear[OFFICE_SKELETON_GEARS], anchor, cock, plate, digit[4], date;
    gfx_image_t gear_img[OFFICE_SKELETON_GEARS][OFFICE_SKELETON_FRAMES];
    gfx_image_t frame_img, anchor_img, cock_img, plate_img;
    gfx_image_t glyph[11];      // '0'..'9', then '-'
    gfx_image_t roll_img[4][2]; // the minute roll, double-buffered per digit
    uint8_t *roll_px[4][2];
    gfx_image_t date_img[2];
    uint8_t *date_px[2];
    int cell_w, cell_h, date_w, date_h;
    char date_text[OFFICE_DATE_MAX];
    int8_t dir[OFFICE_SKELETON_GEARS]; // turning direction: +1 / -1
    office_skeleton_shadow_t shadow[OFFICE_SKELETON_GEARS + 1]; // the wheels', then the timeplate's (x, y: corner)
    gfx_rect_t plate_shadow;
    gfx_color_t colors[48], ink[9], date_ink[OFFICE_INK_LEVELS + 1];
    gfx_palette_t pal, ink_pal, date_pal;
    gfx_rect_t screen;
    size_t mem_used;
    bool visible;
} office_skeleton_t;

// The face's background (ctx: the office_skeleton_t): the clock's near-black with the soft shadows the wheels and
// the timeplate cast on the movement's back.
void office_skeleton_background(void *ctx, gfx_color_t *dst, int x, int y, int w);

// Renders the face into mem (OFFICE_SKELETON_MEMORY bytes, kept for the face's lifetime), lays it out centred in
// `screen` (designed for 1024 x 600; smaller screens are refused) and adds its sprites to the scene from z up,
// hidden. Returns false if the screen is too small, the memory too short or the scene full.
bool office_skeleton_init(office_skeleton_t *k, gfx_scene_t *scene, gfx_rect_t screen, uint8_t *mem,
                          size_t mem_size, int z);

void office_skeleton_show(office_skeleton_t *k, bool visible);

// Shows tm (local time) at ms into its second, or the stopped "not set" face for NULL. Returns ms until it changes.
uint32_t office_skeleton_apply(office_skeleton_t *k, const struct tm *tm, uint32_t ms);

#ifdef __cplusplus
}
#endif
