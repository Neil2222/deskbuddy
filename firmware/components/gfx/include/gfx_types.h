// Basic graphics types. Hardware independent: compiles for the ESP32 and for host-side tests.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// RGB565, as stored in the frame buffer.
typedef uint16_t gfx_color_t;

#define GFX_RGB(r, g, b) \
    ((gfx_color_t)((((unsigned)(r) & 0xF8u) << 8) | (((unsigned)(g) & 0xFCu) << 3) | ((unsigned)(b) >> 3)))

// Half-open rectangle: covers x..x+w-1, y..y+h-1. Empty when w <= 0 or h <= 0.
typedef struct {
    int x, y, w, h;
} gfx_rect_t;

// A block of pixels. stride is in pixels.
// flush (optional): called with each row segment right after the renderer has written it, so a cached frame
// buffer can write it out to memory at once instead of leaving it to whoever evicts those cache lines later.
typedef struct {
    gfx_color_t *px;
    int w, h, stride;
    void (*flush)(const gfx_color_t *px, int count);
} gfx_surface_t;

static inline bool gfx_rect_is_empty(gfx_rect_t r)
{
    return r.w <= 0 || r.h <= 0;
}

static inline int gfx_rect_area(gfx_rect_t r)
{
    return gfx_rect_is_empty(r) ? 0 : r.w * r.h;
}

static inline bool gfx_rect_equal(gfx_rect_t a, gfx_rect_t b)
{
    if (gfx_rect_is_empty(a) || gfx_rect_is_empty(b)) {
        return gfx_rect_is_empty(a) && gfx_rect_is_empty(b);
    }
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static inline bool gfx_rect_contains_point(gfx_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

// True when every pixel of inner lies in outer. An empty inner is contained in anything.
static inline bool gfx_rect_contains_rect(gfx_rect_t outer, gfx_rect_t inner)
{
    if (gfx_rect_is_empty(inner)) {
        return true;
    }
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}

static inline gfx_rect_t gfx_rect_intersect(gfx_rect_t a, gfx_rect_t b)
{
    const int x0 = a.x > b.x ? a.x : b.x;
    const int y0 = a.y > b.y ? a.y : b.y;
    const int x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    const int y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    if (x1 <= x0 || y1 <= y0) {
        return (gfx_rect_t){0, 0, 0, 0};
    }
    return (gfx_rect_t){x0, y0, x1 - x0, y1 - y0};
}

static inline bool gfx_rect_overlaps(gfx_rect_t a, gfx_rect_t b)
{
    return !gfx_rect_is_empty(gfx_rect_intersect(a, b));
}

// Bounding box of both. Empty inputs are ignored.
static inline gfx_rect_t gfx_rect_union(gfx_rect_t a, gfx_rect_t b)
{
    if (gfx_rect_is_empty(a)) {
        return gfx_rect_is_empty(b) ? (gfx_rect_t){0, 0, 0, 0} : b;
    }
    if (gfx_rect_is_empty(b)) {
        return a;
    }
    const int x0 = a.x < b.x ? a.x : b.x;
    const int y0 = a.y < b.y ? a.y : b.y;
    const int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    const int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
    return (gfx_rect_t){x0, y0, x1 - x0, y1 - y0};
}

#ifdef __cplusplus
}
#endif
