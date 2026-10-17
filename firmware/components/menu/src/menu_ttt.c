#include "menu_ttt.h"

#include <stdlib.h>
#include <string.h>

#include "menu_ttt_art.h"

#define MARGIN 5    // as the menu's (menu.c)
#define CELL_PAD 1  // cells take touches this far beyond their edges (the gaps are MENU_TTT_GAP)
#define STRIP_PAD 2
#define X_ARM 12    // the X: half its diagonal, in pixels from the centre...
#define X_PEN 3     // ...drawn with a square pen this wide
#define O_OUT 14    // the O: a ring between these radii
#define O_IN 11

// --- Geometry and touch -------------------------------------------------------------------------------------------

gfx_rect_t menu_ttt_cell_rect(int cell)
{
    return (gfx_rect_t){MARGIN + (cell % 3) * (MENU_TTT_CELL + MENU_TTT_GAP),
                        MENU_TTT_TOP + (cell / 3) * (MENU_TTT_CELL + MENU_TTT_GAP), MENU_TTT_CELL, MENU_TTT_CELL};
}

gfx_rect_t menu_ttt_strip_rect(void)
{
    return (gfx_rect_t){MARGIN, MENU_TTT_STRIP_Y, MENU_TTT_ROW_W, MENU_TTT_STRIP_H};
}

void menu_ttt_init(menu_ttt_t *t, const menu_t *m)
{
    memset(t, 0, sizeof(*t));
    t->area = m->area;
    t->target = t->pressed = MENU_TTT_HIT_NONE;
}

static bool inside(gfx_rect_t r, int pad, int x, int y)
{
    return x >= r.x - pad && y >= r.y - pad && x < r.x + r.w + pad && y < r.y + r.h + pad;
}

int menu_ttt_hit(const menu_ttt_t *t, const ttt_t *g, int x, int y)
{
    if (!gfx_rect_contains_point(t->area, x, y)) {
        return MENU_TTT_HIT_NONE;
    }
    const int mx = (x - t->area.x) / MENU_SCALE, my = (y - t->area.y) / MENU_SCALE;
    if (g->phase == TTT_YOUR_TURN || g->phase == TTT_THINKING) { // during his turn too: he protests (ttt_play)
        for (int i = 0; i < TTT_CELLS; i++) {
            if (g->board[i] == TTT_EMPTY && inside(menu_ttt_cell_rect(i), CELL_PAD, mx, my)) {
                return i;
            }
        }
    }
    if (g->phase == TTT_REMATCH && inside(menu_ttt_strip_rect(), STRIP_PAD, mx, my)) {
        return MENU_TTT_HIT_STRIP;
    }
    return MENU_TTT_HIT_NONE;
}

void menu_ttt_cancel(menu_ttt_t *t)
{
    t->down = false;
    t->target = t->pressed = MENU_TTT_HIT_NONE;
}

bool menu_ttt_holding(const menu_ttt_t *t)
{
    return t->down;
}

int menu_ttt_touch(menu_ttt_t *t, const ttt_t *g, menu_touch_t type, int x, int y)
{
    if (type == MENU_TOUCH_DOWN) {
        menu_ttt_cancel(t); // a down without the previous up: start over
        const int hit = menu_ttt_hit(t, g, x, y);
        if (hit != MENU_TTT_HIT_NONE) {
            t->down = true;
            t->down_x = (int16_t)x;
            t->down_y = (int16_t)y;
            t->target = t->pressed = (int8_t)hit;
        }
        return MENU_TTT_HIT_NONE;
    }
    if (!t->down) {
        return MENU_TTT_HIT_NONE;
    }
    if (abs(x - t->down_x) > MENU_SLOP_PX || abs(y - t->down_y) > MENU_SLOP_PX) {
        t->target = t->pressed = MENU_TTT_HIT_NONE; // slid away: no tap
    }
    if (type == MENU_TOUCH_MOVE) {
        return MENU_TTT_HIT_NONE;
    }
    const int target = t->target;
    menu_ttt_cancel(t);
    return target != MENU_TTT_HIT_NONE && menu_ttt_hit(t, g, x, y) == target ? target : MENU_TTT_HIT_NONE;
}

// --- What it shows ------------------------------------------------------------------------------------------------

static void sooner(uint32_t *next, uint32_t ms)
{
    if (ms < *next) {
        *next = ms ? ms : 1;
    }
}

uint32_t menu_ttt_look(menu_game_t *out, const ttt_t *g, const menu_ttt_t *t, uint32_t now)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->cell, g->board, sizeof(out->cell));
    out->you = g->you;
    out->buddy = g->buddy;
    out->draws = g->draws;
    out->pressed = t ? t->pressed : MENU_TTT_HIT_NONE;
    out->fresh = -1;
    uint32_t next = UINT32_MAX;
    if (g->phase != TTT_OFF && g->last >= 0 && now - g->last_at < MENU_FLASH_MS) {
        out->fresh = g->last;
        sooner(&next, MENU_FLASH_MS - (now - g->last_at));
    }
    switch (g->phase) {
    case TTT_THINKING: {
        const uint32_t t_ms = now - g->phase_at;
        out->says = MENU_TTT_SAYS_THINKING;
        out->dots = (uint8_t)(t_ms / MENU_TTT_DOTS_MS % 4);
        sooner(&next, MENU_TTT_DOTS_MS - t_ms % MENU_TTT_DOTS_MS);
        break;
    }
    case TTT_REACTION:
    case TTT_REMATCH: {
        out->says = g->result == TTT_X_WINS   ? MENU_TTT_SAYS_YOU_WIN
                    : g->result == TTT_O_WINS ? MENU_TTT_SAYS_BUDDY_WINS
                                              : MENU_TTT_SAYS_DRAW;
        out->winner = g->result == TTT_X_WINS ? TTT_X : TTT_O;
        if (g->phase == TTT_REMATCH) {
            out->strip = g->result == TTT_DRAW ? MENU_TTT_STRIP_AGAIN : MENU_TTT_STRIP_REMATCH;
        }
        // The line blinks a few times from the end of the round, then stays lit.
        const uint32_t over_at = g->phase == TTT_REACTION ? g->phase_at : g->phase_at - TTT_REACTION_MS;
        const uint32_t s = now - over_at, blinking = 2u * MENU_TTT_BLINKS * MENU_TTT_BLINK_MS;
        out->lit = g->line;
        if (s < blinking) {
            out->lit = (s / MENU_TTT_BLINK_MS) % 2 == 0 ? g->line : 0;
            sooner(&next, MENU_TTT_BLINK_MS - s % MENU_TTT_BLINK_MS);
        }
        break;
    }
    default:
        out->says = MENU_TTT_SAYS_YOUR_TURN;
        break;
    }
    return next;
}

// --- Keys -------------------------------------------------------------------------------------------------------

enum {
    CELL_SUNK = 1,
    CELL_FLASH = 2,
    CELL_LIT = 4,
    CELL_BUDDYS = 8, // lit in Buddy's colour (his line), not yours
};

static unsigned cell_look(const menu_game_t *g, int i, bool sunk)
{
    unsigned look = 0;
    if (sunk || g->pressed == i) {
        look |= CELL_SUNK;
    }
    if (g->fresh == i) {
        look |= CELL_FLASH;
    }
    if (g->lit & (1u << i)) {
        look |= CELL_LIT | (g->winner == TTT_O ? CELL_BUDDYS : 0);
    }
    return look;
}

uint64_t menu_ttt_piece_key(const menu_game_t *g, int piece, bool sunk)
{
    uint64_t key = 1ull << 63 | (uint64_t)piece << 56;
    if (piece < 3) {
        for (int c = 0; c < 3; c++) {
            const int i = piece * 3 + c;
            key |= (uint64_t)((g->cell[i] & 3u) | (cell_look(g, i, sunk) << 2)) << (c * 8);
        }
        return key;
    }
    return key | (uint64_t)g->strip << 52 | (uint64_t)(g->pressed == MENU_TTT_HIT_STRIP) << 50 |
           (uint64_t)g->you << 32 | (uint64_t)g->buddy << 16 | g->draws;
}

uint32_t menu_ttt_footer_key(const menu_game_t *g)
{
    return 1u << 31 | (uint32_t)g->says << 4 | g->dots;
}

// --- Drawing ----------------------------------------------------------------------------------------------------

// Copies the opaque pixels of src into dst at (x, y).
static void stamp(menu_canvas_t *dst, const menu_canvas_t *src, int x, int y)
{
    for (int sy = 0; sy < src->h; sy++) {
        for (int sx = 0; sx < src->w; sx++) {
            const uint8_t p = src->px[sy * src->w + sx];
            if (p && x + sx >= 0 && x + sx < dst->w && y + sy >= 0 && y + sy < dst->h) {
                dst->px[(y + sy) * dst->w + x + sx] = p;
            }
        }
    }
}

static void draw_x(menu_canvas_t *c, int cx, int cy, uint8_t ink)
{
    for (int k = -X_ARM; k <= X_ARM; k++) {
        menu_art_fill(c, cx + k - X_PEN / 2, cy + k - X_PEN / 2, X_PEN, X_PEN, ink);
        menu_art_fill(c, cx + k - X_PEN / 2, cy - k - X_PEN / 2, X_PEN, X_PEN, ink);
    }
}

// A ring in the colour of his eyes' glow (circle edges rounded the way the menu's discs are: r * r + r).
static void draw_o(menu_canvas_t *c, int cx, int cy, uint8_t ink)
{
    for (int y = -O_OUT; y <= O_OUT; y++) {
        for (int x = -O_OUT; x <= O_OUT; x++) {
            const int d = x * x + y * y;
            if (d > O_IN * O_IN + O_IN && d <= O_OUT * O_OUT + O_OUT) {
                menu_art_fill(c, cx + x, cy + y, 1, 1, ink);
            }
        }
    }
}

static void draw_cell(menu_canvas_t *c, uint8_t mark, unsigned look)
{
    const bool sunk = look & CELL_SUNK, flash = look & CELL_FLASH, lit = look & CELL_LIT;
    const bool buddys = look & CELL_BUDDYS;
    const uint8_t rim = flash ? INK_WHITE : lit ? (buddys ? INK_CYAN : INK_AMBER) : INK_EDGE;
    const uint8_t glow = lit ? (buddys ? INK_CYAN_DARK : INK_AMBER_DARK) : 0;
    menu_art_button(c, sunk, flash ? INK_PLATE_LIT : sunk ? INK_PLATE_DOWN : INK_PLATE, rim, glow);
    const int cx = c->w / 2, cy = (c->h - MENU_LEDGE) / 2 + (sunk ? MENU_LEDGE : 0);
    if (mark == TTT_X) {
        draw_x(c, cx, cy, lit ? INK_AMBER_HOT : INK_AMBER);
    } else if (mark == TTT_O) {
        draw_o(c, cx, cy, lit ? INK_WHITE : INK_CYAN);
    }
}

void menu_ttt_draw_row(menu_canvas_t *row, menu_canvas_t *scratch, const menu_game_t *g, int r, bool sunk)
{
    menu_art_clear(row);
    for (int col = 0; col < 3; col++) {
        const int i = r * 3 + col;
        menu_art_clear(scratch);
        draw_cell(scratch, g->cell[i], cell_look(g, i, sunk));
        stamp(row, scratch, col * (MENU_TTT_CELL + MENU_TTT_GAP), 0);
    }
}

static void score(menu_canvas_t *c, int cx, const char *label, unsigned n, uint8_t ink)
{
    char num[8];
    num[sizeof(num) - 1] = 0;
    int k = (int)sizeof(num) - 1;
    do {
        num[--k] = (char)('0' + n % 10);
        n /= 10;
    } while (n && k > 0);
    menu_art_text(c, label, cx - menu_art_text_width(label) / 2, 9, INK_GREY);
    menu_art_text(c, num + k, cx - menu_art_text_width(num + k) / 2, 22, ink);
}

void menu_ttt_draw_strip(menu_canvas_t *c, const menu_game_t *g)
{
    menu_art_clear(c);
    if (g->strip == MENU_TTT_STRIP_SCORE) {
        const int third = c->w / 3;
        score(c, third / 2, "YOU", g->you, INK_AMBER);
        score(c, third + third / 2, "BUDDY", g->buddy, INK_CYAN);
        score(c, 2 * third + third / 2, "DRAW", g->draws, INK_SILVER);
        return;
    }
    const bool sunk = g->pressed == MENU_TTT_HIT_STRIP;
    menu_art_button(c, sunk, sunk ? INK_PLATE_DOWN : INK_PLATE, INK_CYAN, INK_CYAN_DARK);
    const char *label = g->strip == MENU_TTT_STRIP_AGAIN ? "AGAIN" : "REMATCH";
    const int dy = sunk ? MENU_LEDGE : 0;
    menu_art_text(c, label, (c->w - menu_art_text_width(label)) / 2, (c->h - MENU_LEDGE - 7) / 2 + dy, INK_WHITE);
}

void menu_ttt_draw_footer(menu_canvas_t *c, const menu_game_t *g)
{
    menu_art_clear(c);
    const int y = (c->h - MENU_LEDGE - 7) / 2;
    if (g->says == MENU_TTT_SAYS_THINKING) { // the dots fill in after the words, which stay put
        const int w = menu_art_text_width("BUDDY IS THINKING...");
        const int x = menu_art_text(c, "BUDDY IS THINKING", (c->w - w) / 2, y, INK_GREY);
        static const char *const k_dots[4] = {"", ".", "..", "..."};
        menu_art_text(c, k_dots[g->dots & 3u], x, y, INK_CYAN);
        return;
    }
    static const struct {
        const char *text;
        uint8_t ink;
    } k_says[] = {
        [MENU_TTT_SAYS_YOUR_TURN] = {"YOUR TURN", INK_AMBER},
        [MENU_TTT_SAYS_BUDDY_WINS] = {"BUDDY WINS!", INK_CYAN},
        [MENU_TTT_SAYS_YOU_WIN] = {"YOU WIN!", INK_AMBER},
        [MENU_TTT_SAYS_DRAW] = {"IT'S A DRAW", INK_SILVER},
    };
    const unsigned s = g->says < sizeof(k_says) / sizeof(k_says[0]) ? g->says : MENU_TTT_SAYS_YOUR_TURN;
    menu_art_text(c, k_says[s].text, (c->w - menu_art_text_width(k_says[s].text)) / 2, y, k_says[s].ink);
}
