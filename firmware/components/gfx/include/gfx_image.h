// Indexed pixel-art images and palettes.
#pragma once

#include <stddef.h>

#include "gfx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Row-major palette indices, one byte per pixel. Index 0 is transparent.
typedef struct {
    uint16_t w, h;
    const uint8_t *px;
} gfx_image_t;

// colors[i] is the display color of index i; colors[0] is never drawn. Every index used by an image drawn with
// this palette must be < count.
typedef struct {
    const gfx_color_t *colors;
    uint16_t count;
} gfx_palette_t;

// Decodes ASCII art into an indexed image. Each character's position in `keys` is its palette index, so keys[0]
// is the transparent character (e.g. keys = ".KW" maps '.'->0, 'K'->1, 'W'->2). All rows must have the same
// length. Pixels are written to storage (capacity bytes). Returns false on a ragged row, an unknown character,
// or insufficient storage; img is untouched in that case.
bool gfx_image_from_ascii(gfx_image_t *img, uint8_t *storage, size_t capacity, const char *const *rows, int n_rows,
                          const char *keys);

#ifdef __cplusplus
}
#endif
