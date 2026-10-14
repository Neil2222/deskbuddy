// A small geometric stroke font with antialiasing, for the Office Mode clock.
//
// Glyphs are lines and arcs on a grid 10 units tall (capitals and digits). They are rasterized at any size into
// indexed images whose indices 1..OFFICE_INK_LEVELS are coverage levels; index 0 is transparent. Drawn with a
// palette that blends from the (flat) background colour to the ink colour, the edges come out smooth without any
// blending support in the compositor.
//
// Character set: 0-9 : - space, and the capitals used for English weekday and month abbreviations plus
// "CLOCK NOT SET".
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OFFICE_INK_LEVELS 7

typedef struct {
    float unit;     // pixels per grid unit (cap height = 10 units)
    float stroke;   // stroke width in pixels
    float tracking; // extra pixels between characters
} office_font_style_t;

// True if the font has a glyph for c.
bool office_font_has(char c);

// Horizontal advance of c in pixels, including tracking. 0 for unknown characters.
float office_font_advance(char c, const office_font_style_t *st);

// Width of a string in pixels (the last character's tracking excluded).
float office_font_text_width(const char *s, const office_font_style_t *st);

// Draws c with its left edge at x and its top (cap line) at y (pixels, fractional allowed) into an indexed buffer
// of w x h pixels. Pixels only ever get darker ink (max of old and new coverage), so overlapping strokes and
// neighbouring characters combine cleanly. Unknown characters draw nothing.
void office_font_draw(char c, const office_font_style_t *st, uint8_t *buf, int w, int h, float x, float y);

// Draws a string; returns the x where the next character would go.
float office_font_draw_text(const char *s, const office_font_style_t *st, uint8_t *buf, int w, int h, float x,
                            float y);

#ifdef __cplusplus
}
#endif
