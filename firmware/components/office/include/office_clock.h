// Office Mode: a quiet desk clock that replaces the cartoon, and the moon that stands for it.
//
// Hardware independent: the caller supplies the wall-clock time (or none), the scene, and the glyph memory. The
// clock face is HH:MM in large thin numerals (24-hour), with the date underneath in letter-spaced capitals
// ("TUE 14 OCT 2026"). Without a valid time it shows "--:--" and "CLOCK NOT SET" instead of guessing.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "gfx_scene.h"
#include "office_font.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OFFICE_CLOCK_MEMORY (96 * 1024) // bytes of glyph memory office_clock_init() needs
#define OFFICE_DATE_MAX 24              // longest date text, including the terminator

// The clock background: one flat, nearly black colour (the glyph edges blend into exactly this colour).
void office_background(void *ctx, gfx_color_t *dst, int x, int y, int w);

// Time values before 2026-01-01 are taken as "never set": after power-up the clock starts at 1970.
bool office_time_valid(time_t t);

typedef struct {
    gfx_sprite_t digit[4], colon, date;
    gfx_image_t glyph[11]; // '0'..'9', then '-'
    gfx_image_t colon_img;
    gfx_image_t date_img[2];
    uint8_t *date_px[2];
    int date_w, date_h;
    char date_text[OFFICE_DATE_MAX];
    gfx_color_t ink[OFFICE_INK_LEVELS + 1], dim[OFFICE_INK_LEVELS + 1], quiet[OFFICE_INK_LEVELS + 1];
    gfx_palette_t ink_pal, dim_pal, quiet_pal;
    gfx_rect_t screen;
    bool visible;
} office_clock_t;

// Rasterizes the numerals into mem (OFFICE_CLOCK_MEMORY bytes, kept for the clock's lifetime; slow memory is
// fine, it is only read when the clock redraws), lays the face out centred in `screen`, and adds its sprites to
// the scene at z, hidden. Returns false if the memory is too small or the scene is full.
bool office_clock_init(office_clock_t *c, gfx_scene_t *scene, gfx_rect_t screen, uint8_t *mem, size_t mem_size,
                       int z);

void office_clock_show(office_clock_t *c, bool visible);

// Shows tm (local time), or the "not set" face when tm is NULL. ms is the time into the current second: the colon
// glows during the first half of every second. Returns ms until the face changes next.
uint32_t office_clock_apply(office_clock_t *c, const struct tm *tm, uint32_t ms);

// Formats the date line for tm ("TUE 14 OCT 2026"), or "CLOCK NOT SET" for NULL.
void office_clock_date_text(const struct tm *tm, char out[OFFICE_DATE_MAX]);

// The moon on a round plate: Office Mode's symbol in pictures of DeskBuddy (the companion's Office tile).
// NULL if the built-in art is malformed (a programming error, caught by the host tests).
const gfx_image_t *office_moon_image(const gfx_palette_t **palette);

#ifdef __cplusplus
}
#endif
