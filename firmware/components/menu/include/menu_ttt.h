// The Tic-Tac-Toe screen in the control panel (the menu's MENU_TTT page, ttt.h plays the game): the header stays the
// menu's (Back, the title, the clock on Buddy's head), the rest belongs to the game.
//
//   board   three rows of three cells, each a button like the menu's tiles: your X in amber, Buddy's O a cyan ring
//           (his eyes' glow). A cell sinks under the finger and is played when the finger is lifted on it; sliding
//           off cancels. The mark just placed lights its cell up; the winning line blinks, then stays lit
//   strip   under the board: the score (YOU, BUDDY, DRAW); once Buddy asks for a rematch, a big REMATCH (AGAIN after
//           a draw) button instead
//   footer  whose turn it is, or how the round ended
//
// Drawn by menu_view in the sprites and memory of the menu's tiles, which this page does not use (no sprites or
// pixels of its own); each piece is redrawn only when what it shows changes. Geometry in menu pixels (menu.h).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx_types.h"
#include "menu.h"
#include "ttt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_TTT_CELL 53    // a cell, including its ledge
#define MENU_TTT_GAP 3      // between cells
#define MENU_TTT_TOP 42     // the board's top, under the header
#define MENU_TTT_ROW_W (3 * MENU_TTT_CELL + 2 * MENU_TTT_GAP)
#define MENU_TTT_STRIP_Y 216
#define MENU_TTT_STRIP_H 40
#define MENU_TTT_PIECES 4   // the board's three rows and the strip: the menu's first four slot sprites
// Pixels the pieces take in the menu's tile memory, plus a cell's worth to draw a cell in before it is copied into its
// row (no stack, no static buffer).
#define MENU_TTT_MEMORY                                                                                              \
    (3 * MENU_TTT_ROW_W * MENU_TTT_CELL + MENU_TTT_ROW_W * MENU_TTT_STRIP_H + MENU_TTT_CELL * MENU_TTT_CELL)

// Timing, in ms.
#define MENU_TTT_BLINK_MS 220 // the winning line blinks on and off this fast...
#define MENU_TTT_BLINKS 3     // ...this many times, then stays lit
#define MENU_TTT_DOTS_MS 400  // "BUDDY IS THINKING": one more dot every this many ms

enum {
    MENU_TTT_HIT_NONE = -1,
    // 0 .. TTT_CELLS - 1: the cells
    MENU_TTT_HIT_STRIP = TTT_CELLS, // the REMATCH / AGAIN button
};

typedef enum {
    MENU_TTT_STRIP_SCORE,
    MENU_TTT_STRIP_REMATCH,
    MENU_TTT_STRIP_AGAIN,
} menu_ttt_strip_t;

typedef enum {
    MENU_TTT_SAYS_YOUR_TURN,
    MENU_TTT_SAYS_THINKING,
    MENU_TTT_SAYS_BUDDY_WINS,
    MENU_TTT_SAYS_YOU_WIN,
    MENU_TTT_SAYS_DRAW,
} menu_ttt_says_t;

// What the screen shows (menu_ttt_look); menu_view draws it (menu_status_t.game).
typedef struct menu_game {
    uint8_t cell[TTT_CELLS]; // ttt_mark_t
    uint16_t lit;            // cells lit up now (the winning line): bit n for cell n
    uint8_t winner;          // TTT_X or TTT_O: whose line it is (its colour)
    int8_t fresh;            // the cell marked a moment ago (lights up); -1 none
    int8_t pressed;          // under the finger: a cell or MENU_TTT_HIT_STRIP; MENU_TTT_HIT_NONE
    uint8_t strip;           // menu_ttt_strip_t
    uint8_t says;            // menu_ttt_says_t: the footer
    uint8_t dots;            // ...with 0..3 dots while he thinks
    uint16_t you, buddy, draws;
} menu_game_t;

// A touch on the board or the strip in progress (screen pixels in, like menu_touch).
typedef struct {
    gfx_rect_t area; // the menu's screen rectangle (menu_t.area)
    bool down;
    int16_t down_x, down_y;
    int8_t target, pressed; // what the finger went down on and is still on; MENU_TTT_HIT_NONE
} menu_ttt_t;

gfx_rect_t menu_ttt_cell_rect(int cell);
gfx_rect_t menu_ttt_strip_rect(void);

void menu_ttt_init(menu_ttt_t *t, const menu_t *m);

// What can be tapped at screen point (x, y) now: an empty cell while a round is on, the strip while it is the
// REMATCH / AGAIN button; MENU_TTT_HIT_NONE otherwise (the header and anything else are the menu's).
int menu_ttt_hit(const menu_ttt_t *t, const ttt_t *g, int x, int y);

// Feeds a touch. Returns the cell or MENU_TTT_HIT_STRIP a completed tap was on; MENU_TTT_HIT_NONE otherwise.
int menu_ttt_touch(menu_ttt_t *t, const ttt_t *g, menu_touch_t type, int x, int y);

// A touch that went down on a cell or the button is in progress (it belongs to the game, not the menu).
bool menu_ttt_holding(const menu_ttt_t *t);

void menu_ttt_cancel(menu_ttt_t *t);

// The screen for game g at now. Returns ms until it changes on its own (the line blinking, the dots, the flash of a
// fresh mark); UINT32_MAX when nothing is scheduled.
uint32_t menu_ttt_look(menu_game_t *out, const ttt_t *g, const menu_ttt_t *t, uint32_t now);

#ifdef __cplusplus
}
#endif
