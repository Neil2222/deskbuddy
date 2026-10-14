// The menu's pixel art (private to the component): one palette that starts with Buddy's own colours, so his sprites
// copy straight in, plus the menu's; icons; and drawing into indexed canvases.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buddy_mood.h"
#include "gfx_image.h"
#include "menu_view.h"

// Palette indices. The first block mirrors Buddy's palette keys (buddy_art.c, ".KBbWSEGYRrkNnDgLlcPpOF").
enum {
    INK_CLEAR,
    INK_OUTLINE,     // K
    INK_BODY,        // B
    INK_BODY_SHADE,  // b
    INK_WHITE,       // W
    INK_SCREEN,      // S
    INK_CYAN,        // E: Buddy's eye glow, the menu's highlight
    INK_GREEN,       // G
    INK_YELLOW,      // Y
    INK_RED,         // R
    INK_RED_DARK,    // r
    INK_SHADOW,      // k
    INK_NEON,        // N
    INK_NEON_DARK,   // n
    INK_CAN,         // D
    INK_GLOSS,       // g
    INK_SILVER,      // L
    INK_GREY,        // l
    INK_CABLE,       // c
    INK_PLATE,       // P: a button's face
    INK_EDGE,        // p: its rim
    INK_FILTER,      // O
    INK_EMBER,       // F
    INK_BUDDY_COUNT, // the menu's own colours follow (keys in menu_art.c)
    INK_AMBER = INK_BUDDY_COUNT, // the LED sign's amber: titles, SOON tags
    INK_AMBER_HOT,
    INK_AMBER_DARK,
    INK_PLATE_DOWN,  // a pressed button's face
    INK_LEDGE,       // the dark side under a button
    INK_PLATE_LIT,   // the lit top edge; a tapped button's face
    INK_CYAN_DARK,   // glow around the active button
    INK_TAN,
    INK_BROWN,
    INK_COFFEE,
    INK_WATER,
    INK_WATER_LIGHT,
    INK_PURPLE,
    INK_PURPLE_DARK,
    INK_MOON,
    INK_MOON_SHADE,
    INK_COUNT,
};

typedef enum {
    MENU_PAL_NORMAL,
    MENU_PAL_DIM,     // planned and busy items: everything faded into the panel, except amber
    MENU_PAL_LED_ON,  // the active item's indicator, blinking
    MENU_PAL_LED_OFF,
    MENU_PAL_COUNT,
} menu_pal_t;

typedef struct {
    uint8_t *px;
    int w, h;
} menu_canvas_t;

// Decodes the icons and builds the palettes (once). False if Buddy's art or the menu's own is malformed.
bool menu_art_init(void);
const gfx_palette_t *menu_art_palette(menu_pal_t which);
const gfx_image_t *menu_art_led(void);

void menu_art_clear(menu_canvas_t *c);
void menu_art_fill(menu_canvas_t *c, int x, int y, int w, int h, uint8_t ink);

// A button body filling the canvas: rim, face, a lit top edge, and the ledge beneath it; sunk in, it sits
// MENU_LEDGE lower and hides the ledge. glow (0: none) draws a second rim inside the first.
void menu_art_button(menu_canvas_t *c, bool sunk, uint8_t face, uint8_t rim, uint8_t glow);

// Icon size, and drawing it centred on (cx, cy).
void menu_art_icon_size(menu_icon_t icon, uint8_t arg, int *w, int *h);
void menu_art_icon(menu_canvas_t *c, menu_icon_t icon, uint8_t arg, int cx, int cy);

// Buddy's head with a mood's face (eyes closed when blink), centred on (cx, cy): MENU_FACE_W x MENU_FACE_H pixels.
void menu_art_face(menu_canvas_t *c, buddy_face_t face, bool blink, int cx, int cy);

// Text in the LED sign's 5x7 font, top-left at (x, y). Returns the x after it.
int menu_art_text_width(const char *s);
int menu_art_text(menu_canvas_t *c, const char *s, int x, int y, uint8_t ink);

// Arrows: a solid triangle (back, pages) and a thin chevron (list rows), 5 x 9, pointing left or right.
#define MENU_ARROW_GLYPH_W 5
#define MENU_ARROW_GLYPH_H 9
void menu_art_arrow(menu_canvas_t *c, int x, int y, bool right, bool thin, uint8_t ink);

// The amber SOON tag, top-left at (x, y); hot: lit up (when tapped).
#define MENU_TAG_W 27
#define MENU_TAG_H 11
void menu_art_tag(menu_canvas_t *c, int x, int y, bool hot);
