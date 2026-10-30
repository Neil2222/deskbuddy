// What the strips at the bottom of the stage (usage_view.h, sysinfo_view.h) draw with: one palette, the sign's 5x7
// font (marquee.h) and a small bar, on a dark plate with cut corners. Image pixels of a strip image img_w wide and
// USAGE_VIEW_IMG_H high; every function draws only when px is not NULL and returns where it ends, so a strip measures
// itself with the same code that draws it.
#pragma once

#include <stdint.h>

#include "gfx_image.h"

enum { STRIP_PLATE = 1, STRIP_EDGE, STRIP_BAR_BG, STRIP_GREEN, STRIP_YELLOW, STRIP_ORANGE, STRIP_RED, STRIP_TEXT,
       STRIP_DIM, STRIP_INK_COUNT };

#define STRIP_PAD_X 5       // plate edge to the first and last text
#define STRIP_GAP 4         // between a label, its bar, its pct and what follows
#define STRIP_GAP_SEGMENT 6 // either side of the divider between two segments

extern gfx_palette_t strip_palette;

// The ink of a pct: green below 50, yellow below 75, orange below 90, red from 90 (usage_level).
uint8_t strip_level_ink(int pct);

void strip_fill(uint8_t *px, int img_w, int x, int y, int w, int h, uint8_t ink);

// Text from x.
int strip_text(uint8_t *px, int img_w, int x, const char *s, uint8_t ink);

// The bar from x: filled to min(pct, 100) % in pct's ink.
int strip_bar(uint8_t *px, int img_w, int x, int pct);

// The divider before the next segment, from x.
int strip_divider(uint8_t *px, int img_w, int x);

// Clears px and draws a plate content_w wide (padding added) centred in it: returns where the content starts. The
// plate's left edge and width (at most img_w) go to plate_x and plate_w.
int strip_plate(uint8_t *px, int img_w, int content_w, int *plate_x, int *plate_w);
