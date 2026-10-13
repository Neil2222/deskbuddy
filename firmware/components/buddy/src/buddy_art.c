#include "buddy_art.h"

#include <stddef.h>

// Palette keys: a character's position is its palette index ('.' = transparent).
static const char k_keys[] = ".KBbWSEGYRrk";

// Not const: keeps the palette in internal RAM (read-only data is executed/mapped from PSRAM on this board).
static gfx_color_t s_colors[] = {
    0,                     // . transparent
    GFX_RGB(22, 26, 44),   // K outline
    GFX_RGB(158, 178, 208),// B body
    GFX_RGB(104, 122, 156),// b body shade
    GFX_RGB(236, 242, 255),// W highlight
    GFX_RGB(14, 22, 32),   // S face screen
    GFX_RGB(96, 232, 255), // E eye glow
    GFX_RGB(88, 224, 128), // G green light
    GFX_RGB(252, 212, 84), // Y yellow light
    GFX_RGB(255, 84, 84),  // R antenna lit
    GFX_RGB(110, 36, 44),  // r antenna dark
    GFX_RGB(12, 14, 26),   // k ground shadow
};

static const char *const k_body[] = {
    ".............KK.............",
    ".............KK.............",
    "..KKKKKKKKKKKKKKKKKKKKKKKK..",
    "..KWWBBBBBBBBBBBBBBBBBBBBK..",
    "..KWBBBBBBBBBBBBBBBBBBBBbK..",
    "..KBBKKKKKKKKKKKKKKKKKKBbK..",
    "..KBBKSSSSSSSSSSSSSSSSKBbK..",
    "..KBBKSSSSSSSSSSSSSSSSKBbK..",
    "..KBBKSSSSSSSSSSSSSSSSKBbK..",
    "..KBBKSSSSSSSSSSSSSSSSKBbK..",
    "..KBBKSSSSSSSSSSSSSSSSKBbK..",
    "..KBBKSSSSSSSSSSSSSSSSKBbK..",
    "..KBBKSSSSSSSSSSSSSSSSKBbK..",
    "..KBBKSSSSSSSSSSSSSSSSKBbK..",
    "..KBBKKKKKKKKKKKKKKKKKKBbK..",
    "..KBbbbbbbbbbbbbbbbbbbbbbK..",
    "..KKKKKKKKKKKKKKKKKKKKKKKK..",
    "..........KbbbbbbK..........",
    "..........KKKKKKKK..........",
    "....KKKKKKKKKKKKKKKKKKKK....",
    ".KKKKWBBBBBBBBBBBBBBBBbKKKK.",
    ".KBBKBBBBKKKKKKKKKKBBBbKBBK.",
    ".KBBKBBBBKSGSYSRSSKBBBbKBBK.",
    ".KBBKBBBBKKKKKKKKKKBBBbKBBK.",
    ".KBBKBBBBBBBBBBBBBBBBBbKBBK.",
    ".KKKKbbbbbbbbbbbbbbbbbbKKKK.",
    "....KbbbbbbbbbbbbbbbbbbK....",
    "....KKKKKKKKKKKKKKKKKKKK....",
    ".......KbbK......KbbK.......",
    "......KKbbKK....KKbbKK......",
    "......KKKKKK....KKKKKK......",
};

static const char *const k_antenna_on[] = {".KKKK.", "KRRWRK", "KRRRRK", ".KKKK."};
static const char *const k_antenna_off[] = {".KKKK.", "KrrrrK", "KrrrrK", ".KKKK."};

static const char *const k_shadow[] = {
    "...kkkkkkkkkkkkkk...",
    "kkkkkkkkkkkkkkkkkkkk",
    "...kkkkkkkkkkkkkk...",
};
static const char *const k_shadow_small[] = {
    "....................",
    "....kkkkkkkkkkkk....",
    "....................",
};

static const char *const k_eyes[BUDDY_EYES_COUNT][4] = {
    [BUDDY_EYES_OPEN] = {".EE.", "EEEE", "EEEE", ".EE."},
    [BUDDY_EYES_HALF] = {"....", "....", "EEEE", ".EE."},
    [BUDDY_EYES_CLOSED] = {"....", "....", "EEEE", "...."},
    [BUDDY_EYES_HAPPY] = {".EE.", "E..E", "....", "...."},
    [BUDDY_EYES_WIDE] = {"EEEE", "EWWE", "EWWE", "EEEE"},
};

static const char *const k_mouth[BUDDY_MOUTH_COUNT][3] = {
    [BUDDY_MOUTH_NEUTRAL] = {"......", ".EEEE.", "......"},
    [BUDDY_MOUTH_SMILE] = {"E....E", ".EEEE.", "......"},
    [BUDDY_MOUTH_OPEN] = {"..EE..", ".E..E.", "..EE.."},
};

static uint8_t s_pool[1536]; // decoded pixels, internal RAM
static size_t s_used;
static buddy_art_t s_art;
static gfx_anim_frame_t s_antenna_frames[2][2];
static bool s_ready;

static bool decode(gfx_image_t *img, const char *const *rows, int n_rows)
{
    if (!gfx_image_from_ascii(img, s_pool + s_used, sizeof(s_pool) - s_used, rows, n_rows, k_keys)) {
        return false;
    }
    s_used += (size_t)img->w * img->h;
    return true;
}

#define DECODE(img, rows) decode(&(img), (rows), (int)(sizeof(rows) / sizeof((rows)[0])))

static bool load(void)
{
    buddy_art_t *a = &s_art;
    s_used = 0;
    bool ok = DECODE(a->body, k_body) && DECODE(a->antenna_on, k_antenna_on) &&
              DECODE(a->antenna_off, k_antenna_off) && DECODE(a->shadow, k_shadow) &&
              DECODE(a->shadow_small, k_shadow_small);
    for (int i = 0; ok && i < BUDDY_EYES_COUNT; i++) {
        ok = decode(&a->eyes[i], k_eyes[i], 4);
    }
    for (int i = 0; ok && i < BUDDY_MOUTH_COUNT; i++) {
        ok = decode(&a->mouth[i], k_mouth[i], 3);
    }
    if (!ok) {
        return false;
    }
    a->palette = (gfx_palette_t){s_colors, (uint16_t)(sizeof(s_colors) / sizeof(s_colors[0]))};

    s_antenna_frames[BUDDY_ANTENNA_CALM][0] = (gfx_anim_frame_t){&a->antenna_on, 900};
    s_antenna_frames[BUDDY_ANTENNA_CALM][1] = (gfx_anim_frame_t){&a->antenna_off, 900};
    s_antenna_frames[BUDDY_ANTENNA_EXCITED][0] = (gfx_anim_frame_t){&a->antenna_on, 120};
    s_antenna_frames[BUDDY_ANTENNA_EXCITED][1] = (gfx_anim_frame_t){&a->antenna_off, 120};
    a->antenna[BUDDY_ANTENNA_CALM] = (gfx_anim_t){s_antenna_frames[BUDDY_ANTENNA_CALM], 2, true};
    a->antenna[BUDDY_ANTENNA_EXCITED] = (gfx_anim_t){s_antenna_frames[BUDDY_ANTENNA_EXCITED], 2, true};

    // Layout in art pixels: antenna ball on top of the stem, face screen at rows 10..17, shadow under the feet.
    a->antenna_at = (buddy_point_t){11, 0};
    a->body_at = (buddy_point_t){0, 4};
    a->eye_at[0] = (buddy_point_t){8, 11};
    a->eye_at[1] = (buddy_point_t){16, 11};
    a->mouth_at = (buddy_point_t){11, 15};
    a->shadow_at = (buddy_point_t){4, a->body_at.y + a->body.h};
    a->w = a->body.w;
    a->h = a->shadow_at.y + a->shadow.h;
    return true;
}

const buddy_art_t *buddy_art_get(void)
{
    if (!s_ready) {
        s_ready = load();
    }
    return s_ready ? &s_art : NULL;
}
