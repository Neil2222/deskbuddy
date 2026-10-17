// Drawing the Tic-Tac-Toe screen's pieces (menu_ttt.h) into the menu's canvases. Private to the menu component:
// menu_view.c owns the sprites and decides when a piece is redrawn (when its key changes).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "menu_art.h"
#include "menu_ttt.h"

// Everything a piece shows, packed exactly (equal keys: the same pixels). piece 0..2: a row of the board, sunk while
// it pops in; 3: the strip. Never 0.
uint64_t menu_ttt_piece_key(const menu_game_t *g, int piece, bool sunk);
uint32_t menu_ttt_footer_key(const menu_game_t *g);

// A row of the board into row (MENU_TTT_ROW_W x MENU_TTT_CELL), using scratch (MENU_TTT_CELL x MENU_TTT_CELL) for
// each cell; the strip (MENU_TTT_ROW_W x MENU_TTT_STRIP_H); the footer (MENU_FOOTER_W x MENU_FOOTER_H).
void menu_ttt_draw_row(menu_canvas_t *row, menu_canvas_t *scratch, const menu_game_t *g, int r, bool sunk);
void menu_ttt_draw_strip(menu_canvas_t *c, const menu_game_t *g);
void menu_ttt_draw_footer(menu_canvas_t *c, const menu_game_t *g);
