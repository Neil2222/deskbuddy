#include "buddy_art.h"

#include <stddef.h>
#include <string.h>

// Palette keys: a character's position is its palette index ('.' = transparent). Must match BUDDY_PX_*.
static const char k_keys[] = ".KBbWSEGYRrkNnDgLlcPpOF";

// Not const: keeps the palettes in internal RAM (read-only data is executed/mapped from PSRAM on this board).
static gfx_color_t s_colors[] = {
    0,                      // . transparent
    GFX_RGB(22, 26, 44),    // K outline
    GFX_RGB(158, 178, 208), // B body
    GFX_RGB(104, 122, 156), // b body shade
    GFX_RGB(236, 242, 255), // W highlight
    GFX_RGB(14, 22, 32),    // S face screen
    GFX_RGB(96, 232, 255),  // E eye glow
    GFX_RGB(88, 224, 128),  // G green light
    GFX_RGB(252, 212, 84),  // Y yellow light
    GFX_RGB(255, 84, 84),   // R antenna lit
    GFX_RGB(110, 36, 44),   // r antenna dark
    GFX_RGB(12, 14, 26),    // k ground shadow
    GFX_RGB(124, 255, 40),  // N Monster neon green
    GFX_RGB(34, 96, 26),    // n dark green (drained claw, label)
    GFX_RGB(20, 22, 26),    // D can black
    GFX_RGB(70, 76, 88),    // g can gloss
    GFX_RGB(204, 210, 220), // L silver
    GFX_RGB(128, 134, 148), // l silver shade
    GFX_RGB(64, 184, 72),   // c cable
    GFX_RGB(32, 37, 58),    // P panel plate (the touchscreen menu's buttons)
    GFX_RGB(64, 74, 108),   // p panel edge
    GFX_RGB(226, 150, 70),  // O cigarette filter
    GFX_RGB(255, 140, 40),  // F ember, glowing
};
#define N_COLORS (sizeof(s_colors) / sizeof(s_colors[0]))
static gfx_color_t s_glow[N_COLORS], s_zap[N_COLORS], s_fury[N_COLORS], s_drowsy[N_COLORS], s_dark[N_COLORS];

// The body in two parts that normally move together; the head can also wobble on its own.
static const char *const k_head[] = {
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
};
static const char *const k_torso[] = {
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
static const char *const k_antenna_green[] = {".KKKK.", "KNNWNK", "KNNNNK", ".KKKK."};
static const char *const k_antenna_white[] = {".KKKK.", "KWWWWK", "KWWWWK", ".KKKK."};

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
    [BUDDY_EYES_BOLT] = {".YYY", ".YY.", "YYY.", ".Y.."},
    [BUDDY_EYES_LIDDED] = {"....", "EEEE", "EEEE", ".EE."},
    [BUDDY_EYES_ANGRY] = {"E...", "EEE.", "EEEE", ".EE."}, // left eye; the right one is mirrored
    [BUDDY_EYES_SQUEEZE] = {"EE..", "..EE", "EE..", "...."}, // left: >, right: <
    [BUDDY_EYES_FOCUS] = {"....", "EEEE", "EEEE", "...."},
};

static const char *const k_mouth[BUDDY_MOUTH_COUNT][3] = {
    [BUDDY_MOUTH_NEUTRAL] = {"......", ".EEEE.", "......"},
    [BUDDY_MOUTH_SMILE] = {"E....E", ".EEEE.", "......"},
    [BUDDY_MOUTH_OPEN] = {"..EE..", ".E..E.", "..EE.."},
    [BUDDY_MOUTH_ZAP] = {".E..E.", "E.EE.E", "......"},
    [BUDDY_MOUTH_GRIN] = {"E....E", "EEEEEE", ".EEEE."},
    [BUDDY_MOUTH_SMIRK] = {"......", ".....E", ".EEEE."},
    [BUDDY_MOUTH_FROWN] = {"......", ".EEEE.", "E....E"},
    [BUDDY_MOUTH_GRIT] = {".EEEE.", "EWWWWE", ".EEEE."},
    [BUDDY_MOUTH_TONGUE] = {"......", ".EEEE.", "...RR."},
    [BUDDY_MOUTH_PUFF] = {"......", "..EE..", "..EE.."},
};

// --- Monster props ---

// Black can, silver lid, three neon claw marks (rows 4..11), a label band.
static const char *const k_can[] = {
    "..KKKKKKKK..",
    ".KLLLLLWLLK.",
    ".KllllllllK.",
    "KDDDDDDDDDDK",
    "KgNNDNNDNNDK",
    "KgNNDNNDNNDK",
    "KgDNNDNNDNNK",
    "KgDNNDNNDNNK",
    "KgNNDNNDNNDK",
    "KgNDDNNDDNDK",
    "KgDNDDNNDNDK",
    "KgDDDDDNDDDK",
    "KgDDDDDDDDDK",
    "KgDDDDDDDDDK",
    "KgnnnnnnnnDK",
    "KgDDDDDDDDDK",
    "KgnnnnnnDDDK",
    "KDDDDDDDDDDK",
    ".KllllllllK.",
    "..KKKKKKKK..",
};
#define CAN_CLAW_TOP 4
#define CAN_CLAW_ROWS 8

static const char *const k_can_crushed[] = {
    "....KKKKKK..",
    "..KKLLLWLLK.",
    ".KllllllKK..",
    "KDDDDgDDDDDK",
    ".KDnnDDnnDK.",
    "KgDDnnDDnnDK",
    ".KnDDDnnDDK.",
    "KgDDnDDDDnDK",
    ".KDDDDDDDK..",
    "KgnnnnnnDDDK",
    ".KllllllllK.",
    "..KKKKKKKK..",
};

static const char *const k_can_shadow[] = {
    ".kkkkkkkkkkkk.",
    "...kkkkkkkk...",
};

static const char *const k_port[] = {"lL", "lS", "lL"};
static const char *const k_plug[] = {"LL", "NN", "ll"};
static const char *const k_coil[] = {".ccc.", "c...c", "c.c.c", "c...c", ".ccc."};
static const char *const k_hand[] = {"B..B", "BbbB", ".bb."};
static const char *const k_alert[] = {"KKKK", "KYYK", "KYYK", "KYYK", "KYYK", "KKKK", "KYYK", "KKKK"};

static const char *const k_bolt[BUDDY_BOLT_SHAPES][7] = {
    {"...NN", "..NN.", ".NNW.", "NNNNN", ".WNN.", ".NN..", "NN..."},
    {"..W.", ".N..", "N...", ".NW.", "..N.", ".N..", "W..."},
};

static const char *const k_spark[BUDDY_SPARK_FRAMES][5] = {
    {"..W..", ".NWN.", "WWWWW", ".NWN.", "..W.."},
    {"W...W", ".N.N.", "..W..", ".N.N.", "W...W"},
    {".....", "..N..", ".NWN.", "..N..", "....."},
};

static const char *const k_poof[BUDDY_POOF_FRAMES][5] = {
    {".LL..", "LWWL.", "LWWWL", ".LWWL", "..LL."},
    {".....", ".l.l.", "..L..", ".l.l.", "....."},
};

// Battery: frame and tip; the interior (columns 2..9, rows 2..4) holds BUDDY_BATTERY_SEGMENTS segments.
static const char *const k_battery[] = {
    "KKKKKKKKKKKK.",
    "KLLLLLLLLLLK.",
    "KLSSSSSSSSLKK",
    "KLSSSSSSSSLLK",
    "KLSSSSSSSSLKK",
    "KLLLLLLLLLLK.",
    "KKKKKKKKKKKK.",
};
#define BATTERY_X 2
#define BATTERY_Y 2
#define BATTERY_SEG_W 2
#define BATTERY_SEG_H 3

// --- Mood props: particles and the cigarette ---

static const char *const k_smoke[3][4] = {
    {".L..", "LWL.", ".L..", "...."},
    {".LL.", "LWWL", "LWWL", ".LL."},
    {".l..", "l.l.", ".l.l", "..l."},
};
static const char *const k_z[2][5] = {
    {"WWWW.", "..W..", ".W...", "WWWW.", "....."},
    {"WWWWW", "...W.", "..W..", ".W...", "WWWWW"},
};
static const char *const k_steam[2][4] = {
    {".WW.", "WWWW", "WWWW", ".WW."},
    {".LL.", "L..L", "L..L", ".LL."},
};
static const char *const k_sweat[] = {"..K..", ".KEK.", "KEWEK", "KEEEK", ".KKK."};
static const char *const k_sparkle[2][5] = {
    {".....", "..Y..", ".YWY.", "..Y..", "....."},
    {"..Y..", "..Y..", "YYWYY", "..Y..", "..Y.."},
};
static const char *const k_anger[2][7] = {
    {".......", "..R.R..", ".RR.RR.", ".......", ".RR.RR.", "..R.R..", "......."},
    {"..R.R..", ".RR.RR.", "RR...RR", ".......", "RR...RR", ".RR.RR.", "..R.R.."},
};
// The cigarette: filter, paper (shorter as it burns down) and the tip, by ember (see buddy_props_t.ember).
static const char *const k_cig_tip[BUDDY_EMBERS] = {"lr", "lR", "FY"};

static uint8_t s_pool[7168]; // decoded pixels, internal RAM
static size_t s_used;
static buddy_art_t s_art;
static gfx_anim_frame_t s_antenna_frames[BUDDY_ANTENNA_COUNT][3];
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

// Copies src into fresh pool storage, so the copy can be recolored.
static uint8_t *clone(gfx_image_t *dst, const gfx_image_t *src)
{
    const size_t n = (size_t)src->w * src->h;
    if (n > sizeof(s_pool) - s_used) {
        return NULL;
    }
    uint8_t *px = s_pool + s_used;
    memcpy(px, src->px, n);
    s_used += n;
    *dst = (gfx_image_t){src->w, src->h, px};
    return px;
}

static uint8_t key(char c)
{
    return (uint8_t)(strchr(k_keys, c) - k_keys);
}

// Replaces color `from` by `to` inside a rectangle of the image.
static void recolor(uint8_t *px, int stride, int x0, int y0, int w, int h, char from, char to)
{
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            if (px[y * stride + x] == key(from)) {
                px[y * stride + x] = key(to);
            }
        }
    }
}

static bool load_props(buddy_props_art_t *p)
{
    bool ok = DECODE(p->can[BUDDY_CAN_LEVELS - 1], k_can) && DECODE(p->can_crushed, k_can_crushed) &&
              DECODE(p->can_shadow, k_can_shadow) && DECODE(p->port, k_port) && DECODE(p->plug, k_plug) &&
              DECODE(p->coil, k_coil) && DECODE(p->hand, k_hand) && DECODE(p->alert, k_alert);
    for (int i = 0; ok && i < BUDDY_BOLT_SHAPES; i++) {
        ok = decode(&p->bolt[i], k_bolt[i], 7);
    }
    for (int i = 0; ok && i < BUDDY_SPARK_FRAMES; i++) {
        ok = decode(&p->spark[i], k_spark[i], 5);
    }
    for (int i = 0; ok && i < BUDDY_POOF_FRAMES; i++) {
        ok = decode(&p->poof[i], k_poof[i], 5);
    }
    if (!ok) {
        return false;
    }

    // Draining: the claw marks go dark from the top down.
    const gfx_image_t *full = &p->can[BUDDY_CAN_LEVELS - 1];
    for (int level = 0; level < BUDDY_CAN_LEVELS - 1; level++) {
        uint8_t *px = clone(&p->can[level], full);
        if (!px) {
            return false;
        }
        const int dark_rows = (BUDDY_CAN_LEVELS - 1 - level) * CAN_CLAW_ROWS / (BUDDY_CAN_LEVELS - 1);
        recolor(px, full->w, 0, CAN_CLAW_TOP, full->w, dark_rows, 'N', 'n');
    }

    // Battery: empty, 1..4 segments, then two alternating overflow frames.
    gfx_image_t empty;
    if (!DECODE(empty, k_battery)) {
        return false;
    }
    for (int i = 0; i < BUDDY_BATTERY_FRAMES; i++) {
        uint8_t *px = clone(&p->battery[i], &empty);
        if (!px) {
            return false;
        }
        const int w = empty.w;
        if (i <= BUDDY_BATTERY_SEGMENTS) {
            recolor(px, w, BATTERY_X, BATTERY_Y, i * BATTERY_SEG_W, BATTERY_SEG_H, 'S', 'N');
        } else {
            const bool a = i == BUDDY_BATTERY_OVER - 1;
            recolor(px, w, BATTERY_X, BATTERY_Y, BUDDY_BATTERY_SEGMENTS * BATTERY_SEG_W, BATTERY_SEG_H, 'S',
                    a ? 'N' : 'W');
            recolor(px, w, 0, 0, w, empty.h, 'L', a ? 'W' : 'Y');
        }
    }

    // Particles.
    static const uint8_t k_frames[BUDDY_PART_COUNT] = {
        [BUDDY_PART_SMOKE] = 3, [BUDDY_PART_STEAM] = 2,   [BUDDY_PART_Z] = 2,
        [BUDDY_PART_SWEAT] = 1, [BUDDY_PART_SPARKLE] = 2, [BUDDY_PART_ANGER] = 2,
    };
    memcpy(p->part_frames, k_frames, sizeof(k_frames));
    for (int i = 0; i < 3; i++) {
        ok = ok && decode(&p->part[BUDDY_PART_SMOKE][i], k_smoke[i], 4);
    }
    for (int i = 0; i < 2; i++) {
        ok = ok && decode(&p->part[BUDDY_PART_STEAM][i], k_steam[i], 4) &&
             decode(&p->part[BUDDY_PART_Z][i], k_z[i], 5) &&
             decode(&p->part[BUDDY_PART_SPARKLE][i], k_sparkle[i], 5) &&
             decode(&p->part[BUDDY_PART_ANGER][i], k_anger[i], 7);
    }
    ok = ok && DECODE(p->part[BUDDY_PART_SWEAT][0], k_sweat);
    // Cigarettes: two filter pixels, paper, then the tip; each length shorter by a pixel of paper.
    for (int len = 0; ok && len < BUDDY_CIG_LENGTHS; len++) {
        for (int e = 0; ok && e < BUDDY_EMBERS; e++) {
            char row[16];
            const int paper = 2 + len;
            memcpy(row, "OO", 2);
            memset(row + 2, 'W', (size_t)paper);
            memcpy(row + 2 + paper, k_cig_tip[e], 3); // two tip pixels and the NUL
            const char *rows[] = {row};
            ok = decode(&p->cig[len][e], rows, 1);
        }
    }
    if (!ok) {
        return false;
    }

    // Layout. The can stands on the ground to the robot's right; the port sits on the right side of the head.
    p->ground_y = 34;
    p->can_at = (buddy_point_t){36, p->ground_y + 1 - full->h};
    p->plug_can = (buddy_point_t){p->can_at.x + 5, p->can_at.y - p->plug.h};
    p->hand_can = (buddy_point_t){p->plug_can.x, p->plug_can.y + 1}; // holds the plug 2 px above its slot
    p->port_at = (buddy_point_t){26, 11};
    p->plug_port = (buddy_point_t){p->port_at.x + p->port.w, p->port_at.y};
    p->hand_port = (buddy_point_t){p->plug_port.x + 1, p->plug_port.y + p->plug.h};
    p->arm_root = (buddy_point_t){26, 26};
    p->hand_show = (buddy_point_t){31, 19};
    // The cigarette's filter sits in the right corner of the mouth (mouth row 16); at rest it is held out beside
    // the head, its tip pointing away from him.
    p->hand_mouth = (buddy_point_t){15 + BUDDY_CIG_GRIP, 17};
    p->hand_rest = (buddy_point_t){29, 23};
    p->from[BUDDY_FROM_MOUTH] = (buddy_point_t){13, 16};
    p->from[BUDDY_FROM_HEAD_L] = (buddy_point_t){0, 6};
    p->from[BUDDY_FROM_HEAD_R] = (buddy_point_t){27, 6};
    p->from[BUDDY_FROM_ABOVE] = (buddy_point_t){21, 1};
    p->from[BUDDY_FROM_BROW] = (buddy_point_t){1, 10};
    p->battery_at = (buddy_point_t){-17, 9};
    p->alert_at = (buddy_point_t){22, -10};
    // Bolts crackle along the sides of the head and torso: inside the columns the trembling robot redraws anyway and
    // below the antenna, so they never widen or heighten its dirty rectangle (or bridge it to the battery).
    static const buddy_point_t k_bolt_at[BUDDY_BOLT_SLOTS] = {{-3, 4}, {24, 4}, {-3, 11}, {25, 16}, {-3, 18}, {24, 22}};
    memcpy(p->bolt_at, k_bolt_at, sizeof(k_bolt_at));
    p->cable_len = 26;
    return true;
}

static void make_palettes(buddy_art_t *a)
{
    memcpy(s_glow, s_colors, sizeof(s_colors));
    s_glow[key('E')] = s_colors[key('N')];

    // Electrocuted: a flash of the photographic negative.
    memcpy(s_zap, s_colors, sizeof(s_colors));
    s_zap[key('K')] = GFX_RGB(220, 255, 190);
    s_zap[key('B')] = GFX_RGB(30, 72, 24);
    s_zap[key('b')] = GFX_RGB(18, 44, 16);
    s_zap[key('W')] = GFX_RGB(124, 255, 40);
    s_zap[key('S')] = GFX_RGB(200, 255, 170);
    s_zap[key('E')] = GFX_RGB(10, 30, 10);
    s_zap[key('Y')] = GFX_RGB(40, 20, 120);

    a->palette = (gfx_palette_t){s_colors, (uint16_t)N_COLORS};
    a->tint[BUDDY_TINT_NORMAL] = a->palette;
    a->tint[BUDDY_TINT_GLOW] = (gfx_palette_t){s_glow, (uint16_t)N_COLORS};
    a->tint[BUDDY_TINT_ZAP] = (gfx_palette_t){s_zap, (uint16_t)N_COLORS};

    // Fury: the face screen flushes red and the eyes burn orange.
    memcpy(s_fury, s_colors, sizeof(s_colors));
    s_fury[key('S')] = GFX_RGB(74, 10, 16);
    s_fury[key('E')] = GFX_RGB(255, 120, 72);
    a->tint[BUDDY_TINT_FURY] = (gfx_palette_t){s_fury, (uint16_t)N_COLORS};

    // Drowsy: the eye glow turned down.
    memcpy(s_drowsy, s_colors, sizeof(s_colors));
    s_drowsy[key('E')] = GFX_RGB(52, 140, 166);
    a->tint[BUDDY_TINT_DROWSY] = (gfx_palette_t){s_drowsy, (uint16_t)N_COLORS};

    // Dark: the face screen switched off, eyes and all, in exactly the Office Mode clock's background colour
    // (office_clock.c): a face filling the display turns into the clock with only the digits appearing.
    memcpy(s_dark, s_colors, sizeof(s_colors));
    s_dark[key('S')] = GFX_RGB(8, 9, 12);
    s_dark[key('E')] = GFX_RGB(8, 9, 12);
    a->tint[BUDDY_TINT_DARK] = (gfx_palette_t){s_dark, (uint16_t)N_COLORS};
}

static void make_antenna(buddy_art_t *a, buddy_antenna_t which, const gfx_image_t *const *images, uint16_t ms, int n)
{
    for (int i = 0; i < n; i++) {
        s_antenna_frames[which][i] = (gfx_anim_frame_t){images[i], ms};
    }
    a->antenna[which] = (gfx_anim_t){s_antenna_frames[which], (uint8_t)n, true};
}

static bool load(void)
{
    buddy_art_t *a = &s_art;
    s_used = 0;
    bool ok = DECODE(a->head, k_head) && DECODE(a->torso, k_torso) && DECODE(a->antenna_on, k_antenna_on) &&
              DECODE(a->antenna_off, k_antenna_off) && DECODE(a->antenna_green, k_antenna_green) &&
              DECODE(a->antenna_white, k_antenna_white) && DECODE(a->shadow, k_shadow) &&
              DECODE(a->shadow_small, k_shadow_small);
    for (int i = 0; ok && i < BUDDY_EYES_COUNT; i++) {
        ok = decode(&a->eyes[i], k_eyes[i], 4);
    }
    for (int i = 0; ok && i < BUDDY_MOUTH_COUNT; i++) {
        ok = decode(&a->mouth[i], k_mouth[i], 3);
    }
    if (!ok || !load_props(&a->props)) {
        return false;
    }
    make_palettes(a);

    const gfx_image_t *blink[] = {&a->antenna_on, &a->antenna_off};
    const gfx_image_t *strobe[] = {&a->antenna_on, &a->antenna_green, &a->antenna_white};
    make_antenna(a, BUDDY_ANTENNA_CALM, blink, 900, 2);
    make_antenna(a, BUDDY_ANTENNA_EXCITED, blink, 120, 2);
    make_antenna(a, BUDDY_ANTENNA_OVERLOAD, strobe, 50, 3);
    // Dim: mostly dark, a short faint blink every few seconds.
    s_antenna_frames[BUDDY_ANTENNA_DIM][0] = (gfx_anim_frame_t){&a->antenna_off, 2600};
    s_antenna_frames[BUDDY_ANTENNA_DIM][1] = (gfx_anim_frame_t){&a->antenna_on, 180};
    a->antenna[BUDDY_ANTENNA_DIM] = (gfx_anim_t){s_antenna_frames[BUDDY_ANTENNA_DIM], 2, true};
    a->eye_mirror[BUDDY_EYES_ANGRY] = a->eye_mirror[BUDDY_EYES_SQUEEZE] = true;

    // Layout in art pixels: antenna ball on top of the stem, face screen at rows 10..17, shadow under the feet.
    a->antenna_at = (buddy_point_t){11, 0};
    a->head_at = (buddy_point_t){0, 4};
    a->torso_at = (buddy_point_t){0, a->head_at.y + a->head.h};
    a->eye_at[0] = (buddy_point_t){8, 11};
    a->eye_at[1] = (buddy_point_t){16, 11};
    a->mouth_at = (buddy_point_t){11, 15};
    a->shadow_at = (buddy_point_t){4, a->torso_at.y + a->torso.h};
    a->w = a->head.w > a->torso.w ? a->head.w : a->torso.w;
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
