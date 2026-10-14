// A retro LED sign (lichtkrant) that scrolls a message across the screen in LED-column steps.
//
// The sign is a single sprite: an amber dot-matrix panel in a metal bezel, MARQUEE_COLS x MARQUEE_ROWS LEDs of
// 8x8 screen pixels. Text uses a 5x7 pixel font with proportional spacing. It powers up with a scanner sweep, then
// the text scrolls in from the right and out to the left; short texts run again so every message stays up for at
// least MARQUEE_MIN_SHOW_MS. The scroll position follows the elapsed time; the sign is redrawn only every
// MARQUEE_FRAME_MS, and each redraw covers the panel only (the sprite alternates between two pixel buffers, so the
// scene sees the change). A lower redraw rate means less frame-buffer traffic: at 25 redraws a second the scanout
// showed horizontal artifacts (docs/rendering-results.md).
//
// Text policy (UTF-8 in): printable ASCII as is; accented Latin letters lose their accents (e -> e); typographic
// quotes, dashes and the ellipsis become their ASCII cousins; the euro sign, degree sign and hearts have their own
// glyphs; control characters become spaces; variation selectors and zero-width characters are dropped; anything
// else (emoji, other scripts) shows as a hollow box. Invalid bytes also show as the box.
//
// Hardware independent: no clock of its own, no I/O. Host-tested.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MARQUEE_COLS 78
#define MARQUEE_ROWS 9            // a blank row above and below the 7-row font
#define MARQUEE_TEXT_MAX 100      // glyphs; longer text is cut
#define MARQUEE_COL_MS 60         // scroll speed: one LED column per 60 ms, about 17 columns (2.7 characters) a second
#define MARQUEE_FRAME_MS 60       // the sign is redrawn this often (16.7 fps): one column per redraw
#define MARQUEE_INTRO_MS (10 * MARQUEE_COL_MS) // scanner sweep before the text (whole columns' time)
#define MARQUEE_MIN_SHOW_MS 8000  // short texts repeat until shown at least this long...
#define MARQUEE_MAX_PASSES 3      // ...but at most this many times

#define MARQUEE_LED 4    // image pixels per LED (3x3 dot plus a 1-pixel gap)
#define MARQUEE_BEZEL 3  // image pixels of frame around the LEDs
#define MARQUEE_SCALE 2
#define MARQUEE_IMG_W (MARQUEE_COLS * MARQUEE_LED + 2 * MARQUEE_BEZEL)
#define MARQUEE_IMG_H (MARQUEE_ROWS * MARQUEE_LED + 2 * MARQUEE_BEZEL)
#define MARQUEE_W (MARQUEE_IMG_W * MARQUEE_SCALE) // screen size of the sign
#define MARQUEE_H (MARQUEE_IMG_H * MARQUEE_SCALE)

#define MARQUEE_GLYPH_UNKNOWN 0x7F // hollow box
#define MARQUEE_GLYPH_EURO 0x80
#define MARQUEE_GLYPH_DEGREE 0x81
#define MARQUEE_GLYPH_HEART 0x82

typedef struct {
    gfx_sprite_t sprite;
    gfx_image_t image[2];
    uint8_t px[2][MARQUEE_IMG_W * MARQUEE_IMG_H];
    int cur;  // image in use
    int shown_frame; // frame drawn into image[cur]; -1: none
    uint8_t col[MARQUEE_TEXT_MAX * 6]; // the text as LED columns, bit r = font row r
    int ncols;
    int passes;
    uint32_t start;
    bool active;
} marquee_t;

// Adds the (hidden) sign to the scene at depth z. Returns false when the scene is full.
bool marquee_init(marquee_t *m, gfx_scene_t *scene, int z);

// Starts showing text (UTF-8) at now. Replaces anything showing.
void marquee_start(marquee_t *m, const char *utf8, uint32_t now);

// Puts the sign's top-left corner at (x, y) and advances it to now. Returns ms until it changes next; UINT32_MAX
// when nothing is showing. Once the message has been shown, the sign hides and marquee_active() turns false.
uint32_t marquee_apply(marquee_t *m, int x, int y, uint32_t now);

bool marquee_active(const marquee_t *m);

// How long the current message shows, from marquee_start() until the sign hides.
uint32_t marquee_duration_ms(const marquee_t *m);

// Text policy: converts UTF-8 into glyph codes (see above), at most max of them. Returns the count.
int marquee_glyphs(const char *utf8, uint8_t *out, int max);

#ifdef __cplusplus
}
#endif
