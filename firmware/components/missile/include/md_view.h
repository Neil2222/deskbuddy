// Buddy Missile Defense on screen: the whole 1024 x 600 display, drawn by the game scene's background and overlay
// functions (gfx_scene.h), so the game adds no sprites to the scene; Buddy's own sprites stand between the two.
//
//   background  the night sky (bands and stars), the desk and the plate he stands on: never changes during a game
//   overlay     everything that does, over Buddy: the desk items (standing, flashing, burning, rubble), the missiles'
//               trails and heads, the blasts, his two eye beams, the points scored, his speech bubble, the banners
//               ("WAVE 3", "GAME OVER"), the RETRY / QUIT buttons, the score bar and the heat of his eyes
//
// Change tracking: md_view_apply() works out what the overlay draws now, compares it with what it drew last time, and
// invalidates only what changed (gfx_scene_invalidate): a moving missile its few new rows, a blast its square, a beam
// its length in short pieces (MD_VIEW_PIECE_ROWS), so long diagonal lines never become one large rectangle.
// Lines and blasts are worked out row by row (integer arithmetic); text uses the LED sign's 5x7 font; the items are
// small indexed images. No allocation: everything lives in md_view_t and a static art pool.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx_scene.h"
#include "md.h"

#ifdef __cplusplus
extern "C" {
#endif

// Buddy in the game: this scale, centred on MD_BUDDY_X, his feet on the desk.
#define MD_BUDDY_SCALE 5
#define MD_BUDDY_X0 (MD_BUDDY_X - 28 * MD_BUDDY_SCALE / 2) // art origin (his art is 28 art pixels wide)
#define MD_BUDDY_Y0 (MD_GROUND_Y + 12 - 35 * MD_BUDDY_SCALE) // the soles (art row 34) just below the desk's edge

#define MD_VIEW_PIECE_ROWS 32 // a line is invalidated in pieces this many rows tall
#define MD_VIEW_TEXT_COLS 112 // widest text, in font columns (about 18 characters)
#define MD_VIEW_BUBBLE_COLS 104 // widest line in his bubble, in font columns (host-tested against md_lines)
#define MD_VIEW_STROKES (MD_MAX_MISSILES + 2) // the trails and his two beams

enum { MD_VIEW_TEXT_SCORE_LABEL, MD_VIEW_TEXT_SCORE, MD_VIEW_TEXT_WAVE, MD_VIEW_TEXT_BEST, MD_VIEW_TEXT_EXIT,
       MD_VIEW_TEXT_BANNER, MD_VIEW_TEXT_BANNER2, MD_VIEW_TEXT_SAY, MD_VIEW_TEXT_SAY2, MD_VIEW_TEXT_RETRY,
       MD_VIEW_TEXT_QUIT, MD_VIEW_TEXT_POP, MD_VIEW_TEXTS };

enum { MD_VIEW_BOX_EXIT, MD_VIEW_BOX_RETRY, MD_VIEW_BOX_QUIT, MD_VIEW_BOX_BUBBLE, MD_VIEW_BOX_HEAT, MD_VIEW_BOXES };

// What the overlay draws: one state per thing. Compared field by field with what was drawn last.
typedef struct {
    bool on;
    uint8_t style;          // trail or beam
    int16_t ax, ay, bx, by; // the whole line, a above b (ay <= by)
    int16_t from_y, to_y;   // the part drawn: these rows of it
} md_stroke_t;

typedef struct {
    bool on;
    uint8_t look; // colours: growing, dying down
    int16_t x, y;
    uint8_t r;    // radius in blocks of 4 pixels
} md_ring_t;

typedef struct {
    bool on;
    int16_t x, y; // top-left
    uint8_t scale, ink, n;
    uint8_t cols[MD_VIEW_TEXT_COLS];
} md_text_t;

typedef struct {
    bool on;
    uint8_t look; // plate colours (and sunk in while pressed); the heat bar: segments lit
    int16_t x, y, w, h;
} md_box_t;

typedef struct {
    uint8_t look; // standing, flashing, burning (two flame frames), rubble
} md_item_look_t;

typedef struct {
    md_item_look_t item[MD_ITEMS];
    md_stroke_t stroke[MD_VIEW_STROKES];
    uint8_t head[MD_MAX_MISSILES]; // a missile's head shows (at its stroke's lower end)
    md_ring_t ring[MD_MAX_BLASTS];
    md_text_t text[MD_VIEW_TEXTS];
    md_box_t box[MD_VIEW_BOXES];
} md_layer_t;

typedef enum {
    MD_VIEW_HIT_NONE,
    MD_VIEW_HIT_SKY,  // anywhere else on the field: a shot
    MD_VIEW_HIT_EXIT, // the X in the score bar
    MD_VIEW_HIT_RETRY,
    MD_VIEW_HIT_QUIT,
} md_view_hit_t;

typedef struct {
    gfx_scene_t *scene;
    bool shown;          // the layer is on screen (md_view_show)
    md_layer_t now, drawn;
    // A touch in progress on a button (they act on release, like the menu's): what it went down on, and where.
    uint8_t pressed;     // md_view_hit_t of a button, or NONE
    int16_t down_x, down_y;
    // Buddy's eyes, where the beams start (screen pixels), as of the last md_view_apply.
    int16_t eye_x[2], eye_y[2];
    uint32_t pieces;     // statistics: rectangles invalidated by the last md_view_apply
} md_view_t;

// Decodes the art (once) and readies the view for scene. False if the art is malformed (a programming error, caught
// by the host tests). Draws nothing until md_view_show().
bool md_view_init(md_view_t *v, gfx_scene_t *scene);

// The game scene's background and overlay (ctx: the view). The caller puts them on the scene while the game shows.
void md_view_background(void *ctx, gfx_color_t *dst, int x, int y, int w);
void md_view_overlay(void *ctx, gfx_color_t *dst, int x, int y, int w);

// Starts (true) or stops (false) drawing the layer. The caller redraws the whole screen around this (it switches
// the scene's background and overlay), so nothing is invalidated here; stopping forgets everything drawn.
void md_view_show(md_view_t *v, bool shown);

// Brings the layer up to date with the game at now; eye_x/eye_y: Buddy's eye centres (screen pixels), where the beams
// start. Returns ms until the layer changes on its own (UINT32_MAX: not before the game does).
uint32_t md_view_apply(md_view_t *v, const md_t *g, const int eye_x[2], const int eye_y[2], uint32_t now);

// What is under screen point (x, y) now: a button that shows, else the field (MD_VIEW_HIT_SKY).
md_view_hit_t md_view_hit(const md_view_t *v, const md_t *g, int x, int y, uint32_t now);

typedef enum {
    MD_VIEW_TOUCH_DOWN,
    MD_VIEW_TOUCH_MOVE,
    MD_VIEW_TOUCH_UP,
} md_view_touch_t;

// A touch on the game. Shots go off as the finger comes down (MD_VIEW_HIT_SKY, at once: an arcade); buttons act when
// it is lifted on the button it went down on (sliding away cancels). Returns what to do now: NONE, SKY (a shot at
// x, y), EXIT, RETRY or QUIT.
md_view_hit_t md_view_touch(md_view_t *v, const md_t *g, md_view_touch_t type, int x, int y, uint32_t now);

// Forgets a touch in progress.
void md_view_cancel(md_view_t *v);

#ifdef __cplusplus
}
#endif
