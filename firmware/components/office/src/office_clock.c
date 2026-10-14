#include "office_clock.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Colours: the background, the numerals, the resting colon, the date.
#define BG_R 8
#define BG_G 9
#define BG_B 12
static const uint8_t k_ink[3] = {214, 218, 226};
static const uint8_t k_dim[3] = {62, 66, 76};
static const uint8_t k_quiet[3] = {118, 124, 136};

// Numerals are rasterized at half size and shown at scale 2: about 170 px tall on screen.
#define NUM_SCALE 2
static const office_font_style_t k_num = {.unit = 8.0f, .stroke = 4.4f, .tracking = 0};
#define PAD 2
#define COLON_W 22

static const office_font_style_t k_date = {.unit = 2.0f, .stroke = 1.8f, .tracking = 9.0f};
#define DATE_W 480
#define DATE_GAP 56 // screen pixels between the numerals and the date

#define VALID_FROM 1767225600 // 2026-01-01T00:00:00Z

static const char k_cells[11] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-'};

void office_background(void *ctx, gfx_color_t *dst, int x, int y, int w)
{
    (void)ctx;
    (void)x;
    (void)y;
    const gfx_color_t c = GFX_RGB(BG_R, BG_G, BG_B);
    for (int i = 0; i < w; i++) {
        dst[i] = c;
    }
}

bool office_time_valid(time_t t)
{
    return t >= (time_t)VALID_FROM;
}

// Coverage level i of OFFICE_INK_LEVELS blends from the background to the ink colour.
static void ramp(gfx_color_t *colors, gfx_palette_t *pal, const uint8_t ink[3])
{
    colors[0] = 0;
    for (int i = 1; i <= OFFICE_INK_LEVELS; i++) {
        colors[i] = GFX_RGB(BG_R + (ink[0] - BG_R) * i / OFFICE_INK_LEVELS,
                            BG_G + (ink[1] - BG_G) * i / OFFICE_INK_LEVELS,
                            BG_B + (ink[2] - BG_B) * i / OFFICE_INK_LEVELS);
    }
    *pal = (gfx_palette_t){colors, OFFICE_INK_LEVELS + 1};
}

static void sprite(gfx_sprite_t *sp, const gfx_image_t *img, const gfx_palette_t *pal, int scale)
{
    *sp = (gfx_sprite_t){.image = img, .palette = pal, .scale = (uint8_t)scale, .flags = GFX_SPRITE_HIDDEN};
}

bool office_clock_init(office_clock_t *c, gfx_scene_t *scene, gfx_rect_t screen, uint8_t *mem, size_t mem_size,
                       int z)
{
    memset(c, 0, sizeof(*c));
    c->screen = screen;
    ramp(c->ink, &c->ink_pal, k_ink);
    ramp(c->dim, &c->dim_pal, k_dim);
    ramp(c->quiet, &c->quiet_pal, k_quiet);

    const float glyph_w = 6 * k_num.unit + k_num.stroke;
    const int cell_w = (int)ceilf(glyph_w) + 2 * PAD, cell_h = (int)ceilf(10 * k_num.unit + k_num.stroke) + 2 * PAD;
    c->date_w = DATE_W;
    c->date_h = (int)ceilf(10 * k_date.unit + k_date.stroke) + 2 * PAD;
    const size_t cell = (size_t)cell_w * cell_h, colon = (size_t)COLON_W * cell_h;
    const size_t date = (size_t)c->date_w * c->date_h;
    if (!mem || 11 * cell + colon + 2 * date > mem_size) {
        return false;
    }
    memset(mem, 0, 11 * cell + colon + 2 * date);

    // Tabular numerals: every cell has the same width, centred glyphs, so the time never shifts sideways.
    for (int i = 0; i < 11; i++) {
        uint8_t *px = mem + i * cell;
        office_font_draw(k_cells[i], &k_num, px, cell_w, cell_h, (cell_w - glyph_w) / 2, PAD);
        c->glyph[i] = (gfx_image_t){(uint16_t)cell_w, (uint16_t)cell_h, px};
    }
    uint8_t *colon_px = mem + 11 * cell;
    office_font_draw(':', &k_num, colon_px, COLON_W, cell_h, COLON_W / 2.0f - k_num.stroke / 2, PAD);
    c->colon_img = (gfx_image_t){COLON_W, (uint16_t)cell_h, colon_px};
    c->date_px[0] = colon_px + colon;
    c->date_px[1] = c->date_px[0] + date;

    // Layout: HH:MM centred, a little above the middle of the screen; the date centred underneath.
    const int time_w = (4 * cell_w + COLON_W) * NUM_SCALE, time_h = cell_h * NUM_SCALE;
    int x = screen.x + (screen.w - time_w) / 2;
    const int y = screen.y + screen.h * 42 / 100 - time_h / 2;
    for (int i = 0; i < 4; i++) {
        sprite(&c->digit[i], &c->glyph[10], &c->ink_pal, NUM_SCALE);
        c->digit[i].x = x;
        c->digit[i].y = y;
        x += cell_w * NUM_SCALE;
        if (i == 1) {
            sprite(&c->colon, &c->colon_img, &c->dim_pal, NUM_SCALE);
            c->colon.x = x;
            c->colon.y = y;
            x += COLON_W * NUM_SCALE;
        }
    }
    sprite(&c->date, NULL, &c->quiet_pal, 1);
    c->date.x = screen.x + (screen.w - c->date_w) / 2;
    c->date.y = y + time_h + DATE_GAP;

    bool ok = true;
    for (int i = 0; ok && i < 4; i++) {
        ok = gfx_scene_add(scene, &c->digit[i], z);
    }
    return ok && gfx_scene_add(scene, &c->colon, z) && gfx_scene_add(scene, &c->date, z);
}

static void set_hidden(gfx_sprite_t *sp, bool hidden)
{
    sp->flags = (uint8_t)(hidden ? sp->flags | GFX_SPRITE_HIDDEN : sp->flags & ~GFX_SPRITE_HIDDEN);
}

void office_clock_show(office_clock_t *c, bool visible)
{
    c->visible = visible;
    for (int i = 0; i < 4; i++) {
        set_hidden(&c->digit[i], !visible);
    }
    set_hidden(&c->colon, !visible);
    set_hidden(&c->date, !visible);
}

void office_clock_date_text(const struct tm *tm, char out[OFFICE_DATE_MAX])
{
    static const char *const k_days[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *const k_months[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                             "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (!tm || tm->tm_wday < 0 || tm->tm_wday > 6 || tm->tm_mon < 0 || tm->tm_mon > 11) {
        snprintf(out, OFFICE_DATE_MAX, "CLOCK NOT SET");
        return;
    }
    snprintf(out, OFFICE_DATE_MAX, "%s %d %s %d", k_days[tm->tm_wday], tm->tm_mday, k_months[tm->tm_mon],
             tm->tm_year + 1900);
}

// Renders the date into the buffer the screen is not showing, so the scene sees a new image pointer.
static void set_date(office_clock_t *c, const char *text)
{
    if (c->date.image && strcmp(text, c->date_text) == 0) {
        return;
    }
    snprintf(c->date_text, sizeof(c->date_text), "%s", text);
    const int next = c->date.shown.image == &c->date_img[0] ? 1 : 0;
    uint8_t *px = c->date_px[next];
    memset(px, 0, (size_t)c->date_w * c->date_h);
    const float w = office_font_text_width(text, &k_date);
    office_font_draw_text(text, &k_date, px, c->date_w, c->date_h, (c->date_w - w) / 2, PAD);
    c->date_img[next] = (gfx_image_t){(uint16_t)c->date_w, (uint16_t)c->date_h, px};
    c->date.image = &c->date_img[next];
}

uint32_t office_clock_apply(office_clock_t *c, const struct tm *tm, uint32_t ms)
{
    if (tm) {
        const int h = tm->tm_hour < 0 || tm->tm_hour > 23 ? 0 : tm->tm_hour;
        const int m = tm->tm_min < 0 || tm->tm_min > 59 ? 0 : tm->tm_min;
        const int d[4] = {h / 10, h % 10, m / 10, m % 10};
        for (int i = 0; i < 4; i++) {
            c->digit[i].image = &c->glyph[d[i]];
        }
    } else {
        for (int i = 0; i < 4; i++) {
            c->digit[i].image = &c->glyph[10];
        }
    }
    const bool glow = tm && ms % 1000 < 500;
    c->colon.palette = glow ? &c->ink_pal : &c->dim_pal;

    char text[OFFICE_DATE_MAX];
    office_clock_date_text(tm, text);
    set_date(c, text);
    if (!tm) {
        return 1000; // nothing moves until a time source sets the clock
    }
    ms %= 1000;
    return ms < 500 ? 500 - ms : 1000 - ms;
}

// --- The moon ---------------------------------------------------------------------------------------------------

static const char k_moon_keys[] = ".pPYy";
static gfx_color_t s_moon_colors[] = {
    0,
    GFX_RGB(64, 74, 108),   // p plate edge (as the menu's buttons)
    GFX_RGB(32, 37, 58),    // P plate
    GFX_RGB(226, 222, 186), // Y moon
    GFX_RGB(150, 148, 128), // y moon shade
};
static const char *const k_moon[] = {
    "..ppppppppp..",
    ".pPPPPPPPPPp.",
    "pPPPPYYYyPPPp",
    "pPPPYYYyPPPPp",
    "pPPYYYyPPPPPp",
    "pPPYYYPPPPPPp",
    "pPPYYYPPPPPPp",
    "pPPYYYPPPPPPp",
    "pPPYYYyPPPPPp",
    "pPPPYYYyPPPPp",
    "pPPPPYYYyPPPp",
    ".pPPPPPPPPPp.",
    "..ppppppppp..",
};
#define MOON_N (sizeof(k_moon) / sizeof(k_moon[0]))
static uint8_t s_moon_px[MOON_N * 13];
static gfx_image_t s_moon_img;
static const gfx_palette_t k_moon_pal = {s_moon_colors, sizeof(s_moon_colors) / sizeof(s_moon_colors[0])};

const gfx_image_t *office_moon_image(const gfx_palette_t **palette)
{
    if (!s_moon_img.px &&
        !gfx_image_from_ascii(&s_moon_img, s_moon_px, sizeof(s_moon_px), k_moon, (int)MOON_N, k_moon_keys)) {
        return NULL;
    }
    *palette = &k_moon_pal;
    return &s_moon_img;
}
