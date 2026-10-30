#include "sysinfo_view.h"

#include <stdio.h>
#include <string.h>

#include "strip.h"

// One stat's segment from x (drawn only if px): returns where it ends.
static int segment(const sysinfo_view_t *v, uint8_t *px, int x, const char *label, int pct, const char *extra)
{
    x = strip_text(px, v->img_w, x, label, STRIP_DIM) + STRIP_GAP;
    x = strip_bar(px, v->img_w, x, pct) + STRIP_GAP;
    char s[16];
    snprintf(s, sizeof(s), "%d%%", pct);
    x = strip_text(px, v->img_w, x, s, STRIP_TEXT);
    return extra ? strip_text(px, v->img_w, x + STRIP_GAP + 2, extra, STRIP_DIM) : x;
}

// Every segment from x (drawn only if px): returns where the last one ends.
static int segments(const sysinfo_view_t *v, uint8_t *px, int x, const link_sys_t *sys, const char *free_text)
{
    const struct {
        const char *label;
        int pct;
        const char *extra;
    } k_stats[] = {{"CPU", sys->cpu, NULL}, {"RAM", sys->ram, NULL}, {"C:", sys->disk, free_text[0] ? free_text : NULL}};
    bool first = true;
    for (unsigned i = 0; i < sizeof(k_stats) / sizeof(k_stats[0]); i++) {
        if (k_stats[i].pct < 0) {
            continue;
        }
        if (!first) {
            x = strip_divider(px, v->img_w, x);
        }
        x = segment(v, px, x, k_stats[i].label, k_stats[i].pct, k_stats[i].extra);
        first = false;
    }
    return x;
}

// What is free on the disk: "120 GB vrij", from 1000 GB on "1.8 TB vrij"; "" when not known.
static void free_text(char *buf, size_t size, int32_t gb)
{
    if (gb < 0) {
        buf[0] = '\0';
    } else if (gb < 1000) {
        snprintf(buf, size, "%d GB vrij", (int)gb);
    } else {
        const int tenths = (int)(((int64_t)gb * 10 + 512) / 1024);
        snprintf(buf, size, "%d.%d TB vrij", tenths / 10, tenths % 10);
    }
}

gfx_rect_t sysinfo_view_area(gfx_rect_t stage, bool raised)
{
    gfx_rect_t a = usage_view_area(stage);
    if (raised) {
        a.y -= USAGE_VIEW_H + SYSINFO_VIEW_GAP;
    }
    return a;
}

bool sysinfo_view_init(sysinfo_view_t *v, gfx_scene_t *scene, gfx_rect_t stage, uint8_t *mem, int z)
{
    memset(v, 0, sizeof(*v));
    const gfx_rect_t area = sysinfo_view_area(stage, false), above = sysinfo_view_area(stage, true);
    if (!mem || area.w <= 0 || above.y < stage.y) {
        return false;
    }
    v->img_w = area.w / USAGE_VIEW_SCALE;
    v->y[0] = area.y;
    v->y[1] = above.y;
    for (int i = 0; i < 2; i++) {
        v->px[i] = mem + (size_t)i * USAGE_VIEW_IMG_W * USAGE_VIEW_IMG_H;
        memset(v->px[i], 0, (size_t)v->img_w * USAGE_VIEW_IMG_H);
        v->image[i] = (gfx_image_t){(uint16_t)v->img_w, USAGE_VIEW_IMG_H, v->px[i]};
    }
    v->sprite = (gfx_sprite_t){.image = &v->image[0], .palette = &strip_palette, .x = area.x, .y = area.y,
                               .scale = USAGE_VIEW_SCALE, .flags = GFX_SPRITE_HIDDEN};
    return gfx_scene_add(scene, &v->sprite, z);
}

void sysinfo_view_apply(sysinfo_view_t *v, const sysinfo_t *s, bool raised)
{
    if (!s || !sysinfo_has_stats(&s->sys)) {
        v->sprite.flags |= GFX_SPRITE_HIDDEN;
        return;
    }
    char free_s[24]; // up to "2097151.9 TB vrij" for any int32 GB count
    free_text(free_s, sizeof(free_s), s->sys.disk >= 0 ? s->sys.disk_free : -1);
    char key[SYSINFO_VIEW_KEY_MAX];
    snprintf(key, sizeof(key), "%d:%d:%d:%s", s->sys.cpu, s->sys.ram, s->sys.disk, free_s);
    if (strcmp(key, v->shown) != 0) {
        const int next = v->cur ^ 1;
        uint8_t *px = v->px[next];
        const int x = strip_plate(px, v->img_w, segments(v, NULL, 0, &s->sys, free_s), &v->plate_x, &v->plate_w);
        segments(v, px, x, &s->sys, free_s);
        v->sprite.image = &v->image[next];
        v->cur = next;
        snprintf(v->shown, sizeof(v->shown), "%s", key);
    }
    v->sprite.y = v->y[raised];
    v->sprite.flags = (uint8_t)(v->sprite.flags & ~GFX_SPRITE_HIDDEN);
}
