#include "strip.h"

#include <string.h>

#include "marquee.h"
#include "usage.h"
#include "usage_view.h"

// Not const: palettes in internal RAM (read-only data is mapped from PSRAM on the board).
static gfx_color_t s_colors[STRIP_INK_COUNT] = {
    0,
    GFX_RGB(20, 23, 36),    // plate (the panel's background)
    GFX_RGB(64, 74, 108),   // edge, divider (the panel's divider)
    GFX_RGB(0x33, 0x33, 0x33), // the bar's empty part
    GFX_RGB(0x2e, 0xcc, 0x40), // below 50 %
    GFX_RGB(0xf1, 0xc4, 0x0f), // below 75 %
    GFX_RGB(0xe6, 0x7e, 0x22), // below 90 %
    GFX_RGB(0xe7, 0x4c, 0x3c), // from 90 %
    GFX_RGB(214, 218, 226), // values (the clock's ink)
    GFX_RGB(128, 134, 148), // labels, countdowns
};
gfx_palette_t strip_palette = {s_colors, STRIP_INK_COUNT};

// Image rows: the text's 7 between an edge and a row of padding above and below; the bar in the middle 5.
#define TEXT_Y 2
#define BAR_Y 3
#define BAR_H 5
#define BAR_W 24

uint8_t strip_level_ink(int pct)
{
    static const uint8_t k_ink[] = {STRIP_GREEN, STRIP_YELLOW, STRIP_ORANGE, STRIP_RED};
    return k_ink[usage_level(pct)];
}

static void put(uint8_t *px, int img_w, int x, int y, uint8_t ink)
{
    if (x >= 0 && x < img_w) {
        px[y * img_w + x] = ink;
    }
}

void strip_fill(uint8_t *px, int img_w, int x, int y, int w, int h, uint8_t ink)
{
    for (int r = y; px && r < y + h; r++) {
        for (int c = x; c < x + w; c++) {
            put(px, img_w, c, r, ink);
        }
    }
}

int strip_text(uint8_t *px, int img_w, int x, const char *s, uint8_t ink)
{
    uint8_t cols[64];
    const int n = marquee_text_columns(s, cols, (int)sizeof(cols));
    for (int i = 0; px && i < n; i++) {
        for (int r = 0; r < 7; r++) {
            if ((cols[i] >> r) & 1) {
                put(px, img_w, x + i, TEXT_Y + r, ink);
            }
        }
    }
    return x + n;
}

int strip_bar(uint8_t *px, int img_w, int x, int pct)
{
    const int full = pct < 100 ? pct : 100;
    strip_fill(px, img_w, x, BAR_Y, BAR_W, BAR_H, STRIP_BAR_BG);
    strip_fill(px, img_w, x, BAR_Y, (full * BAR_W + 50) / 100, BAR_H, strip_level_ink(pct));
    return x + BAR_W;
}

int strip_divider(uint8_t *px, int img_w, int x)
{
    strip_fill(px, img_w, x + STRIP_GAP_SEGMENT, TEXT_Y, 1, 7, STRIP_EDGE);
    return x + 2 * STRIP_GAP_SEGMENT + 1;
}

int strip_plate(uint8_t *px, int img_w, int content_w, int *plate_x, int *plate_w)
{
    memset(px, 0, (size_t)img_w * USAGE_VIEW_IMG_H);
    int w = content_w + 2 * STRIP_PAD_X;
    w = w < img_w ? w : img_w;
    const int x0 = (img_w - w) / 2;
    // The plate, its corners cut.
    strip_fill(px, img_w, x0 + 1, 0, w - 2, USAGE_VIEW_IMG_H, STRIP_EDGE);
    strip_fill(px, img_w, x0, 1, w, USAGE_VIEW_IMG_H - 2, STRIP_EDGE);
    strip_fill(px, img_w, x0 + 1, 1, w - 2, USAGE_VIEW_IMG_H - 2, STRIP_PLATE);
    *plate_x = x0;
    *plate_w = w;
    return x0 + STRIP_PAD_X;
}
