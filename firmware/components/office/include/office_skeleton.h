// Clock Mode's Mechanical Skeleton face: a miniature steampunk movement around a dark enamel timeplate.
//
// Layered brass and bronze wheels with teeth, curved crossings, steel pinions and jewelled pivots turn in front of
// a frosted bronze frame, under a cock over the escapement; HH:MM stands in four recessed windows on the timeplate,
// the date on a plaque under it. The artwork is procedural pixel art at scale 2, drawn on the development PC
// (office_skeleton_art.c) and built into the firmware as packed data (office_skeleton_assets.h): the board only
// unpacks it at start.
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
// Layering: the frame is part of the face's background (it never moves, and most of it is empty, so it is kept as
// runs of visible pixels and only the rows being redrawn are read); then, from z up, the wheels and the anchor; the
// cock; the timeplate with its plaque; the digits and the date. The timeplate covers every digit window, so no wheel
// ever shows through the time.
//
// Hardware independent like office_clock: the caller supplies the time, the scene and the memory.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "gfx_scene.h"
#include "office_clock.h"
#include "office_skeleton_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OFFICE_SKELETON_MEMORY (400 * 1024) // bytes office_skeleton_init() needs (pixels; PSRAM is fine)
#define OFFICE_SKELETON_GEARS OFFICE_SKELETON_WHEELS
#define OFFICE_SKELETON_FRAMES OFFICE_SKELETON_STEPS
#define OFFICE_SKELETON_TICK_FRAME_MS 60  // the minute roll: two in-between frames this long
#define OFFICE_SKELETON_FLOURISH_STEPS 20 // on the hour: quick steps (a multiple of FRAMES: no jump afterwards)
#define OFFICE_SKELETON_FLOURISH_MS 80    // ...one every this many ms
#define OFFICE_SKELETON_DATE_STYLE {.unit = 2.0f, .stroke = 1.8f, .tracking = 9.0f}

// Called after the face has written pixels into its memory (at start, and when a digit rolls or the date changes),
// so a platform with a write-back cache can write them out at once (see app_port_flush). May be NULL.
typedef void (*office_skeleton_flush_fn)(const void *px, size_t bytes);

typedef struct {
    float x, y; // centre on screen (pixels)
    float r;    // radius of the shadow it casts on the background (pixels)
} office_skeleton_shadow_t;

typedef struct {
    gfx_sprite_t gear[OFFICE_SKELETON_GEARS], anchor, cock, plate, digit[4], date;
    gfx_image_t gear_img[OFFICE_SKELETON_GEARS][OFFICE_SKELETON_FRAMES];
    gfx_image_t anchor_img, cock_img, plate_img;
    gfx_image_t glyph[11];      // '0'..'9', then '-'
    gfx_image_t roll_img[4][2]; // the minute roll, double-buffered per digit
    uint8_t *roll_px[4][2];
    gfx_image_t date_img[2];
    uint8_t *date_px[2];
    int cell_w, cell_h, date_w, date_h;
    char date_text[OFFICE_DATE_MAX];
    int8_t dir[OFFICE_SKELETON_GEARS]; // turning direction: +1 / -1
    office_skeleton_shadow_t shadow[OFFICE_SKELETON_GEARS];
    gfx_rect_t plate_shadow;
    int ox, oy;                // the design screen's top left on the screen
    const uint32_t *frame_row; // the frame's runs (office_skeleton_frame_t)
    const uint8_t *frame_runs;
    int frame_h;
    gfx_color_t colors[OFFICE_SKELETON_P_COUNT], ink[9], date_ink[OFFICE_INK_LEVELS + 1];
    gfx_palette_t pal, ink_pal, date_pal;
    office_skeleton_flush_fn flush;
    gfx_rect_t screen;
    size_t mem_used;
    bool frame_copied; // the frame's runs were copied into frame memory
    bool visible;
} office_skeleton_t;

// The face's background (ctx: the office_skeleton_t): the clock's near-black, the soft shadows the wheels and the
// timeplate cast on the movement's back, and the frame.
void office_skeleton_background(void *ctx, gfx_color_t *dst, int x, int y, int w);

// Unpacks the artwork into mem (OFFICE_SKELETON_MEMORY bytes, kept for the face's lifetime), copies the frame's runs
// into frame_mem if given (office_skeleton_frame_memory() bytes; NULL: they are read where the firmware keeps its
// constant data), lays the face out centred in `screen` (designed for 1024 x 600; smaller screens are refused) and
// adds its sprites to the scene from z up, hidden. Returns false if the screen is too small, the memory too short or
// the scene full.
bool office_skeleton_init(office_skeleton_t *k, gfx_scene_t *scene, gfx_rect_t screen, uint8_t *mem,
                          size_t mem_size, uint8_t *frame_mem, size_t frame_size, office_skeleton_flush_fn flush,
                          int z);

// Bytes of the frame's runs: what frame_mem should hold (best in fast memory: they are read on every redraw).
size_t office_skeleton_frame_memory(void);

void office_skeleton_show(office_skeleton_t *k, bool visible);

// Shows tm (local time) at ms into its second, or the stopped "not set" face for NULL. Returns ms until it changes.
uint32_t office_skeleton_apply(office_skeleton_t *k, const struct tm *tm, uint32_t ms);

#ifdef __cplusplus
}
#endif
