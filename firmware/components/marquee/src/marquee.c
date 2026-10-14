#include "marquee.h"

#include <string.h>

// 5x7 font, column-major: bit 0 is the top row. Codes 0x20..0x7E are ASCII, then MARQUEE_GLYPH_UNKNOWN (0x7F),
// MARQUEE_GLYPH_EURO, _DEGREE and _HEART.
#define FONT_FIRST 0x20
#define FONT_LAST MARQUEE_GLYPH_HEART
static const uint8_t k_font[FONT_LAST - FONT_FIRST + 1][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x00, 0x07, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // }
    {0x08, 0x04, 0x08, 0x10, 0x08}, // ~
    {0x7F, 0x41, 0x41, 0x41, 0x7F}, // unknown: hollow box
    {0x14, 0x3E, 0x55, 0x41, 0x22}, // euro
    {0x00, 0x06, 0x09, 0x09, 0x06}, // degree
    {0x0C, 0x1E, 0x3C, 0x1E, 0x0C}, // heart
};

// U+00C0..U+00FF without their accents (x for the multiplication sign, / for division).
static const char k_latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";

enum { // palette indices
    OUTLINE = 1,
    METAL,
    METAL_DARK,
    GAP,
    OFF,
    OFF_EDGE,
    ON,
    ON_EDGE,
    ON_HOT,
    COLORS,
};

static const gfx_color_t k_colors[COLORS] = {
    [OUTLINE] = GFX_RGB(6, 7, 10),        [METAL] = GFX_RGB(96, 106, 126),   [METAL_DARK] = GFX_RGB(48, 54, 68),
    [GAP] = GFX_RGB(12, 8, 6),            [OFF] = GFX_RGB(48, 26, 10),       [OFF_EDGE] = GFX_RGB(30, 17, 8),
    [ON] = GFX_RGB(255, 170, 40),         [ON_EDGE] = GFX_RGB(170, 92, 16),  [ON_HOT] = GFX_RGB(255, 232, 160),
};
static const gfx_palette_t k_palette = {k_colors, COLORS};

// Lenient UTF-8 decoding (the protocol has validated the text already): invalid bytes decode as U+FFFD.
static uint32_t utf8_next(const uint8_t **s)
{
    const uint8_t *p = *s;
    const uint8_t b = p[0];
    int n = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
    uint32_t cp = n == 1 ? b : n == 2 ? (b & 0x1Fu) : n == 3 ? (b & 0x0Fu) : (b & 0x07u);
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            n = 0;
            break;
        }
        cp = cp << 6 | (p[i] & 0x3Fu);
    }
    if (n == 0) {
        *s = p + 1;
        return 0xFFFD;
    }
    *s = p + n;
    return cp;
}

// Glyph code for a code point, or -1 to drop it.
static int glyph_for(uint32_t cp)
{
    if (cp >= 0x20 && cp < 0x7F) {
        return (int)cp;
    }
    if (cp < 0x20 || (cp >= 0x7F && cp <= 0xA0)) {
        return ' '; // controls, delete, C1 controls, no-break space
    }
    if (cp >= 0xC0 && cp <= 0xFF) {
        return k_latin1[cp - 0xC0];
    }
    switch (cp) {
    case 0xA1:
        return '!';
    case 0xBF:
        return '?';
    case 0xAB:
    case 0x2039:
        return '<';
    case 0xBB:
    case 0x203A:
        return '>';
    case 0xB7:
    case 0x2022:
    case 0x2026:
        return '.';
    case 0xA9:
        return 'C';
    case 0xAE:
        return 'R';
    case 0xB4:
    case 0x2018:
    case 0x2019:
    case 0x201A:
    case 0x2032:
        return '\'';
    case 0x201C:
    case 0x201D:
    case 0x201E:
    case 0x2033:
        return '"';
    case 0x2010:
    case 0x2011:
    case 0x2012:
    case 0x2013:
    case 0x2014:
    case 0x2212:
        return '-';
    case 0x0152:
        return 'O';
    case 0x0153:
        return 'o';
    case 0x0160:
        return 'S';
    case 0x0161:
        return 's';
    case 0x0178:
        return 'Y';
    case 0x017D:
        return 'Z';
    case 0x017E:
        return 'z';
    case 0x20AC:
        return MARQUEE_GLYPH_EURO;
    case 0xB0:
        return MARQUEE_GLYPH_DEGREE;
    case 0x2665:
    case 0x2764:
    case 0x1F496:
    case 0x1F499:
    case 0x1F49A:
    case 0x1F49B:
    case 0x1F49C:
        return MARQUEE_GLYPH_HEART;
    case 0x200B:
    case 0x200C:
    case 0x200D:
    case 0x2060:
    case 0xFEFF:
        return -1; // zero-width
    default:
        break;
    }
    if (cp >= 0xFE00 && cp <= 0xFE0F) {
        return -1; // variation selectors ("emoji style")
    }
    return MARQUEE_GLYPH_UNKNOWN;
}

int marquee_glyphs(const char *utf8, uint8_t *out, int max)
{
    const uint8_t *p = (const uint8_t *)utf8;
    int n = 0;
    while (*p && n < max) {
        const int g = glyph_for(utf8_next(&p));
        if (g >= 0) {
            out[n++] = (uint8_t)g;
        }
    }
    return n;
}

// Lays the glyphs out as LED columns: each glyph without its empty side columns, one blank column between
// glyphs, a space three columns wide.
static void layout(marquee_t *m, const uint8_t *glyphs, int n)
{
    m->ncols = 0;
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            m->col[m->ncols++] = 0;
        }
        const uint8_t *f = k_font[glyphs[i] - FONT_FIRST];
        if (glyphs[i] == ' ') {
            m->col[m->ncols++] = 0;
            m->col[m->ncols++] = 0;
            continue;
        }
        int first = 0, last = 4;
        while (first < 4 && !f[first]) {
            first++;
        }
        while (last > first && !f[last]) {
            last--;
        }
        for (int c = first; c <= last; c++) {
            m->col[m->ncols++] = f[c];
        }
    }
}

static int pass_cols(const marquee_t *m)
{
    return MARQUEE_COLS + m->ncols;
}

#define INTRO_STEPS (MARQUEE_INTRO_MS / MARQUEE_COL_MS)

uint32_t marquee_duration_ms(const marquee_t *m)
{
    return MARQUEE_INTRO_MS + (uint32_t)(m->passes * pass_cols(m)) * MARQUEE_COL_MS;
}

static void draw_frame(uint8_t *px)
{
    for (int y = 0; y < MARQUEE_IMG_H; y++) {
        for (int x = 0; x < MARQUEE_IMG_W; x++) {
            const int ring = x < MARQUEE_IMG_W - 1 - x ? x : MARQUEE_IMG_W - 1 - x;
            const int ring_y = y < MARQUEE_IMG_H - 1 - y ? y : MARQUEE_IMG_H - 1 - y;
            const int r = ring < ring_y ? ring : ring_y;
            uint8_t c = GAP;
            if (r == 0 || r == 2) {
                c = OUTLINE;
            } else if (r == 1) {
                c = (y == 1 || x == 1) && x < MARQUEE_IMG_W - 2 && y < MARQUEE_IMG_H - 2 ? METAL : METAL_DARK;
            }
            px[y * MARQUEE_IMG_W + x] = c;
        }
    }
}

static void draw_led(uint8_t *px, int col, int row, bool on)
{
    uint8_t *p = px + (MARQUEE_BEZEL + row * MARQUEE_LED) * MARQUEE_IMG_W + MARQUEE_BEZEL + col * MARQUEE_LED;
    const uint8_t mid = on ? ON : OFF, edge = on ? ON_EDGE : OFF_EDGE;
    p[0] = edge, p[1] = mid, p[2] = edge;
    p += MARQUEE_IMG_W;
    p[0] = mid, p[1] = on ? ON_HOT : OFF, p[2] = mid;
    p += MARQUEE_IMG_W;
    p[0] = edge, p[1] = mid, p[2] = edge;
}

// Bits of LED column c (bit r = LED row r) at animation step.
static uint32_t column_at(const marquee_t *m, int step, int c)
{
    if (step < INTRO_STEPS) { // a three-column scanner bar sweeps across the dark panel
        const int head = step * (MARQUEE_COLS + 3) / (INTRO_STEPS - 1);
        return c <= head && c > head - 3 ? (1u << MARQUEE_ROWS) - 1 : 0;
    }
    const int q = (step - INTRO_STEPS) % pass_cols(m); // columns scrolled in this pass
    const int j = c - (MARQUEE_COLS - q);              // text column under LED column c
    return j >= 0 && j < m->ncols ? (uint32_t)m->col[j] << 1 : 0; // font rows 0..6 on LED rows 1..7
}

static void draw(marquee_t *m, int step)
{
    const int next = m->cur ^ 1;
    uint8_t *px = m->px[next];
    for (int c = 0; c < MARQUEE_COLS; c++) {
        const uint32_t bits = column_at(m, step, c);
        for (int r = 0; r < MARQUEE_ROWS; r++) {
            draw_led(px, c, r, ((bits >> r) & 1u) != 0);
        }
    }
    m->sprite.image = &m->image[next];
    m->cur = next;
}

bool marquee_init(marquee_t *m, gfx_scene_t *scene, int z)
{
    memset(m, 0, sizeof(*m));
    for (int i = 0; i < 2; i++) {
        draw_frame(m->px[i]);
        m->image[i] = (gfx_image_t){MARQUEE_IMG_W, MARQUEE_IMG_H, m->px[i]};
    }
    m->sprite = (gfx_sprite_t){.image = &m->image[0], .palette = &k_palette, .scale = MARQUEE_SCALE,
                               .flags = GFX_SPRITE_HIDDEN};
    m->shown_frame = -1;
    return gfx_scene_add(scene, &m->sprite, z);
}

void marquee_start(marquee_t *m, const char *utf8, uint32_t now)
{
    uint8_t glyphs[MARQUEE_TEXT_MAX];
    layout(m, glyphs, marquee_glyphs(utf8, glyphs, MARQUEE_TEXT_MAX));
    const uint32_t pass_ms = (uint32_t)pass_cols(m) * MARQUEE_COL_MS;
    m->passes = (int)((MARQUEE_MIN_SHOW_MS - MARQUEE_INTRO_MS + pass_ms - 1) / pass_ms);
    m->passes = m->passes < 1 ? 1 : m->passes > MARQUEE_MAX_PASSES ? MARQUEE_MAX_PASSES : m->passes;
    m->start = now;
    m->shown_frame = -1;
    m->active = true;
}

uint32_t marquee_apply(marquee_t *m, int x, int y, uint32_t now)
{
    const uint32_t t = now - m->start;
    if (!m->active || t >= marquee_duration_ms(m)) {
        m->active = false;
        m->sprite.flags |= GFX_SPRITE_HIDDEN;
        return UINT32_MAX;
    }
    m->sprite.x = x;
    m->sprite.y = y;
    m->sprite.flags = (uint8_t)(m->sprite.flags & ~GFX_SPRITE_HIDDEN);
    // Where the text is follows the time (one column per MARQUEE_COL_MS); the sign is redrawn once per frame.
    const int frame = (int)(t / MARQUEE_FRAME_MS);
    if (frame != m->shown_frame) {
        draw(m, (int)((uint32_t)frame * MARQUEE_FRAME_MS / MARQUEE_COL_MS));
        m->shown_frame = frame;
    }
    return (uint32_t)(frame + 1) * MARQUEE_FRAME_MS - t;
}

bool marquee_active(const marquee_t *m)
{
    return m->active;
}
