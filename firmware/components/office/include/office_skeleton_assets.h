// The Mechanical Skeleton's static artwork as firmware data: made on the development PC by the same C drawing code
// (office_skeleton_art.c, host only) and committed as office_skeleton_assets.c, so the board never draws it.
//
// Regenerate after changing the drawing code: tools/host-test.ps1 -Assets. The host test
// office_skeleton_assets_match_the_drawing fails while the committed file differs from what the code draws.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OFFICE_SKELETON_DESIGN_W 1024 // the face is laid out for this screen (centred in larger ones)
#define OFFICE_SKELETON_DESIGN_H 600
#define OFFICE_SKELETON_SCALE 2       // art pixels are this many screen pixels square
#define OFFICE_SKELETON_WHEELS 7      // six train wheels and the escape wheel
#define OFFICE_SKELETON_STEPS 4       // quarter-tooth positions per wheel

// The images, in this order.
enum {
    OFFICE_SKELETON_IMG_WHEEL = 0, // + wheel * STEPS + step
    OFFICE_SKELETON_IMG_ANCHOR = OFFICE_SKELETON_WHEELS * OFFICE_SKELETON_STEPS,
    OFFICE_SKELETON_IMG_COCK,
    OFFICE_SKELETON_IMG_PLATE,
    OFFICE_SKELETON_IMG_GLYPH, // + '0'..'9', then '-'
    OFFICE_SKELETON_IMAGES = OFFICE_SKELETON_IMG_GLYPH + 11,
};

// Palette indices of the movement, the frame and the timeplate (colours: office_skeleton.c).
enum {
    OFFICE_SKELETON_P_BRASS = 1,   // 7 levels
    OFFICE_SKELETON_P_BRONZE = 8,  // 7 levels
    OFFICE_SKELETON_P_STEEL = 15,  // 5 levels
    OFFICE_SKELETON_P_RUBY = 20,   // 3 levels
    OFFICE_SKELETON_P_BLUED = 23,  // 3 levels
    OFFICE_SKELETON_P_SHADOW = 26, // cast on what lies below
    OFFICE_SKELETON_P_ENAMEL = 27, // 3 levels: the timeplate
    OFFICE_SKELETON_P_WINDOW = 30, // 3 levels: the digit windows (the numerals' ink fades into the middle one)
    OFFICE_SKELETON_P_COUNT = 33,
};

// Where everything goes, relative to the design screen's top left (screen pixels).
typedef struct {
    uint16_t w[OFFICE_SKELETON_IMAGES], h[OFFICE_SKELETON_IMAGES]; // art pixels
    int16_t x[OFFICE_SKELETON_IMAGES], y[OFFICE_SKELETON_IMAGES];  // sprite positions (glyphs: unused)
    int8_t dir[OFFICE_SKELETON_WHEELS];                            // turning direction: +1 / -1
    float shadow[OFFICE_SKELETON_WHEELS][3];                       // x, y, radius of each wheel's shadow
    int16_t plate_shadow[4];                                       // x, y, w, h
    int16_t digit_x[4], digit_y;
    int16_t date_x, date_y, date_w, date_h;
} office_skeleton_layout_t;

// The frame behind the wheels (full screen, mostly empty) as runs of visible art pixels, row by row: for art row r,
// row[r] .. row[r + 1] in `runs` holds, per run, its first column (2 bytes, little-endian), its length n (2 bytes)
// and n palette indices. The face's background draws it, so only rows being redrawn are read, and only their runs.
typedef struct {
    uint16_t w, h; // art pixels
    const uint32_t *row; // h + 1 offsets into runs
    const uint8_t *runs;
    uint32_t runs_size;
} office_skeleton_frame_t;

typedef struct {
    office_skeleton_layout_t layout;
    uint32_t offset[OFFICE_SKELETON_IMAGES + 1]; // each image's packed bytes in `packed`
    const uint8_t *packed;                       // PackBits (see office_skeleton_unpack)
    office_skeleton_frame_t frame;
} office_skeleton_assets_t;

extern const office_skeleton_assets_t office_skeleton_assets;

// PackBits: a header byte n < 128 is followed by n + 1 literal bytes; n >= 128 by one byte repeated n - 126 times.
// Unpacks exactly `size` bytes from src into dst; false if src does not hold that (a corrupt asset).
bool office_skeleton_unpack(const uint8_t *src, size_t src_size, uint8_t *dst, size_t size);

#ifdef __cplusplus
}
#endif
