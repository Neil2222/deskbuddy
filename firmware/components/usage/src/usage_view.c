#include "usage_view.h"

#include <stdio.h>
#include <string.h>

#include "strip.h"

#define GAP_TIME 7 // before the countdown

// One window's segment from x (drawn only if px): returns where it ends.
static int segment(const usage_view_t *v, uint8_t *px, int x, int w, const link_usage_t *win, const char *countdown)
{
    x = strip_text(px, v->img_w, x, w == LINK_USAGE_FIVE ? "5U" : "7D", STRIP_DIM) + STRIP_GAP;
    x = strip_bar(px, v->img_w, x, win->pct) + STRIP_GAP;
    char s[16];
    snprintf(s, sizeof(s), "%u%%", (unsigned)win->pct);
    x = strip_text(px, v->img_w, x, s, STRIP_TEXT);
    if (win->pred >= 0) { // the font has no arrow
        snprintf(s, sizeof(s), "> %d%%", win->pred);
        x = strip_text(px, v->img_w, x + STRIP_GAP, s, strip_level_ink(win->pred));
    }
    return countdown[0] ? strip_text(px, v->img_w, x + GAP_TIME, countdown, STRIP_DIM) : x;
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
            x = strip_divider(px, v->img_w, x);
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
    const int x = strip_plate(px, v->img_w, segments(v, NULL, 0, u, countdown), &v->plate_x, &v->plate_w);
    segments(v, px, x, u, countdown);
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
    v->sprite = (gfx_sprite_t){.image = &v->image[0], .palette = &strip_palette, .x = area.x, .y = area.y,
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
