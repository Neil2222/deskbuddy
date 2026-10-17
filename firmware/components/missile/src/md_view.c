#include "md_view.h"

#include <stdio.h>
#include <string.h>

#include "marquee.h"

// --- Colours --------------------------------------------------------------------------------------------------------

#define C_OUTLINE GFX_RGB(22, 26, 44)
#define C_WHITE GFX_RGB(236, 242, 255)
#define C_CYAN GFX_RGB(96, 232, 255)
#define C_AMBER GFX_RGB(252, 212, 84)
#define C_DIM GFX_RGB(128, 134, 148)
#define C_RED GFX_RGB(255, 84, 84)
#define C_PLATE GFX_RGB(32, 37, 58)
#define C_PLATE_HOT GFX_RGB(52, 62, 98)
#define C_EDGE GFX_RGB(64, 74, 108)
#define C_BAR GFX_RGB(14, 16, 28)
#define C_DESK GFX_RGB(32, 37, 58)
#define C_GRAIN GFX_RGB(40, 46, 70)
#define C_HEAT_OFF GFX_RGB(40, 46, 70)

enum { INK_WHITE, INK_DIM, INK_AMBER, INK_CYAN, INK_RED, INK_DARK };
static const gfx_color_t k_ink[] = {C_WHITE, C_DIM, C_AMBER, C_CYAN, C_RED, C_OUTLINE};

// The night sky: bands from almost black at the top to the stage's blue at the horizon.
#define SKY_BANDS 10
static const gfx_color_t k_sky[SKY_BANDS] = {
    GFX_RGB(6, 8, 20),    GFX_RGB(8, 11, 24),  GFX_RGB(11, 13, 29),  GFX_RGB(14, 16, 33), GFX_RGB(16, 19, 38),
    GFX_RGB(19, 22, 42),  GFX_RGB(22, 26, 47), GFX_RGB(25, 29, 51),  GFX_RGB(27, 32, 56), GFX_RGB(30, 36, 62),
};
#define STAR_BOTTOM (MD_GROUND_Y - 110) // no stars low over the desk

// Missile trails: a dim halo and a bright core, both brighter further down their path (always brightest at the head).
static const gfx_color_t k_trail_halo[4] = {GFX_RGB(80, 22, 22), GFX_RGB(110, 30, 26), GFX_RGB(140, 38, 30),
                                            GFX_RGB(170, 48, 34)};
static const gfx_color_t k_trail_core[4] = {GFX_RGB(190, 60, 40), GFX_RGB(230, 90, 50), GFX_RGB(255, 140, 60),
                                            GFX_RGB(255, 200, 100)};
#define TRAIL_HALO 2 // half thickness, pixels
#define TRAIL_CORE 1
// His eye beams: a soft glow, the cyan of his eyes, a white-hot core.
#define BEAM_GLOW 4
#define BEAM_CYAN 2
#define BEAM_CORE 1
#define C_BEAM_GLOW GFX_RGB(30, 104, 140)

// Blasts, by look: growing, dying down, an impact. Rings from the middle: white, yellow, orange, a dithered red edge.
enum { RING_GROW, RING_FADE, RING_IMPACT };
static const gfx_color_t k_ring[3][4] = {
    {GFX_RGB(255, 255, 240), GFX_RGB(252, 212, 84), GFX_RGB(255, 140, 40), GFX_RGB(255, 84, 84)},
    {GFX_RGB(252, 212, 84), GFX_RGB(255, 140, 40), GFX_RGB(220, 70, 50), GFX_RGB(120, 36, 44)},
    {GFX_RGB(255, 255, 240), GFX_RGB(255, 160, 60), GFX_RGB(230, 70, 40), GFX_RGB(120, 36, 44)},
};
#define BLOCK 4 // blasts are drawn in blocks of 4 x 4 pixels

// A missile's head: 3 x 3 blocks of 4 pixels, white in the middle (12 pixels: easy to see at low brightness).
#define HEAD_BLOCK 4
static const gfx_color_t k_head[3][3] = {
    {GFX_RGB(255, 140, 40), GFX_RGB(252, 212, 84), GFX_RGB(255, 140, 40)},
    {GFX_RGB(252, 212, 84), GFX_RGB(255, 255, 255), GFX_RGB(252, 212, 84)},
    {GFX_RGB(255, 140, 40), GFX_RGB(252, 212, 84), GFX_RGB(255, 140, 40)},
};

// --- The desk items -------------------------------------------------------------------------------------------------

#define ITEM_W 12
#define ITEM_H 10
#define ITEM_SCALE 4
#define ITEM_TOP (MD_GROUND_Y + 2 - ITEM_H * ITEM_SCALE)
#define ITEM_FLASH_MS 160  // destroyed: a white flash...
#define ITEM_BURN_MS 2600  // ...flames over the rubble...
#define ITEM_FLAME_MS 130  // (two flickering frames)
enum { ITEM_STAND, ITEM_FLASH, ITEM_BURN0, ITEM_BURN1, ITEM_RUBBLE }; // ...then the rubble, charred

static const char k_art_keys[] = ".KWLlDgNnYOFRrGcmoSEPkb";
static gfx_color_t s_art_colors[] = {
    0,                      // . clear
    GFX_RGB(22, 26, 44),    // K outline
    GFX_RGB(236, 242, 255), // W white
    GFX_RGB(204, 210, 220), // L light (china, silver)
    GFX_RGB(128, 134, 148), // l shade
    GFX_RGB(20, 22, 26),    // D can black
    GFX_RGB(70, 76, 88),    // g gloss
    GFX_RGB(124, 255, 40),  // N Monster green
    GFX_RGB(34, 96, 26),    // n dark green
    GFX_RGB(252, 212, 84),  // Y yellow
    GFX_RGB(226, 150, 70),  // O orange (pot, beak, dough)
    GFX_RGB(255, 140, 40),  // F flame
    GFX_RGB(255, 84, 84),   // R red
    GFX_RGB(110, 36, 44),   // r dark red
    GFX_RGB(88, 224, 128),  // G leaf
    GFX_RGB(64, 184, 72),   // c leaf, dark
    GFX_RGB(128, 76, 40),   // m brown
    GFX_RGB(78, 44, 24),    // o coffee
    GFX_RGB(14, 22, 32),    // S screen
    GFX_RGB(96, 232, 255),  // E cyan
    GFX_RGB(255, 150, 190), // P pink icing
    GFX_RGB(12, 14, 26),    // k soot
    GFX_RGB(110, 100, 96),  // b ash
};
#define ART_COLORS (sizeof(s_art_colors) / sizeof(s_art_colors[0]))
_Static_assert(sizeof(k_art_keys) - 1 == ART_COLORS, "a colour for every key");

static const char *const k_items[MD_ITEMS][ITEM_H] = {
    { // the coffee mug
        "............", "..KKKKKKK...", ".KWWWWWWWK..", ".KoooooooKKK", ".KLLLLLLLK.K",
        ".KLLRRLLLK.K", ".KLLRRLLLKKK", ".KLLLLLLLK..", ".KlllllllK..", "..KKKKKKK...",
    },
    { // the plant
        "...G..G.....", "..GcG.GcG...", ".GccG.GccG..", "..GccGccG...", "....GcG.....",
        "..KKKKKKKK..", "..KOOOOOOK..", "...KOmOOK...", "...KOOOOK...", "....KKKK....",
    },
    { // the Monster can
        "..KKKKKKK...", ".KLLLWLLLK..", ".KDDDDDDDK..", ".KDNDNDNDK..", ".KDNDNDNDK..",
        ".KDDNDNDDK..", ".KDNDDNDDK..", ".KDnnnnnDK..", ".KlllllllK..", "..KKKKKKK...",
    },
    { // the laptop
        "............", "..KKKKKKKK..", "..KSSSSSSK..", "..KSEEESSK..", "..KSSSESSK..",
        "..KSEESSSK..", "..KKKKKKKK..", "KLLLLLLLLLLK", "KllllllllllK", "KKKKKKKKKKKK",
    },
    { // the rubber duck
        "............", "....KKKK....", "...KYYYYK...", "...KYKYYKK..", "...KYYYYOOK.",
        ".K..KYYYKK..", "KYK.KYYYYK..", "KYYKYYYYYYK.", ".KYYYYYYYK..", "..KKKKKKK...",
    },
    { // the donut
        "............", "...KKKKKK...", "..KPPWPPPK..", ".KPRPPKPWPK.", ".KPPK..KPPK.",
        ".KOPK..KPOK.", ".KOOPKKPOOK.", "..KOOOOOOK..", "...KKKKKK...", "............",
    },
};
static const char *const k_rubble[ITEM_H] = { // charred and smouldering: ash with glowing embers
    "............", "............", "............", "............", "............",
    "....k.......", "..kbk..k....", ".kbrkbkFk...", "kbbkFbkbrkk.", "kkkkkkkkkkkk",
};
static const char *const k_flames[2][ITEM_H] = {
    {
        ".....R......", "....RFR.....", "...RFYFR..R.", "..RFYYFR.RFR", "..RFYWYFRFYF",
        ".RFYWWYFRFYF", ".RFYYYYFFYFR", "..RFFFFFRFR.", "............", "............",
    },
    {
        "......R.....", ".R...RFR....", "RFR.RFYFR...", "RFYRFYYFR...", "FYFRFYWYFR..",
        "FYFRFYWWYFR.", "RFYFFYYYYFR.", ".RFRFFFFFR..", "............", "............",
    },
};

static uint8_t s_pool[(MD_ITEMS + 3) * ITEM_W * ITEM_H]; // decoded pixels, internal RAM
static gfx_image_t s_item_img[MD_ITEMS], s_rubble_img, s_flame_img[2];
static bool s_art_ready;

static bool art_init(void)
{
    if (s_art_ready) {
        return true;
    }
    size_t used = 0;
    bool ok = true;
    for (int i = 0; ok && i < MD_ITEMS; i++) {
        ok = gfx_image_from_ascii(&s_item_img[i], s_pool + used, sizeof(s_pool) - used, k_items[i], ITEM_H,
                                  k_art_keys);
        used += ITEM_W * ITEM_H;
    }
    ok = ok && gfx_image_from_ascii(&s_rubble_img, s_pool + used, sizeof(s_pool) - used, k_rubble, ITEM_H, k_art_keys);
    used += ITEM_W * ITEM_H;
    for (int f = 0; ok && f < 2; f++) {
        ok = gfx_image_from_ascii(&s_flame_img[f], s_pool + used, sizeof(s_pool) - used, k_flames[f], ITEM_H,
                                  k_art_keys);
        used += ITEM_W * ITEM_H;
    }
    for (int i = 0; ok && i < MD_ITEMS; i++) {
        ok = s_item_img[i].w == ITEM_W && s_item_img[i].h == ITEM_H;
    }
    s_art_ready = ok;
    return ok;
}

// --- Geometry -------------------------------------------------------------------------------------------------------

#define HUD_H 44
#define PLATE_X (MD_BUDDY_X0 + 2)                  // the plate he stands on
#define PLATE_W (28 * MD_BUDDY_SCALE - 4)
#define PLATE_Y (MD_GROUND_Y - 4)
#define PLATE_H 12
#define EXIT_X (MD_W - 60)
#define EXIT_Y 4
#define EXIT_W 52
#define EXIT_H 34
#define BUTTON_W 160
#define BUTTON_H 52
#define BUTTON_Y 214
#define RETRY_X (MD_W / 2 - 32 - BUTTON_W)
#define QUIT_X (MD_W / 2 + 32)
#define HEAT_X (MD_BUDDY_X - 61)
#define HEAT_Y 583
#define HEAT_SEGS 10
#define HEAT_W (HEAT_SEGS * 12 + 2)
#define HEAT_H 14
#define BUBBLE_X (MD_BUDDY_X0 + 28 * MD_BUDDY_SCALE + 10) // right of his head...
#define BUBBLE_BOTTOM (MD_BUDDY_Y0 + 12 * MD_BUDDY_SCALE) // ...its tail pointing at his face
#define BUBBLE_PAD 10
#define BUBBLE_TAIL 14
#define TEXT_SCALE_SAY 2
#define POP_MS 700
#define TOUCH_PAD 8  // buttons take touches this far beyond their edges
#define SLOP_PX 24   // a finger that slides further than this no longer presses its button

// --- Small helpers --------------------------------------------------------------------------------------------------

static int min_i(int a, int b)
{
    return a < b ? a : b;
}

static int max_i(int a, int b)
{
    return a > b ? a : b;
}

static int clamp_i(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int floor_div(int a, int b) // b > 0
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static int isqrt(int v)
{
    if (v <= 0) {
        return 0;
    }
    int r = 0;
    while ((r + 1) * (r + 1) <= v) {
        r++;
    }
    return r;
}

static uint32_t min_u(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

// Fills screen columns [a, b) of the row segment that starts at screen column x and is w wide.
static void fill(gfx_color_t *dst, int x, int w, int a, int b, gfx_color_t c)
{
    a = max_i(a, x);
    b = min_i(b, x + w);
    for (int i = a; i < b; i++) {
        dst[i - x] = c;
    }
}

static uint32_t hash32(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

static void invalidate(md_view_t *v, gfx_rect_t r)
{
    if (r.w > 0 && r.h > 0) {
        gfx_scene_invalidate(v->scene, r);
        v->pieces++;
    }
}

// --- The background: the sky, the desk, his plate -------------------------------------------------------------------

void md_view_background(void *ctx, gfx_color_t *dst, int x, int y, int w)
{
    (void)ctx;
    if (y < HUD_H) {
        fill(dst, x, w, x, x + w, C_BAR);
        return;
    }
    if (y < MD_SKY_TOP) {
        fill(dst, x, w, x, x + w, C_EDGE); // the score bar's lower edge
        return;
    }
    if (y < MD_GROUND_Y) {
        fill(dst, x, w, x, x + w, k_sky[(y - MD_SKY_TOP) * SKY_BANDS / (MD_GROUND_Y - MD_SKY_TOP)]);
        // A star on about one row pair in five, 2 x 2 pixels, some brighter.
        const uint32_t h = hash32((uint32_t)(y / 2) * 2654435761u + 7u);
        if (y < STAR_BOTTOM && h % 5 == 0) {
            const int sx = (int)((h >> 8) % MD_W) & ~1;
            fill(dst, x, w, sx, sx + 2, (h >> 4) % 4 == 0 ? GFX_RGB(180, 196, 230) : GFX_RGB(90, 104, 140));
        }
        return;
    }
    fill(dst, x, w, x, x + w, y < MD_GROUND_Y + 4 ? C_EDGE : C_DESK);
    if (y == MD_GROUND_Y + 30 || y == MD_GROUND_Y + 31) { // wood grain
        for (int gx = (x / 64) * 64; gx < x + w; gx += 64) {
            fill(dst, x, w, gx, gx + 40, C_GRAIN);
        }
    }
    if (y >= PLATE_Y && y < PLATE_Y + PLATE_H) {
        const bool rim = y < PLATE_Y + 2 || y >= PLATE_Y + PLATE_H - 2;
        fill(dst, x, w, PLATE_X, PLATE_X + PLATE_W, C_OUTLINE);
        if (!rim) {
            fill(dst, x, w, PLATE_X + 2, PLATE_X + PLATE_W - 2, C_EDGE);
        }
    }
}

// --- Lines (trails and beams) ---------------------------------------------------------------------------------------

enum { STROKE_TRAIL, STROKE_BEAM };

static int stroke_reach(const md_stroke_t *s) // the widest half thickness it is drawn with
{
    return s->style == STROKE_BEAM ? BEAM_GLOW : TRAIL_HALO;
}

// x of the whole line at row y (y within ay..by).
static int line_x(const md_stroke_t *s, int y)
{
    if (s->by == s->ay) {
        return s->ax;
    }
    const int num = (s->bx - s->ax) * (y - s->ay), den = s->by - s->ay;
    return s->ax + (num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den));
}

// The columns [*a, *b) of row y that the drawn part covers at half thickness h. False if none.
static bool stroke_span(const md_stroke_t *s, int y, int h, int *a, int *b)
{
    if (y < s->from_y - h || y > s->to_y + h) {
        return false;
    }
    if (s->by == s->ay) { // level: the whole width, h rows either side
        *a = min_i(s->ax, s->bx) - h;
        *b = max_i(s->ax, s->bx) + h + 1;
        return true;
    }
    const int xa = line_x(s, clamp_i(y - h, s->from_y, s->to_y));
    const int xb = line_x(s, clamp_i(y + h + 1, s->from_y, s->to_y));
    *a = min_i(xa, xb) - h;
    *b = max_i(xa, xb) + h + 1;
    return true;
}

// Everything the stroke draws in rows r0..r1 (inclusive).
static gfx_rect_t stroke_box(const md_stroke_t *s, int r0, int r1)
{
    const int h = stroke_reach(s);
    r0 = max_i(r0, s->from_y - h);
    r1 = min_i(r1, s->to_y + h);
    if (r1 < r0) {
        return (gfx_rect_t){0, 0, 0, 0};
    }
    int a0, b0, a1, b1;
    stroke_span(s, r0, h, &a0, &b0);
    stroke_span(s, r1, h, &a1, &b1);
    const int x0 = min_i(a0, a1) - 1, x1 = max_i(b0, b1) + 1;
    return (gfx_rect_t){x0, r0, x1 - x0, r1 - r0 + 1};
}

// Invalidates rows r0..r1 of what the stroke draws, in pieces of MD_VIEW_PIECE_ROWS rows.
static void invalidate_stroke(md_view_t *v, const md_stroke_t *s, int r0, int r1)
{
    const int h = stroke_reach(s);
    r0 = max_i(r0, s->from_y - h);
    r1 = min_i(r1, s->to_y + h);
    for (int p = r0; p <= r1; p += MD_VIEW_PIECE_ROWS) {
        invalidate(v, stroke_box(s, p, min_i(p + MD_VIEW_PIECE_ROWS - 1, r1)));
    }
}

static bool same_line(const md_stroke_t *a, const md_stroke_t *b)
{
    return a->style == b->style && a->ax == b->ax && a->ay == b->ay && a->bx == b->bx && a->by == b->by;
}

// What changed between two states of a stroke: all of it if it came, went or moved; else only the rows around the
// ends that moved (the line itself is the same, so every other row is drawn exactly as before).
static void stroke_changed(md_view_t *v, const md_stroke_t *was, const md_stroke_t *now)
{
    if (!was->on && !now->on) {
        return;
    }
    if (was->on != now->on || !same_line(was, now)) {
        if (was->on) {
            invalidate_stroke(v, was, was->from_y - BEAM_GLOW, was->to_y + BEAM_GLOW);
        }
        if (now->on) {
            invalidate_stroke(v, now, now->from_y - BEAM_GLOW, now->to_y + BEAM_GLOW);
        }
        return;
    }
    const int m = 2 * stroke_reach(now) + 2;
    if (was->to_y != now->to_y) {
        const int r0 = min_i(was->to_y, now->to_y) - m, r1 = max_i(was->to_y, now->to_y) + m;
        invalidate_stroke(v, was, r0, r1);
        invalidate_stroke(v, now, r0, r1);
    }
    if (was->from_y != now->from_y) {
        const int r0 = min_i(was->from_y, now->from_y) - m, r1 = max_i(was->from_y, now->from_y) + m;
        invalidate_stroke(v, was, r0, r1);
        invalidate_stroke(v, now, r0, r1);
    }
}

static void stroke_row(const md_stroke_t *s, gfx_color_t *dst, int x, int y, int w)
{
    int a, b;
    if (s->style == STROKE_BEAM) {
        if (stroke_span(s, y, BEAM_GLOW, &a, &b)) {
            fill(dst, x, w, a, b, C_BEAM_GLOW);
        }
        if (stroke_span(s, y, BEAM_CYAN, &a, &b)) {
            fill(dst, x, w, a, b, C_CYAN);
        }
        if (stroke_span(s, y, BEAM_CORE, &a, &b)) {
            fill(dst, x, w, a, b, C_WHITE);
        }
        return;
    }
    const int q = clamp_i((y - s->ay) * 4 / (s->by - s->ay + 1), 0, 3);
    if (stroke_span(s, y, TRAIL_HALO, &a, &b)) {
        fill(dst, x, w, a, b, k_trail_halo[q]);
    }
    if (stroke_span(s, y, TRAIL_CORE, &a, &b)) {
        fill(dst, x, w, a, b, k_trail_core[q]);
    }
}

// A missile's head sits on its stroke's lower drawn end.
static gfx_rect_t head_box(const md_stroke_t *s)
{
    const int hx = line_x(s, s->to_y), hy = s->to_y;
    return (gfx_rect_t){hx - HEAD_BLOCK * 3 / 2, hy - HEAD_BLOCK * 3 / 2, HEAD_BLOCK * 3, HEAD_BLOCK * 3};
}

static void head_row(const md_stroke_t *s, gfx_color_t *dst, int x, int y, int w)
{
    const gfx_rect_t r = head_box(s);
    if (y < r.y || y >= r.y + r.h) {
        return;
    }
    const int j = (y - r.y) / HEAD_BLOCK;
    for (int i = 0; i < 3; i++) {
        fill(dst, x, w, r.x + i * HEAD_BLOCK, r.x + (i + 1) * HEAD_BLOCK, k_head[j][i]);
    }
}

// --- Blasts ---------------------------------------------------------------------------------------------------------

static gfx_rect_t ring_box(const md_ring_t *r)
{
    const int e = r->r * BLOCK + BLOCK / 2 + 1;
    return (gfx_rect_t){r->x - e, r->y - e, 2 * e, 2 * e};
}

static void ring_row(const md_ring_t *r, gfx_color_t *dst, int x, int y, int w)
{
    const int R = r->r, j = floor_div(y - r->y + BLOCK / 2, BLOCK);
    if (j < -R || j > R) {
        return;
    }
    const int rc = R * 35 / 100, ry = R * 60 / 100, ro = R * 85 / 100;
    const int hw = isqrt(R * R - j * j);
    const gfx_color_t *c = k_ring[r->look < 3 ? r->look : 0];
    for (int i = -hw; i <= hw; i++) {
        const int d = i * i + j * j;
        gfx_color_t col;
        if (d <= rc * rc) {
            col = c[0];
        } else if (d <= ry * ry) {
            col = c[1];
        } else if (d <= ro * ro) {
            col = c[2];
        } else if (((i + j) & 1) == 0) {
            col = c[3];
        } else {
            continue;
        }
        const int bx = r->x + i * BLOCK - BLOCK / 2;
        fill(dst, x, w, bx, bx + BLOCK, col);
    }
}

// --- Items ----------------------------------------------------------------------------------------------------------

static gfx_rect_t item_box(int i)
{
    return (gfx_rect_t){md_item_x[i] - ITEM_W * ITEM_SCALE / 2, ITEM_TOP, ITEM_W * ITEM_SCALE, ITEM_H * ITEM_SCALE};
}

// One row of an indexed image at scale, top-left at (ix, iy); flash: every pixel white.
static void image_row(const gfx_image_t *img, int ix, int iy, int scale, bool flash, gfx_color_t *dst, int x, int y,
                      int w)
{
    const int row = (y - iy) / scale;
    if (y < iy || row >= img->h) {
        return;
    }
    const uint8_t *px = img->px + (size_t)row * img->w;
    const int c0 = max_i(0, (x - ix) / scale), c1 = min_i(img->w, (x + w - ix + scale - 1) / scale);
    for (int c = c0; c < c1; c++) {
        if (px[c]) {
            fill(dst, x, w, ix + c * scale, ix + (c + 1) * scale, flash ? C_WHITE : s_art_colors[px[c]]);
        }
    }
}

static void item_row(int i, uint8_t look, gfx_color_t *dst, int x, int y, int w)
{
    const gfx_rect_t r = item_box(i);
    if (y < r.y || y >= r.y + r.h || r.x >= x + w || r.x + r.w <= x) {
        return;
    }
    if (look == ITEM_STAND || look == ITEM_FLASH) {
        image_row(&s_item_img[i], r.x, r.y, ITEM_SCALE, look == ITEM_FLASH, dst, x, y, w);
        return;
    }
    image_row(&s_rubble_img, r.x, r.y, ITEM_SCALE, false, dst, x, y, w);
    if (look == ITEM_BURN0 || look == ITEM_BURN1) {
        image_row(&s_flame_img[look - ITEM_BURN0], r.x, r.y, ITEM_SCALE, false, dst, x, y, w);
    }
}

// --- Text and boxes -------------------------------------------------------------------------------------------------

static gfx_rect_t text_box(const md_text_t *t)
{
    return (gfx_rect_t){t->x, t->y, t->n * t->scale, 7 * t->scale};
}

static void text_row(const md_text_t *t, gfx_color_t *dst, int x, int y, int w)
{
    const int r = (y - t->y) / t->scale;
    if (y < t->y || r >= 7 || t->x >= x + w || t->x + t->n * t->scale <= x) {
        return;
    }
    const gfx_color_t c = k_ink[t->ink < sizeof(k_ink) / sizeof(k_ink[0]) ? t->ink : 0];
    for (int i = 0; i < t->n; i++) {
        if ((t->cols[i] >> r) & 1u) {
            fill(dst, x, w, t->x + i * t->scale, t->x + (i + 1) * t->scale, c);
        }
    }
}

static gfx_rect_t box_box(int which, const md_box_t *b)
{
    return (gfx_rect_t){b->x, b->y, b->w, b->h + (which == MD_VIEW_BOX_BUBBLE ? BUBBLE_TAIL : 0)};
}

static void box_row(int which, const md_box_t *b, gfx_color_t *dst, int x, int y, int w)
{
    const int x0 = b->x, x1 = b->x + b->w, y0 = b->y, y1 = b->y + b->h;
    if (which == MD_VIEW_BOX_BUBBLE) {
        if (y >= y1 && y < y1 + BUBBLE_TAIL) { // the tail, down and to the left towards his face
            const int k = y - y1, tx = x0 + 22 - k;
            fill(dst, x, w, tx - 3, tx + 12 - k / 2 + 3, C_OUTLINE);
            if (k < BUBBLE_TAIL - 4) {
                fill(dst, x, w, tx, tx + 12 - k / 2, C_WHITE);
            }
            return;
        }
        if (y < y0 || y >= y1) {
            return;
        }
        fill(dst, x, w, x0, x1, C_OUTLINE);
        if (y >= y0 + 3 && y < y1 - 3) {
            fill(dst, x, w, x0 + 3, x1 - 3, C_WHITE);
        } else if (y >= y1 - 3) {
            fill(dst, x, w, x0 + 22, x0 + 34, C_WHITE); // open into the tail
        }
        return;
    }
    if (y < y0 || y >= y1) {
        return;
    }
    if (which == MD_VIEW_BOX_HEAT) { // a dark frame and ten segments: cyan, then amber, then red; all red when hot
        fill(dst, x, w, x0, x1, C_OUTLINE);
        if (y >= y0 + 2 && y < y1 - 2) {
            const int lit = b->look & 0x7F;
            const bool hot = (b->look & 0x80) != 0;
            for (int s = 0; s < HEAT_SEGS; s++) {
                const gfx_color_t on = hot ? C_RED : s < 6 ? C_CYAN : s < 8 ? C_AMBER : C_RED;
                fill(dst, x, w, x0 + 2 + s * 12, x0 + 2 + s * 12 + 10, s < lit ? on : C_HEAT_OFF);
            }
        }
        return;
    }
    // A plate: a rim (cyan for RETRY), its face lighter while pressed.
    const bool pressed = (b->look & 1u) != 0;
    const gfx_color_t rim = which == MD_VIEW_BOX_RETRY ? C_CYAN : which == MD_VIEW_BOX_QUIT ? C_WHITE : C_EDGE;
    fill(dst, x, w, x0, x1, rim);
    if (y >= y0 + 3 && y < y1 - 3) {
        fill(dst, x, w, x0 + 3, x1 - 3, pressed ? C_PLATE_HOT : C_PLATE);
    }
}

// --- The overlay ----------------------------------------------------------------------------------------------------

void md_view_overlay(void *ctx, gfx_color_t *dst, int x, int y, int w)
{
    const md_view_t *v = ctx;
    if (!v->shown) {
        return;
    }
    const md_layer_t *L = &v->drawn;
    if (y >= ITEM_TOP && y < ITEM_TOP + ITEM_H * ITEM_SCALE) {
        for (int i = 0; i < MD_ITEMS; i++) {
            item_row(i, L->item[i].look, dst, x, y, w);
        }
    }
    for (int i = 0; i < MD_MAX_MISSILES; i++) {
        if (L->stroke[i].on) {
            stroke_row(&L->stroke[i], dst, x, y, w);
        }
    }
    for (int i = 0; i < MD_MAX_MISSILES; i++) {
        if (L->stroke[i].on && L->head[i]) {
            head_row(&L->stroke[i], dst, x, y, w);
        }
    }
    for (int i = 0; i < MD_MAX_BLASTS; i++) {
        if (L->ring[i].on) {
            ring_row(&L->ring[i], dst, x, y, w);
        }
    }
    for (int i = MD_MAX_MISSILES; i < MD_VIEW_STROKES; i++) {
        if (L->stroke[i].on) {
            stroke_row(&L->stroke[i], dst, x, y, w);
        }
    }
    for (int i = 0; i < MD_VIEW_BOXES; i++) {
        if (L->box[i].on) {
            box_row(i, &L->box[i], dst, x, y, w);
        }
    }
    for (int i = 0; i < MD_VIEW_TEXTS; i++) {
        if (L->text[i].on) {
            text_row(&L->text[i], dst, x, y, w);
        }
    }
}

// --- Bringing the layer up to date ----------------------------------------------------------------------------------

static void set_text(md_text_t *t, const char *s, int x, int y, int scale, uint8_t ink)
{
    memset(t, 0, sizeof(*t));
    t->on = true;
    t->n = (uint8_t)marquee_text_columns(s, t->cols, MD_VIEW_TEXT_COLS);
    t->x = (int16_t)x;
    t->y = (int16_t)y;
    t->scale = (uint8_t)scale;
    t->ink = ink;
}

static void center_text(md_text_t *t, const char *s, int cx, int y, int scale, uint8_t ink)
{
    set_text(t, s, 0, y, scale, ink);
    t->x = (int16_t)(cx - t->n * scale / 2);
}

static void set_box(md_box_t *b, int x, int y, int w, int h, uint8_t look)
{
    *b = (md_box_t){true, look, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
}

static void beam(md_stroke_t *s, int ex, int ey, int tx, int ty)
{
    const bool down = ty >= ey; // the eye above the target (a tap low at the side)
    *s = (md_stroke_t){true, STROKE_BEAM, (int16_t)(down ? ex : tx), (int16_t)(down ? ey : ty),
                       (int16_t)(down ? tx : ex), (int16_t)(down ? ty : ey), 0, 0};
    s->from_y = s->ay;
    s->to_y = s->by;
}

static uint8_t item_look(const md_t *g, int i, uint32_t now, uint32_t *next)
{
    if ((g->items >> i) & 1u) {
        return ITEM_STAND;
    }
    const uint32_t t = now - g->item_lost_at[i];
    if (t < ITEM_FLASH_MS) {
        *next = min_u(*next, ITEM_FLASH_MS - t);
        return ITEM_FLASH;
    }
    if (t < ITEM_BURN_MS) {
        *next = min_u(*next, ITEM_FLAME_MS - t % ITEM_FLAME_MS);
        return (t / ITEM_FLAME_MS) & 1u ? ITEM_BURN1 : ITEM_BURN0;
    }
    return ITEM_RUBBLE;
}

// The layer as the game is at now. The world (missiles, blasts, beams, points, heat) is drawn at md_world_ms: after
// the game is lost it stands still. The desk items' own flash and flames run their course (decoration, not play), as do
// the game-over screen and Buddy.
static uint32_t build(md_view_t *v, const md_t *g, uint32_t now, md_layer_t *L)
{
    memset(L, 0, sizeof(*L));
    uint32_t next = UINT32_MAX, still = UINT32_MAX;
    const uint32_t w = md_world_ms(g, now);
    const bool frozen = w != now;
    uint32_t *world_next = frozen ? &still : &next; // a frozen world never wakes the screen
    char s[48];

    for (int i = 0; i < MD_ITEMS; i++) {
        L->item[i].look = item_look(g, i, now, &next);
    }

    for (int i = 0; i < MD_MAX_MISSILES; i++) {
        const md_missile_t *m = &g->missile[i];
        md_stroke_t *st = &L->stroke[i];
        if (m->state == MD_MISSILE_FREE) {
            continue;
        }
        *st = (md_stroke_t){true, STROKE_TRAIL, m->x0, m->y0, m->x1, m->y1, m->y0, m->y0};
        if (m->state == MD_MISSILE_FLYING) {
            int hx, hy;
            md_missile_head(m, w, &hx, &hy);
            st->to_y = (int16_t)hy;
            L->head[i] = 1;
        } else { // fading: the trail shrinks back into where it ended
            const uint32_t t = w - m->end_at;
            const int f = t >= MD_TRAIL_FADE_MS ? 1000 : (int)(t * 1000 / MD_TRAIL_FADE_MS);
            st->to_y = m->ey;
            st->from_y = (int16_t)(m->y0 + (m->ey - m->y0) * f / 1000);
            if (st->from_y >= st->to_y) {
                st->on = false;
            }
        }
    }

    if (g->shots > 0 && w - g->shot_at < MD_BEAM_MS) {
        beam(&L->stroke[MD_MAX_MISSILES], v->eye_x[0], v->eye_y[0], g->shot_x, g->shot_y);
        beam(&L->stroke[MD_MAX_MISSILES + 1], v->eye_x[1], v->eye_y[1], g->shot_x, g->shot_y);
        *world_next = min_u(*world_next, MD_BEAM_MS - (w - g->shot_at));
    }

    for (int i = 0; i < MD_MAX_BLASTS; i++) {
        const md_blast_t *b = &g->blast[i];
        const int r = md_blast_radius(b, w);
        if (r <= 0) {
            continue;
        }
        const uint8_t look = b->kind == MD_BLAST_IMPACT                               ? RING_IMPACT
                             : w - b->born >= MD_BLAST_GROW_MS + MD_BLAST_HOLD_MS ? RING_FADE
                                                                                     : RING_GROW;
        L->ring[i] = (md_ring_t){true, look, b->x, b->y, (uint8_t)max_i(1, (r + BLOCK / 2) / BLOCK)};
    }

    // The score bar.
    set_text(&L->text[MD_VIEW_TEXT_SCORE_LABEL], "SCORE", 16, 5, 2, INK_DIM);
    snprintf(s, sizeof(s), "%05u", (unsigned)g->score);
    set_text(&L->text[MD_VIEW_TEXT_SCORE], s, 16, 23, 2, INK_AMBER);
    snprintf(s, sizeof(s), "WAVE %u", (unsigned)g->wave);
    center_text(&L->text[MD_VIEW_TEXT_WAVE], s, MD_W / 2, 12, 3, INK_CYAN);
    snprintf(s, sizeof(s), "BEST %05u", (unsigned)g->best);
    set_text(&L->text[MD_VIEW_TEXT_BEST], s, 0, 15, 2, INK_DIM);
    L->text[MD_VIEW_TEXT_BEST].x = (int16_t)(EXIT_X - 20 - L->text[MD_VIEW_TEXT_BEST].n * 2);
    set_box(&L->box[MD_VIEW_BOX_EXIT], EXIT_X, EXIT_Y, EXIT_W, EXIT_H, v->pressed == MD_VIEW_HIT_EXIT);
    center_text(&L->text[MD_VIEW_TEXT_EXIT], "X", EXIT_X + EXIT_W / 2, EXIT_Y + (EXIT_H - 14) / 2, 2, INK_WHITE);

    // The heat of his eyes, under his feet.
    const int heat = md_heat(g, w);
    set_box(&L->box[MD_VIEW_BOX_HEAT], HEAT_X, HEAT_Y, HEAT_W, HEAT_H,
            g->hot ? (uint8_t)(0x80 | HEAT_SEGS) : (uint8_t)((heat * HEAT_SEGS + 99) / 100));
    if (heat > 0 && !g->hot) {
        *world_next = min_u(*world_next, 100); // it cools down a segment at a time
    }

    // The banners.
    switch (g->phase) {
    case MD_READY:
        snprintf(s, sizeof(s), "WAVE %u", (unsigned)g->wave);
        center_text(&L->text[MD_VIEW_TEXT_BANNER], s, MD_W / 2, 110, 6, INK_CYAN);
        center_text(&L->text[MD_VIEW_TEXT_BANNER2], "GET READY!", MD_W / 2, 172, 3, INK_WHITE);
        break;
    case MD_CLEAR:
        snprintf(s, sizeof(s), "WAVE %u CLEAR!", (unsigned)g->wave);
        center_text(&L->text[MD_VIEW_TEXT_BANNER], s, MD_W / 2, 120, 5, INK_CYAN);
        snprintf(s, sizeof(s), "DESK SAVED %u X %u", (unsigned)g->saved, (unsigned)MD_POINTS_ITEM);
        center_text(&L->text[MD_VIEW_TEXT_BANNER2], s, MD_W / 2, 182, 3, INK_AMBER);
        break;
    case MD_OVER:
        center_text(&L->text[MD_VIEW_TEXT_BANNER], "GAME OVER", MD_W / 2, 92, 7, INK_RED);
        snprintf(s, sizeof(s), g->score > g->best_before ? "NEW BEST %05u" : "SCORE %05u", (unsigned)g->score);
        center_text(&L->text[MD_VIEW_TEXT_BANNER2], s, MD_W / 2, 164, 3, INK_AMBER);
        if (md_buttons(g, now)) {
            set_box(&L->box[MD_VIEW_BOX_RETRY], RETRY_X, BUTTON_Y, BUTTON_W, BUTTON_H,
                    v->pressed == MD_VIEW_HIT_RETRY);
            set_box(&L->box[MD_VIEW_BOX_QUIT], QUIT_X, BUTTON_Y, BUTTON_W, BUTTON_H, v->pressed == MD_VIEW_HIT_QUIT);
            center_text(&L->text[MD_VIEW_TEXT_RETRY], "RETRY", RETRY_X + BUTTON_W / 2, BUTTON_Y + 16, 3, INK_CYAN);
            center_text(&L->text[MD_VIEW_TEXT_QUIT], "QUIT", QUIT_X + BUTTON_W / 2, BUTTON_Y + 16, 3, INK_WHITE);
        }
        break;
    default:
        break;
    }

    // His line, in a bubble beside his head (one or two lines).
    const char *line = md_line(g, now);
    if (line) {
        char first[24];
        const char *nl = strchr(line, '\n');
        const size_t n1 = nl ? (size_t)(nl - line) : strlen(line);
        snprintf(first, sizeof(first), "%.*s", (int)(n1 < sizeof(first) - 1 ? n1 : sizeof(first) - 1), line);
        md_text_t *t1 = &L->text[MD_VIEW_TEXT_SAY], *t2 = &L->text[MD_VIEW_TEXT_SAY2];
        const int lines = nl ? 2 : 1, lh = 7 * TEXT_SCALE_SAY;
        const int h = lines * lh + (lines - 1) * 4 + 2 * BUBBLE_PAD + 6, y0 = BUBBLE_BOTTOM - h;
        set_text(t1, first, BUBBLE_X + 3 + BUBBLE_PAD, y0 + 3 + BUBBLE_PAD, TEXT_SCALE_SAY, INK_DARK);
        int cols = t1->n;
        if (nl) {
            set_text(t2, nl + 1, t1->x, t1->y + lh + 4, TEXT_SCALE_SAY, INK_DARK);
            cols = max_i(cols, t2->n);
        }
        set_box(&L->box[MD_VIEW_BOX_BUBBLE], BUBBLE_X, y0, cols * TEXT_SCALE_SAY + 2 * BUBBLE_PAD + 6, h, 0);
    }

    // The points just scored, where they were made.
    if (g->pop_points && w - g->pop_at < POP_MS) {
        if (g->pop_mult > 1) {
            snprintf(s, sizeof(s), "+%u X%u", (unsigned)g->pop_points, (unsigned)g->pop_mult);
        } else {
            snprintf(s, sizeof(s), "+%u", (unsigned)g->pop_points);
        }
        md_text_t *t = &L->text[MD_VIEW_TEXT_POP];
        center_text(t, s, g->pop_x, g->pop_y - MD_CHAIN_RADIUS - 22, 2, INK_AMBER);
        t->x = (int16_t)clamp_i(t->x, 4, MD_W - 4 - t->n * 2);
        t->y = (int16_t)clamp_i(t->y, MD_SKY_TOP + 2, MD_GROUND_Y - 16);
        *world_next = min_u(*world_next, POP_MS - (w - g->pop_at));
    }
    return next;
}

bool md_view_init(md_view_t *v, gfx_scene_t *scene)
{
    memset(v, 0, sizeof(*v));
    v->scene = scene;
    return art_init();
}

void md_view_show(md_view_t *v, bool shown)
{
    v->shown = shown;
    memset(&v->drawn, 0, sizeof(v->drawn));
    memset(&v->now, 0, sizeof(v->now));
    v->pressed = MD_VIEW_HIT_NONE;
}

uint32_t md_view_apply(md_view_t *v, const md_t *g, const int eye_x[2], const int eye_y[2], uint32_t now)
{
    v->pieces = 0;
    if (!v->shown) {
        return UINT32_MAX;
    }
    for (int i = 0; i < 2; i++) {
        v->eye_x[i] = (int16_t)eye_x[i];
        v->eye_y[i] = (int16_t)eye_y[i];
    }
    md_layer_t *L = &v->now;
    const uint32_t next = build(v, g, now, L);
    const md_layer_t *D = &v->drawn;

    for (int i = 0; i < MD_ITEMS; i++) {
        if (L->item[i].look != D->item[i].look) {
            invalidate(v, item_box(i));
        }
    }
    for (int i = 0; i < MD_VIEW_STROKES; i++) {
        stroke_changed(v, &D->stroke[i], &L->stroke[i]);
    }
    for (int i = 0; i < MD_MAX_MISSILES; i++) {
        const bool was = D->head[i] && D->stroke[i].on, is = L->head[i] && L->stroke[i].on;
        if (was != is || (is && (D->stroke[i].to_y != L->stroke[i].to_y || !same_line(&D->stroke[i], &L->stroke[i])))) {
            if (was) {
                invalidate(v, head_box(&D->stroke[i]));
            }
            if (is) {
                invalidate(v, head_box(&L->stroke[i]));
            }
        }
    }
    for (int i = 0; i < MD_MAX_BLASTS; i++) {
        if (memcmp(&L->ring[i], &D->ring[i], sizeof(md_ring_t)) != 0) {
            if (D->ring[i].on) {
                invalidate(v, ring_box(&D->ring[i]));
            }
            if (L->ring[i].on) {
                invalidate(v, ring_box(&L->ring[i]));
            }
        }
    }
    for (int i = 0; i < MD_VIEW_TEXTS; i++) {
        if (memcmp(&L->text[i], &D->text[i], sizeof(md_text_t)) != 0) {
            if (D->text[i].on) {
                invalidate(v, text_box(&D->text[i]));
            }
            if (L->text[i].on) {
                invalidate(v, text_box(&L->text[i]));
            }
        }
    }
    for (int i = 0; i < MD_VIEW_BOXES; i++) {
        if (memcmp(&L->box[i], &D->box[i], sizeof(md_box_t)) != 0) {
            if (D->box[i].on) {
                invalidate(v, box_box(i, &D->box[i]));
            }
            if (L->box[i].on) {
                invalidate(v, box_box(i, &L->box[i]));
            }
        }
    }
    v->drawn = *L;
    return next;
}

// --- Touch ----------------------------------------------------------------------------------------------------------

static bool in_box(int x, int y, int bx, int by, int bw, int bh)
{
    return x >= bx - TOUCH_PAD && x < bx + bw + TOUCH_PAD && y >= by - TOUCH_PAD && y < by + bh + TOUCH_PAD;
}

md_view_hit_t md_view_hit(const md_view_t *v, const md_t *g, int x, int y, uint32_t now)
{
    if (!v->shown || g->phase == MD_OFF) {
        return MD_VIEW_HIT_NONE;
    }
    if (md_buttons(g, now)) {
        if (in_box(x, y, RETRY_X, BUTTON_Y, BUTTON_W, BUTTON_H)) {
            return MD_VIEW_HIT_RETRY;
        }
        if (in_box(x, y, QUIT_X, BUTTON_Y, BUTTON_W, BUTTON_H)) {
            return MD_VIEW_HIT_QUIT;
        }
    }
    if (in_box(x, y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H)) {
        return MD_VIEW_HIT_EXIT;
    }
    return y >= MD_SKY_TOP ? MD_VIEW_HIT_SKY : MD_VIEW_HIT_NONE;
}

md_view_hit_t md_view_touch(md_view_t *v, const md_t *g, md_view_touch_t type, int x, int y, uint32_t now)
{
    switch (type) {
    case MD_VIEW_TOUCH_DOWN: {
        const md_view_hit_t hit = md_view_hit(v, g, x, y, now);
        v->pressed = MD_VIEW_HIT_NONE;
        if (hit == MD_VIEW_HIT_SKY || hit == MD_VIEW_HIT_NONE) {
            return hit;
        }
        v->pressed = (uint8_t)hit;
        v->down_x = (int16_t)x;
        v->down_y = (int16_t)y;
        return MD_VIEW_HIT_NONE;
    }
    case MD_VIEW_TOUCH_MOVE:
        if (v->pressed != MD_VIEW_HIT_NONE && (x - v->down_x) * (x - v->down_x) + (y - v->down_y) * (y - v->down_y) >
                                                  SLOP_PX * SLOP_PX) {
            v->pressed = MD_VIEW_HIT_NONE;
        }
        return MD_VIEW_HIT_NONE;
    case MD_VIEW_TOUCH_UP: {
        const uint8_t pressed = v->pressed;
        v->pressed = MD_VIEW_HIT_NONE;
        return pressed != MD_VIEW_HIT_NONE && md_view_hit(v, g, x, y, now) == pressed ? (md_view_hit_t)pressed
                                                                                      : MD_VIEW_HIT_NONE;
    }
    default:
        return MD_VIEW_HIT_NONE;
    }
}

void md_view_cancel(md_view_t *v)
{
    v->pressed = MD_VIEW_HIT_NONE;
}
