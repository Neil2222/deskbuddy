#include "usage_view.h"

#include <stdio.h>
#include <string.h>

#include "marquee.h"

enum { INK_PLATE = 1, INK_EDGE, INK_BAR_BG, INK_GREEN, INK_YELLOW, INK_ORANGE, INK_RED, INK_TEXT, INK_DIM, INK_COUNT };

// Not const: palettes in internal RAM (read-only data is mapped from PSRAM on the board).
static gfx_color_t s_colors[INK_COUNT] = {
    0,
    GFX_RGB(20, 23, 36),    // plate (the panel's background)
    GFX_RGB(64, 74, 108),   // edge, divider (the panel's divider)
    GFX_RGB(0x33, 0x33, 0x33), // the bar's empty part
    GFX_RGB(0x2e, 0xcc, 0x40), // below 50 %
    GFX_RGB(0xf1, 0xc4, 0x0f), // below 75 %
    GFX_RGB(0xe6, 0x7e, 0x22), // below 90 %
    GFX_RGB(0xe7, 0x4c, 0x3c), // from 90 %
    GFX_RGB(214, 218, 226), // the pct now (the clock's ink)
    GFX_RGB(128, 134, 148), // labels, countdowns
};
static gfx_palette_t s_palette = {s_colors, INK_COUNT};

// Image rows: the text's 7 between an edge and a row of padding above and below; the bar in the middle 5.
#define TEXT_Y 2
#define BAR_Y 3
#define BAR_H 5
#define BAR_W 24
#define PAD_X 5         // plate edge to the first and last text
#define GAP 4           // between the label, the bar, the pct and the prediction
#define GAP_TIME 7      // before the countdown
#define GAP_SEGMENT 6   // either side of the divider between the windows

static uint8_t level_ink(int pct)
{
    static const uint8_t k_ink[] = {INK_GREEN, INK_YELLOW, INK_ORANGE, INK_RED};
    return k_ink[usage_level(pct)];
}

static void put(const usage_view_t *v, uint8_t *px, int x, int y, uint8_t ink)
{
    if (x >= 0 && x < v->img_w) {
        px[y * v->img_w + x] = ink;
    }
}

static void fill(const usage_view_t *v, uint8_t *px, int x, int y, int w, int h, uint8_t ink)
{
    for (int r = y; r < y + h; r++) {
        for (int c = x; c < x + w; c++) {
            put(v, px, c, r, ink);
        }
    }
}

// Text from x (drawn only if px): returns where it ends.
static int text(const usage_view_t *v, uint8_t *px, int x, const char *s, uint8_t ink)
{
    uint8_t cols[64];
    const int n = marquee_text_columns(s, cols, (int)sizeof(cols));
    for (int i = 0; px && i < n; i++) {
        for (int r = 0; r < 7; r++) {
            if ((cols[i] >> r) & 1) {
                put(v, px, x + i, TEXT_Y + r, ink);
            }
        }
    }
    return x + n;
}

// One window's segment from x (drawn only if px): returns where it ends.
static int segment(const usage_view_t *v, uint8_t *px, int x, int w, const link_usage_t *win, const char *countdown)
{
    x = text(v, px, x, w == LINK_USAGE_FIVE ? "5U" : "7D", INK_DIM) + GAP;
    if (px) {
        const int pct = win->pct < 100 ? win->pct : 100;
        fill(v, px, x, BAR_Y, BAR_W, BAR_H, INK_BAR_BG);
        fill(v, px, x, BAR_Y, (pct * BAR_W + 50) / 100, BAR_H, level_ink(win->pct));
    }
    x += BAR_W + GAP;
    char s[16];
    snprintf(s, sizeof(s), "%u%%", (unsigned)win->pct);
    x = text(v, px, x, s, INK_TEXT);
    if (win->pred >= 0) { // the font has no arrow
        snprintf(s, sizeof(s), "> %d%%", win->pred);
        x = text(v, px, x + GAP, s, level_ink(win->pred));
    }
    return countdown[0] ? text(v, px, x + GAP_TIME, countdown, INK_DIM) : x;
}

// Every segment from x (drawn only if px): returns where the last one ends.
static int segments(const usage_view_t *v, uint8_t *px, int x, const usage_t *u,
                    char countdown[LINK_USAGE_WINDOWS][USAGE_COUNTDOWN_MAX])
{
    bool first = true;
    for (int w = 0; w < LINK_USAGE_WINDOWS; w++) {
        if (!u->win[w].has) {
            continue;
        }
        if (!first) {
            if (px) {
                fill(v, px, x + GAP_SEGMENT, TEXT_Y, 1, 7, INK_EDGE);
            }
            x += 2 * GAP_SEGMENT + 1;
        }
        x = segment(v, px, x, w, &u->win[w], countdown[w]);
        first = false;
    }
    return x;
}

static void draw(usage_view_t *v, const usage_t *u, char countdown[LINK_USAGE_WINDOWS][USAGE_COUNTDOWN_MAX])
{
    const int next = v->cur ^ 1;
    uint8_t *px = v->px[next];
    memset(px, 0, (size_t)v->img_w * USAGE_VIEW_IMG_H);
    int w = segments(v, NULL, 0, u, countdown) + 2 * PAD_X;
    w = w < v->img_w ? w : v->img_w;
    const int x0 = (v->img_w - w) / 2;
    // The plate, its corners cut.
    fill(v, px, x0 + 1, 0, w - 2, USAGE_VIEW_IMG_H, INK_EDGE);
    fill(v, px, x0, 1, w, USAGE_VIEW_IMG_H - 2, INK_EDGE);
    fill(v, px, x0 + 1, 1, w - 2, USAGE_VIEW_IMG_H - 2, INK_PLATE);
    segments(v, px, x0 + PAD_X, u, countdown);
    v->plate_x = x0;
    v->plate_w = w;
    v->sprite.image = &v->image[next];
    v->cur = next;
}

gfx_rect_t usage_view_area(gfx_rect_t stage)
{
    int w = USAGE_VIEW_IMG_W * USAGE_VIEW_SCALE;
    w = w < stage.w ? w : stage.w / USAGE_VIEW_SCALE * USAGE_VIEW_SCALE;
    return (gfx_rect_t){stage.x + (stage.w - w) / 2, stage.y + stage.h - USAGE_VIEW_H - USAGE_VIEW_BOTTOM, w,
                        USAGE_VIEW_H};
}

bool usage_view_init(usage_view_t *v, gfx_scene_t *scene, gfx_rect_t stage, uint8_t *mem, int z)
{
    memset(v, 0, sizeof(*v));
    const gfx_rect_t area = usage_view_area(stage);
    if (!mem || area.w <= 0 || area.y < stage.y) {
        return false;
    }
    v->img_w = area.w / USAGE_VIEW_SCALE;
    for (int i = 0; i < 2; i++) {
        v->px[i] = mem + (size_t)i * USAGE_VIEW_IMG_W * USAGE_VIEW_IMG_H;
        memset(v->px[i], 0, (size_t)v->img_w * USAGE_VIEW_IMG_H);
        v->image[i] = (gfx_image_t){(uint16_t)v->img_w, USAGE_VIEW_IMG_H, v->px[i]};
    }
    v->sprite = (gfx_sprite_t){.image = &v->image[0], .palette = &s_palette, .x = area.x, .y = area.y,
                               .scale = USAGE_VIEW_SCALE, .flags = GFX_SPRITE_HIDDEN};
    return gfx_scene_add(scene, &v->sprite, z);
}

uint32_t usage_view_apply(usage_view_t *v, const usage_t *u, int64_t utc_ms)
{
    if (!u || (!u->win[LINK_USAGE_FIVE].has && !u->win[LINK_USAGE_WEEK].has)) {
        v->sprite.flags |= GFX_SPRITE_HIDDEN;
        return UINT32_MAX;
    }
    char countdown[LINK_USAGE_WINDOWS][USAGE_COUNTDOWN_MAX] = {{0}};
    char key[USAGE_VIEW_KEY_MAX];
    size_t n = 0;
    uint32_t next = UINT32_MAX;
    for (int w = 0; w < LINK_USAGE_WINDOWS; w++) {
        const link_usage_t *win = &u->win[w];
        if (!win->has) {
            continue;
        }
        if (utc_ms >= 0) {
            const uint32_t t = usage_countdown(countdown[w], USAGE_COUNTDOWN_MAX, utc_ms, win->end);
            next = t < next ? t : next;
        }
        const int k = snprintf(key + n, sizeof(key) - n, "%d:%u:%d:%s;", w, (unsigned)win->pct, win->pred,
                               countdown[w]);
        n = k > 0 && n + (size_t)k < sizeof(key) ? n + (size_t)k : n;
    }
    if (strcmp(key, v->shown) != 0) {
        draw(v, u, countdown);
        memcpy(v->shown, key, n + 1);
    }
    v->sprite.flags = (uint8_t)(v->sprite.flags & ~GFX_SPRITE_HIDDEN);
    return next;
}
