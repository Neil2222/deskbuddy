#include "office_font.h"

#include <math.h>
#include <stddef.h>

typedef enum { LINE, ARC, DOT } stroke_kind_t;

// LINE: (a,b)-(c,d). ARC: centre (a,b), radii (c,d), from angle e to f in degrees (0 = right, 90 = up), drawn
// in the direction from e to f. DOT: centre (a,b), radius c strokes.
typedef struct {
    stroke_kind_t kind;
    float a, b, c, d, e, f;
} stroke_t;

#define MAX_STROKES 6

typedef struct {
    char ch;
    float w; // width of the centre lines, in units
    uint8_t n;
    stroke_t s[MAX_STROKES];
} glyph_t;

#define L(x0, y0, x1, y1) {LINE, x0, y0, x1, y1, 0, 0}
#define A(cx, cy, rx, ry, a0, a1) {ARC, cx, cy, rx, ry, a0, a1}
#define D(cx, cy, r) {DOT, cx, cy, r, 0, 0, 0}

static const glyph_t k_glyphs[] = {
    {'0', 6, 1, {A(3, 5, 3, 5, 0, 360)}},
    {'1', 6, 2, {L(1.5f, 2, 3.5f, 0), L(3.5f, 0, 3.5f, 10)}},
    {'2', 6, 3, {A(3, 3, 3, 3, 160, -40), L(5.3f, 4.93f, 0, 10), L(0, 10, 6, 10)}},
    {'3', 6, 2, {A(3, 2.5f, 2.6f, 2.5f, 150, -90), A(3, 7.5f, 3, 2.5f, 90, -150)}},
    {'4', 6, 3, {L(4.5f, 10, 4.5f, 0), L(4.5f, 0, 0, 7), L(0, 7, 6, 7)}},
    {'5', 6, 3, {L(5.6f, 0, 1, 0), L(1, 0, 0.7f, 4.74f), A(3, 6.8f, 3, 3.2f, 140, -150)}},
    {'6', 6, 2, {A(3, 7, 3, 3, 0, 360), L(4.6f, 0, 0.3f, 5.8f)}},
    {'7', 6, 2, {L(0, 0, 6, 0), L(6, 0, 2, 10)}},
    {'8', 6, 2, {A(3, 2.5f, 2.6f, 2.5f, 0, 360), A(3, 7.5f, 3, 2.5f, 0, 360)}},
    {'9', 6, 2, {A(3, 3, 3, 3, 0, 360), L(5.82f, 4.03f, 1.6f, 10)}},
    {'-', 6, 1, {L(1.5f, 5, 4.5f, 5)}},
    {':', 0, 2, {D(0, 3, 0.75f), D(0, 7, 0.75f)}},
    {' ', 3, 0, {{LINE, 0, 0, 0, 0, 0, 0}}},
    {'A', 6, 3, {L(0, 10, 3, 0), L(3, 0, 6, 10), L(1.1f, 6.5f, 4.9f, 6.5f)}},
    {'B', 5.8f, 6, {L(0, 10, 0, 0), L(0, 0, 3, 0), A(3, 2.5f, 2.5f, 2.5f, 90, -90), L(0, 5, 3.2f, 5),
                    A(3.2f, 7.5f, 2.6f, 2.5f, 90, -90), L(3.2f, 10, 0, 10)}},
    {'C', 6, 1, {A(3.5f, 5, 3.5f, 5, 50, 310)}},
    {'D', 6, 4, {L(0, 0, 0, 10), L(0, 0, 1.5f, 0), A(1.5f, 5, 4.5f, 5, 90, -90), L(1.5f, 10, 0, 10)}},
    {'E', 5, 4, {L(5, 0, 0, 0), L(0, 0, 0, 10), L(0, 10, 5, 10), L(0, 5, 4, 5)}},
    {'F', 5, 3, {L(5, 0, 0, 0), L(0, 0, 0, 10), L(0, 5, 4, 5)}},
    {'G', 7, 2, {A(3.5f, 5, 3.5f, 5, 50, 360), L(7, 5, 4, 5)}},
    {'H', 6, 3, {L(0, 0, 0, 10), L(6, 0, 6, 10), L(0, 5, 6, 5)}},
    {'I', 0, 1, {L(0, 0, 0, 10)}},
    {'J', 4.5f, 2, {L(4.5f, 0, 4.5f, 7.5f), A(2.25f, 7.5f, 2.25f, 2.5f, 0, -180)}},
    {'K', 5.5f, 3, {L(0, 0, 0, 10), L(5.5f, 0, 0, 6.5f), L(1.9f, 4.4f, 5.5f, 10)}},
    {'L', 5, 2, {L(0, 0, 0, 10), L(0, 10, 5, 10)}},
    {'M', 7, 4, {L(0, 10, 0, 0), L(0, 0, 3.5f, 7), L(3.5f, 7, 7, 0), L(7, 0, 7, 10)}},
    {'N', 6, 3, {L(0, 10, 0, 0), L(0, 0, 6, 10), L(6, 10, 6, 0)}},
    {'O', 8, 1, {A(4, 5, 4, 5, 0, 360)}},
    {'P', 5.5f, 4, {L(0, 10, 0, 0), L(0, 0, 3, 0), A(3, 2.75f, 2.5f, 2.75f, 90, -90), L(3, 5.5f, 0, 5.5f)}},
    {'R', 5.5f, 5, {L(0, 10, 0, 0), L(0, 0, 3, 0), A(3, 2.75f, 2.5f, 2.75f, 90, -90), L(3, 5.5f, 0, 5.5f),
                    L(3, 5.5f, 5.5f, 10)}},
    {'S', 5.5f, 2, {A(2.75f, 2.5f, 2.6f, 2.5f, 30, 270), A(2.75f, 7.5f, 2.75f, 2.5f, 90, -150)}},
    {'T', 6, 2, {L(0, 0, 6, 0), L(3, 0, 3, 10)}},
    {'U', 6, 3, {L(0, 0, 0, 7), A(3, 7, 3, 3, 180, 360), L(6, 7, 6, 0)}},
    {'V', 6, 2, {L(0, 0, 3, 10), L(3, 10, 6, 0)}},
    {'W', 8, 4, {L(0, 0, 2, 10), L(2, 10, 4, 2), L(4, 2, 6, 10), L(6, 10, 8, 0)}},
    {'Y', 6, 3, {L(0, 0, 3, 5), L(6, 0, 3, 5), L(3, 5, 3, 10)}},
};

static const glyph_t *find(char c)
{
    for (size_t i = 0; i < sizeof(k_glyphs) / sizeof(k_glyphs[0]); i++) {
        if (k_glyphs[i].ch == c) {
            return &k_glyphs[i];
        }
    }
    return NULL;
}

bool office_font_has(char c)
{
    return find(c) != NULL;
}

float office_font_advance(char c, const office_font_style_t *st)
{
    const glyph_t *g = find(c);
    return g ? g->w * st->unit + st->stroke + st->tracking : 0;
}

float office_font_text_width(const char *s, const office_font_style_t *st)
{
    float w = 0;
    for (; *s; s++) {
        w += office_font_advance(*s, st);
    }
    return w > st->tracking ? w - st->tracking : 0;
}

typedef struct {
    uint8_t *buf;
    int w, h;
} target_t;

// Inks a capsule: all points within r of the segment p0-p1, with a one-pixel antialiased edge.
static void capsule(const target_t *t, float x0, float y0, float x1, float y1, float r)
{
    const int bx0 = (int)floorf(fminf(x0, x1) - r - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + r + 1);
    const int by0 = (int)floorf(fminf(y0, y1) - r - 1), by1 = (int)ceilf(fmaxf(y0, y1) + r + 1);
    const float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int py = by0 < 0 ? 0 : by0; py < by1 && py < t->h; py++) {
        for (int px = bx0 < 0 ? 0 : bx0; px < bx1 && px < t->w; px++) {
            const float cx = px + 0.5f - x0, cy = py + 0.5f - y0;
            float u = len2 > 0 ? (cx * dx + cy * dy) / len2 : 0;
            u = u < 0 ? 0 : u > 1 ? 1 : u;
            const float ex = cx - u * dx, ey = cy - u * dy;
            float cover = r + 0.5f - sqrtf(ex * ex + ey * ey);
            if (cover <= 0) {
                continue;
            }
            cover = cover > 1 ? 1 : cover;
            const uint8_t level = (uint8_t)(cover * OFFICE_INK_LEVELS + 0.5f);
            uint8_t *p = &t->buf[py * t->w + px];
            if (level > *p) {
                *p = level;
            }
        }
    }
}

void office_font_draw(char c, const office_font_style_t *st, uint8_t *buf, int w, int h, float x, float y)
{
    const glyph_t *g = find(c);
    if (!g) {
        return;
    }
    const target_t t = {buf, w, h};
    const float u = st->unit, r = st->stroke / 2;
    const float ox = x + r, oy = y + r; // grid origin: the stroke's outer edge sits at (x, y)
    for (int i = 0; i < g->n; i++) {
        const stroke_t *s = &g->s[i];
        switch (s->kind) {
        case LINE:
            capsule(&t, ox + s->a * u, oy + s->b * u, ox + s->c * u, oy + s->d * u, r);
            break;
        case DOT:
            capsule(&t, ox + s->a * u, oy + s->b * u, ox + s->a * u, oy + s->b * u, r * s->c * 2);
            break;
        case ARC: {
            const float span = s->f - s->e;
            int n = (int)(fabsf(span) / 8);
            n = n < 8 ? 8 : n;
            float px = 0, py = 0;
            for (int k = 0; k <= n; k++) {
                const float a = (s->e + span * k / n) * 3.14159265f / 180;
                const float qx = ox + (s->a + s->c * cosf(a)) * u, qy = oy + (s->b - s->d * sinf(a)) * u;
                if (k > 0) {
                    capsule(&t, px, py, qx, qy, r);
                }
                px = qx;
                py = qy;
            }
            break;
        }
        }
    }
}

float office_font_draw_text(const char *s, const office_font_style_t *st, uint8_t *buf, int w, int h, float x,
                            float y)
{
    for (; *s; s++) {
        office_font_draw(*s, st, buf, w, h, x, y);
        x += office_font_advance(*s, st);
    }
    return x;
}
