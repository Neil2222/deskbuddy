#include "office_skeleton.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "office_font.h"

#define SC OFFICE_SKELETON_SCALE
#define ESCAPE (OFFICE_SKELETON_GEARS - 1)

// The background: the clock's near-black (Buddy's switched-off face is this colour) and the shadows on it.
#define BG GFX_RGB(8, 9, 12)
#define BG_SHADOW GFX_RGB(4, 5, 7)

// The palette of the movement, the frame and the timeplate (indices: office_skeleton_assets.h).
static const uint8_t k_rgb[OFFICE_SKELETON_P_COUNT][3] = {
    {0, 0, 0},
    // brass: deep shadow to polished highlight
    {44, 26, 10}, {84, 54, 20}, {126, 86, 34}, {168, 120, 50}, {204, 156, 70}, {232, 192, 104}, {252, 232, 168},
    // bronze: darker, redder
    {34, 20, 14}, {62, 38, 24}, {94, 58, 34}, {128, 82, 46}, {162, 106, 60}, {196, 138, 84}, {230, 184, 132},
    // steel
    {30, 34, 42}, {64, 70, 84}, {108, 116, 132}, {156, 164, 180}, {214, 220, 232},
    // ruby
    {96, 8, 24}, {188, 28, 52}, {255, 128, 150},
    // blued steel
    {18, 28, 70}, {38, 70, 156}, {112, 152, 236},
    // cast shadow
    {3, 3, 5},
    // enamel: dark, base, light
    {12, 11, 15}, {24, 22, 30}, {44, 40, 52},
    // windows: dark, base, light
    {5, 5, 7}, {11, 11, 15}, {26, 25, 32},
};

// The numerals: ivory on the windows' colour.
static const uint8_t k_ivory[3] = {240, 230, 200};
static const uint8_t k_ink_shadow[3] = {3, 3, 4};
// The date: brass engraved into the plaque's enamel.
static const uint8_t k_date_ink[3] = {226, 184, 104};
static const office_font_style_t k_date = OFFICE_SKELETON_DATE_STYLE;
#define PAD 2

bool office_skeleton_unpack(const uint8_t *src, size_t src_size, uint8_t *dst, size_t size)
{
    size_t i = 0, o = 0;
    while (o < size) {
        if (i >= src_size) {
            return false;
        }
        const unsigned n = src[i++];
        if (n < 128) {
            if (i + n + 1 > src_size || o + n + 1 > size) {
                return false;
            }
            memcpy(dst + o, src + i, n + 1);
            i += n + 1;
            o += n + 1;
        } else {
            if (i >= src_size || o + n - 126 > size) {
                return false;
            }
            memset(dst + o, src[i++], n - 126);
            o += n - 126;
        }
    }
    return i == src_size;
}

// --- Background -------------------------------------------------------------------------------------------------

void office_skeleton_background(void *ctx, gfx_color_t *dst, int x, int y, int w)
{
    const office_skeleton_t *k = ctx;
    for (int i = 0; i < w; i++) {
        dst[i] = BG;
    }
    for (int g = 0; g < OFFICE_SKELETON_GEARS; g++) {
        const office_skeleton_shadow_t *s = &k->shadow[g];
        const float dy = y + 0.5f - s->y;
        if (fabsf(dy) >= s->r) {
            continue;
        }
        const float half = sqrtf(s->r * s->r - dy * dy);
        int x0 = (int)ceilf(s->x - half - 0.5f) - x, x1 = (int)floorf(s->x + half - 0.5f) - x;
        x0 = x0 < 0 ? 0 : x0;
        x1 = x1 >= w ? w - 1 : x1;
        for (int i = x0; i <= x1; i++) {
            dst[i] = BG_SHADOW;
        }
    }
    const gfx_rect_t p = k->plate_shadow;
    if (y >= p.y && y < p.y + p.h) {
        int x0 = p.x - x, x1 = p.x + p.w - x;
        x0 = x0 < 0 ? 0 : x0;
        x1 = x1 > w ? w : x1;
        for (int i = x0; i < x1; i++) {
            dst[i] = BG_SHADOW;
        }
    }
    // The frame: the runs of this art row that reach into [x, x + w).
    const int ry = y - k->oy;
    if (ry < 0 || ry >= k->frame_h * SC) {
        return;
    }
    const uint8_t *r = k->frame_runs + k->frame_row[ry / SC], *end = k->frame_runs + k->frame_row[ry / SC + 1];
    while (r < end) {
        const int col = r[0] | r[1] << 8, n = r[2] | r[3] << 8;
        const uint8_t *idx = r + 4;
        r += 4 + n;
        const int s0 = k->ox + col * SC, s1 = s0 + n * SC;
        if (s1 <= x) {
            continue;
        }
        if (s0 >= x + w) {
            break; // runs are in order
        }
        const int a = s0 > x ? s0 : x, b = s1 < x + w ? s1 : x + w;
        for (int sx = a; sx < b; sx++) {
            dst[sx - x] = k->colors[idx[(sx - s0) / SC]];
        }
    }
}

// --- Setup ----------------------------------------------------------------------------------------------------------

static gfx_color_t rgb(const uint8_t c[3])
{
    return GFX_RGB(c[0], c[1], c[2]);
}

// Ink levels 1..OFFICE_INK_LEVELS blend from `from` to `to`.
static void ramp(gfx_color_t *colors, const uint8_t from[3], const uint8_t to[3])
{
    colors[0] = 0;
    for (int i = 1; i <= OFFICE_INK_LEVELS; i++) {
        colors[i] = GFX_RGB(from[0] + (to[0] - from[0]) * i / OFFICE_INK_LEVELS,
                            from[1] + (to[1] - from[1]) * i / OFFICE_INK_LEVELS,
                            from[2] + (to[2] - from[2]) * i / OFFICE_INK_LEVELS);
    }
}

static void place(gfx_sprite_t *sp, const gfx_image_t *img, const gfx_palette_t *pal, int scale, int x, int y)
{
    *sp = (gfx_sprite_t){.image = img, .palette = pal, .x = x, .y = y, .scale = (uint8_t)scale,
                         .flags = GFX_SPRITE_HIDDEN};
}

// Unpacks image i into mem at *used; NULL if it does not fit or the data is corrupt.
static uint8_t *unpack_image(const office_skeleton_assets_t *as, int i, uint8_t *mem, size_t mem_size, size_t *used)
{
    const size_t size = (size_t)as->layout.w[i] * as->layout.h[i];
    if (*used + size > mem_size ||
        !office_skeleton_unpack(as->packed + as->offset[i], as->offset[i + 1] - as->offset[i], mem + *used, size)) {
        return NULL;
    }
    uint8_t *px = mem + *used;
    *used += size;
    return px;
}

size_t office_skeleton_frame_memory(void)
{
    const office_skeleton_frame_t *fr = &office_skeleton_assets.frame;
    return (size_t)(fr->h + 1) * sizeof(uint32_t) + fr->runs_size;
}

bool office_skeleton_init(office_skeleton_t *k, gfx_scene_t *scene, gfx_rect_t screen, uint8_t *mem,
                          size_t mem_size, uint8_t *frame_mem, size_t frame_size, office_skeleton_flush_fn flush,
                          int z)
{
    memset(k, 0, sizeof(*k));
    k->screen = screen;
    k->flush = flush;
    const office_skeleton_assets_t *as = &office_skeleton_assets;
    const office_skeleton_layout_t *l = &as->layout;
    if (screen.w < OFFICE_SKELETON_DESIGN_W || screen.h < OFFICE_SKELETON_DESIGN_H || !mem) {
        return false;
    }
    const int ox = screen.x + (screen.w - OFFICE_SKELETON_DESIGN_W) / 2;
    const int oy = screen.y + (screen.h - OFFICE_SKELETON_DESIGN_H) / 2;
    k->ox = ox;
    k->oy = oy;

    for (int i = 0; i < OFFICE_SKELETON_P_COUNT; i++) {
        k->colors[i] = rgb(k_rgb[i]);
    }
    k->colors[0] = 0;
    k->pal = (gfx_palette_t){k->colors, OFFICE_SKELETON_P_COUNT};
    ramp(k->ink, k_rgb[OFFICE_SKELETON_P_WINDOW + 1], k_ivory);
    k->ink[OFFICE_INK_LEVELS + 1] = rgb(k_ink_shadow);
    k->ink_pal = (gfx_palette_t){k->ink, OFFICE_INK_LEVELS + 2};
    ramp(k->date_ink, k_rgb[OFFICE_SKELETON_P_ENAMEL + 1], k_date_ink);
    k->date_pal = (gfx_palette_t){k->date_ink, OFFICE_INK_LEVELS + 1};

    // Wheels, anchor, cock, timeplate and numerals: unpacked once.
    size_t used = 0;
    for (int g = 0; g < OFFICE_SKELETON_GEARS; g++) {
        for (int f = 0; f < OFFICE_SKELETON_FRAMES; f++) {
            const int i = OFFICE_SKELETON_IMG_WHEEL + g * OFFICE_SKELETON_STEPS + f;
            uint8_t *px = unpack_image(as, i, mem, mem_size, &used);
            if (!px) {
                return false;
            }
            k->gear_img[g][f] = (gfx_image_t){l->w[i], l->h[i], px};
        }
        const int i0 = OFFICE_SKELETON_IMG_WHEEL + g * OFFICE_SKELETON_STEPS;
        place(&k->gear[g], &k->gear_img[g][0], &k->pal, SC, ox + l->x[i0], oy + l->y[i0]);
        k->dir[g] = l->dir[g];
        k->shadow[g] = (office_skeleton_shadow_t){ox + l->shadow[g][0], oy + l->shadow[g][1], l->shadow[g][2]};
    }
    const struct {
        int i;
        gfx_image_t *img;
        gfx_sprite_t *sp;
    } parts[] = {
        {OFFICE_SKELETON_IMG_ANCHOR, &k->anchor_img, &k->anchor},
        {OFFICE_SKELETON_IMG_COCK, &k->cock_img, &k->cock},
        {OFFICE_SKELETON_IMG_PLATE, &k->plate_img, &k->plate},
    };
    for (unsigned p = 0; p < sizeof(parts) / sizeof(parts[0]); p++) {
        const int i = parts[p].i;
        uint8_t *px = unpack_image(as, i, mem, mem_size, &used);
        if (!px) {
            return false;
        }
        *parts[p].img = (gfx_image_t){l->w[i], l->h[i], px};
        place(parts[p].sp, parts[p].img, &k->pal, SC, ox + l->x[i], oy + l->y[i]);
    }
    for (int d = 0; d < 11; d++) {
        const int i = OFFICE_SKELETON_IMG_GLYPH + d;
        uint8_t *px = unpack_image(as, i, mem, mem_size, &used);
        if (!px) {
            return false;
        }
        k->glyph[d] = (gfx_image_t){l->w[i], l->h[i], px};
    }
    k->cell_w = k->glyph[0].w;
    k->cell_h = k->glyph[0].h;
    const size_t cell = (size_t)k->cell_w * k->cell_h;
    for (int d = 0; d < 4; d++) {
        for (int b = 0; b < 2; b++) {
            if (used + cell > mem_size) {
                return false;
            }
            k->roll_px[d][b] = mem + used;
            used += cell;
        }
        place(&k->digit[d], &k->glyph[10], &k->ink_pal, SC, ox + l->digit_x[d], oy + l->digit_y);
    }
    k->date_w = l->date_w;
    k->date_h = l->date_h;
    const size_t date = (size_t)k->date_w * k->date_h;
    if (used + 2 * date > mem_size) {
        return false;
    }
    k->date_px[0] = mem + used;
    k->date_px[1] = mem + used + date;
    used += 2 * date;
    place(&k->date, NULL, &k->date_pal, 1, ox + l->date_x, oy + l->date_y);
    k->plate_shadow =
        (gfx_rect_t){ox + l->plate_shadow[0], oy + l->plate_shadow[1], l->plate_shadow[2], l->plate_shadow[3]};
    k->mem_used = used;
    if (flush) {
        flush(mem, used); // all of it is read from now on, never written: out of the cache at once
    }

    // The frame: its runs, in fast memory if there is some (they are read on every redraw).
    const office_skeleton_frame_t *fr = &as->frame;
    k->frame_h = fr->h;
    k->frame_row = fr->row;
    k->frame_runs = fr->runs;
    const size_t rows = (size_t)(fr->h + 1) * sizeof(uint32_t);
    if (frame_mem && rows + fr->runs_size <= frame_size) {
        memcpy(frame_mem, fr->row, rows);
        memcpy(frame_mem + rows, fr->runs, fr->runs_size);
        k->frame_row = (const uint32_t *)(const void *)frame_mem;
        k->frame_runs = frame_mem + rows;
        k->frame_copied = true;
    }

    bool ok = true;
    for (int g = 0; ok && g < OFFICE_SKELETON_GEARS; g++) {
        ok = gfx_scene_add(scene, &k->gear[g], z + 1);
    }
    ok = ok && gfx_scene_add(scene, &k->anchor, z + 1) && gfx_scene_add(scene, &k->cock, z + 2) &&
         gfx_scene_add(scene, &k->plate, z + 3);
    for (int d = 0; ok && d < 4; d++) {
        ok = gfx_scene_add(scene, &k->digit[d], z + 4);
    }
    return ok && gfx_scene_add(scene, &k->date, z + 4);
}

static void set_hidden(gfx_sprite_t *sp, bool hidden)
{
    sp->flags = (uint8_t)(hidden ? sp->flags | GFX_SPRITE_HIDDEN : sp->flags & ~GFX_SPRITE_HIDDEN);
}

void office_skeleton_show(office_skeleton_t *k, bool visible)
{
    k->visible = visible;
    for (int g = 0; g < OFFICE_SKELETON_GEARS; g++) {
        set_hidden(&k->gear[g], !visible);
    }
    set_hidden(&k->anchor, !visible);
    set_hidden(&k->cock, !visible);
    set_hidden(&k->plate, !visible);
    for (int d = 0; d < 4; d++) {
        set_hidden(&k->digit[d], !visible);
    }
    set_hidden(&k->date, !visible);
}

static void flushed(const office_skeleton_t *k, const void *px, size_t bytes)
{
    if (k->flush) {
        k->flush(px, bytes);
    }
}

// Renders the date into the buffer the screen is not showing, so the scene sees a new image pointer.
static void set_date(office_skeleton_t *k, const char *text)
{
    if (k->date.image && strcmp(text, k->date_text) == 0) {
        return;
    }
    snprintf(k->date_text, sizeof(k->date_text), "%s", text);
    const int next = k->date.shown.image == &k->date_img[0] ? 1 : 0;
    uint8_t *px = k->date_px[next];
    const size_t size = (size_t)k->date_w * k->date_h;
    memset(px, 0, size);
    const float w = office_font_text_width(text, &k_date);
    office_font_draw_text(text, &k_date, px, k->date_w, k->date_h, (k->date_w - w) / 2, PAD);
    flushed(k, px, size);
    k->date_img[next] = (gfx_image_t){(uint16_t)k->date_w, (uint16_t)k->date_h, px};
    k->date.image = &k->date_img[next];
}

// A digit window part way through a roll: `from` moving up and out, `to` coming in from below; `step` of 3.
static void set_roll(office_skeleton_t *k, int d, int from, int to, int step)
{
    const int next = k->digit[d].shown.image == &k->roll_img[d][0] ? 1 : 0;
    uint8_t *px = k->roll_px[d][next];
    const int cw = k->cell_w, ch = k->cell_h, shift = ch * step / 3;
    const uint8_t *a = k->glyph[from].px, *b = k->glyph[to].px;
    for (int y = 0; y < ch; y++) {
        const int sy = y + shift;
        memcpy(px + (size_t)y * cw, sy < ch ? a + (size_t)sy * cw : b + (size_t)(sy - ch) * cw, (size_t)cw);
    }
    flushed(k, px, (size_t)cw * ch);
    k->roll_img[d][next] = (gfx_image_t){(uint16_t)cw, (uint16_t)ch, px};
    k->digit[d].image = &k->roll_img[d][next];
}

static void set_frames(office_skeleton_t *k, uint32_t escape, uint32_t left, uint32_t right)
{
    for (int g = 0; g < OFFICE_SKELETON_GEARS; g++) {
        const uint32_t step = g == ESCAPE ? escape : g < 3 ? left : right;
        const uint32_t f = (k->dir[g] > 0 ? step : 0u - step) % OFFICE_SKELETON_FRAMES;
        k->gear[g].image = &k->gear_img[g][f];
    }
    k->anchor.flags = (uint8_t)(escape & 1 ? k->anchor.flags | GFX_SPRITE_FLIP_X
                                           : k->anchor.flags & ~GFX_SPRITE_FLIP_X);
}

uint32_t office_skeleton_apply(office_skeleton_t *k, const struct tm *tm, uint32_t ms)
{
    char text[OFFICE_DATE_MAX];
    office_clock_date_text(tm, text);
    set_date(k, text);
    if (!tm) { // stopped: nothing moves until a time source sets the clock
        for (int d = 0; d < 4; d++) {
            k->digit[d].image = &k->glyph[10];
        }
        set_frames(k, 0, 0, 0);
        return 1000;
    }
    ms %= 1000;
    const int h = tm->tm_hour < 0 || tm->tm_hour > 23 ? 0 : tm->tm_hour;
    const int m = tm->tm_min < 0 || tm->tm_min > 59 ? 0 : tm->tm_min;
    const int s = tm->tm_sec < 0 || tm->tm_sec > 59 ? 0 : tm->tm_sec;
    const uint32_t secs = (uint32_t)(h * 3600 + m * 60 + s);
    uint32_t next = 1000 - ms;

    // On the hour: a quick whir of the whole movement (a whole number of turns of the frames: no jump after it).
    uint32_t extra = 0;
    const uint32_t into_hour = (uint32_t)s * 1000 + ms;
    const uint32_t flourish_ms = OFFICE_SKELETON_FLOURISH_STEPS * OFFICE_SKELETON_FLOURISH_MS;
    if (m == 0 && into_hour < flourish_ms) {
        extra = into_hour / OFFICE_SKELETON_FLOURISH_MS + 1;
        const uint32_t to_next = OFFICE_SKELETON_FLOURISH_MS - into_hour % OFFICE_SKELETON_FLOURISH_MS;
        next = to_next < next ? to_next : next;
        if (extra >= OFFICE_SKELETON_FLOURISH_STEPS) {
            extra = OFFICE_SKELETON_FLOURISH_STEPS;
        }
    }
    // The right train steps 7 s past each quarter minute, so it never steps together with the left one.
    set_frames(k, secs + extra, secs / 5 + extra, (secs + 8) / 15 + extra);

    // The time; at a minute change the changed digits roll up.
    const int now_d[4] = {h / 10, h % 10, m / 10, m % 10};
    const bool rolling = s == 0 && ms < 2 * OFFICE_SKELETON_TICK_FRAME_MS;
    int prev_d[4];
    if (rolling) {
        const int pm = (h * 60 + m + 24 * 60 - 1) % (24 * 60);
        prev_d[0] = pm / 60 / 10;
        prev_d[1] = pm / 60 % 10;
        prev_d[2] = pm % 60 / 10;
        prev_d[3] = pm % 60 % 10;
        const uint32_t to_next = OFFICE_SKELETON_TICK_FRAME_MS - ms % OFFICE_SKELETON_TICK_FRAME_MS;
        next = to_next < next ? to_next : next;
    }
    for (int d = 0; d < 4; d++) {
        if (rolling && prev_d[d] != now_d[d]) {
            set_roll(k, d, prev_d[d], now_d[d], 1 + (int)(ms / OFFICE_SKELETON_TICK_FRAME_MS));
        } else {
            k->digit[d].image = &k->glyph[now_d[d]];
        }
    }
    return next;
}
