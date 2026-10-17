#include "menu_art.h"

#include <string.h>

#include "buddy_activity.h"
#include "buddy_art.h"
#include "marquee.h"

// Palette keys for the menu's own art: Buddy's keys, then the menu's colours in INK_* order.
static const char k_keys[] = ".KBbWSEGYRrkNnDgLlcPpOF"
                             "ahdvqfeumowxjJMz";

static const gfx_color_t k_own[INK_COUNT - INK_BUDDY_COUNT] = {
    GFX_RGB(255, 170, 40),  // a amber (the LED sign's lit LEDs)
    GFX_RGB(255, 232, 160), // h amber, lit up
    GFX_RGB(170, 92, 16),   // d amber, dark
    GFX_RGB(24, 28, 46),    // v pressed face
    GFX_RGB(10, 12, 20),    // q ledge
    GFX_RGB(52, 62, 98),    // f lit edge, tapped face
    GFX_RGB(30, 104, 132),  // e cyan glow
    GFX_RGB(214, 150, 72),  // u tan
    GFX_RGB(128, 76, 40),   // m brown
    GFX_RGB(78, 44, 24),    // o coffee
    GFX_RGB(72, 150, 255),  // w water
    GFX_RGB(160, 210, 255), // x water, light
    GFX_RGB(150, 104, 230), // j purple
    GFX_RGB(88, 60, 150),   // J purple, dark
    GFX_RGB(226, 222, 186), // M moon (as Office Mode's)
    GFX_RGB(150, 148, 128), // z moon shade
};
#define PANEL_R 20 // the panel background (layout.c), what dimmed colours fade into
#define PANEL_G 23
#define PANEL_B 36

// Not const: palettes in internal RAM (read-only data is mapped from PSRAM on this board).
static gfx_color_t s_colors[MENU_PAL_COUNT][INK_COUNT];
static gfx_palette_t s_palettes[MENU_PAL_COUNT];

// --- Icons ----------------------------------------------------------------------------------------------------------

static const char *const k_star[] = {
    "........K........",
    ".......KYK.......",
    ".......KYK.......",
    "......KYYYK......",
    "......KYWYK......",
    ".....KYYWYYK.....",
    "..KKKYYYWYYYKKK..",
    "KKYYYYYWWWYYYYYKK",
    "..KKKYYYWYYYKKK..",
    ".....KYYWYYK.....",
    "......KYWYK......",
    "......KYYYK......",
    ".......KYK.......",
    ".......KYK.......",
    "........K........",
};
static const char *const k_apple[] = {
    "..........G.....",
    ".......K.GGG....",
    "........KGG.....",
    "........K.......",
    "...KKKK.K.KKK...",
    "..KRRRRKKKRRRK..",
    ".KRWWRRRRRRRRRK.",
    ".KRWRRRRRRRRRRK.",
    "KRRRRRRRRRRRRRRK",
    "KRRRRRRRRRRRRRRK",
    "KRRRRRRRRRRRRRrK",
    "KRRRRRRRRRRRRRrK",
    ".KRRRRRRRRRRRrK.",
    ".KRRRRRRRRRRrrK.",
    "..KRRRRrrRRrrK..",
    "...KKKK..KKKK...",
};
static const char *const k_gamepad[] = {
    "....KKKKKKKKKKKKKK....",
    "..KKjjjjjjjjjjjjjjKK..",
    ".KjjjjjjjjjjjjjjjjjjK.",
    "KjjjjWjjjjjjjjjjjYYjjK",
    "KjjjjWjjjjjjjjjjjYYjjK",
    "KjjWWWWWjjjjjjRRjjjjjK",
    "KjjjjWjjjjjjjjRRjjGGjK",
    "KjjjjWjjjjjjjjjjjjGGjK",
    "KJjjjjjjKKKKKKjjjjjjJK",
    "KJJjjjjK......KjjjjJJK",
    ".KJJJJK........KJJJJK.",
    "..KKKK..........KKKK..",
};
static const char *const k_calendar[] = {
    "...KK........KK...",
    ".KKWWKKKKKKKKWWKK.",
    "KRRWWRRRRRRRRWWRRK",
    "KRRRRRRRRRRRRRRRRK",
    "KrrrrrrrrrrrrrrrrK",
    "KWWWWWWWWWWWWWWWWK",
    "KWLLWLLWLLWLLWaaWK",
    "KWLLWLLWLLWLLWaaWK",
    "KWWWWWWWWWWWWWWWWK",
    "KWLLWLLWLLWLLWLLWK",
    "KWLLWLLWLLWLLWLLWK",
    "KWWWWWWWWWWWWWWWWK",
    "KWLLWLLWLLWLLWLLWK",
    "KWLLWLLWLLWLLWLLWK",
    "KWWWWWWWWWWWWWWWWK",
    ".KKKKKKKKKKKKKKKK.",
};
static const char *const k_moon[] = {
    "......KKKKKK........",
    "....KKMMMMMMK.......",
    "...KMMMMMMMK........",
    "..KMMMMMMKK.........",
    ".KMMMMMK......Y.....",
    ".KMMMMK......YWY....",
    "KMMMMMK.......Y.....",
    "KMMMMK..............",
    "KMMMMK..............",
    "KMMMMK..............",
    "KzMMMK..............",
    "KzMMMMK........W....",
    "KzzMMMK.............",
    ".KzzMMMK............",
    ".KzzzMMMKK..........",
    "..KzzzzMMMKK........",
    "...KKzzzzzzzK.......",
    ".....KKKKKKK........",
};
static const char *const k_water[] = {
    "KKKKKKKKKKKKKKKKKK",
    "KLSSSSSSSSSSSSSSLK",
    "KLSSSSSSSSSSSSSSLK",
    "KLxxxxxxxxxxxxxxLK",
    "KLwWwwwwwwwwwwwwLK",
    "KLwWwwwwwxxxwwwwLK",
    ".KLwWwwwwxWWxwwLK.",
    ".KLwWwwwwxxxxwwLK.",
    ".KLwWwwwwwwwwwwLK.",
    ".KLwwwwwwwwwwwwLK.",
    "..KLwwwwwwwwwwLK..",
    "..KLwwwwwwwwwwLK..",
    "..KLwwwwwwwwwwLK..",
    "..KLwwwwwwwwwwLK..",
    "...KLwwwwwwwwLK...",
    "...KLwwwwwwwwLK...",
    "...KLLLLLLLLLLK...",
    "...KKKKKKKKKKKK...",
};
static const char *const k_coffee[] = {
    "....L....L............",
    ".....L....L...........",
    "....L....L............",
    ".....L....L...........",
    "KKKKKKKKKKKKKKKK......",
    "KLooooooooooooLK......",
    "KWWWWWWWWWWWWWLKKKKK..",
    "KEEEEEEEEEEEEELK..LK..",
    "KWWWWWWWWWWWWWLK..LK..",
    "KWWWWWWWWWWWWWLK..LK..",
    "KWWWWWWWWWWWWWLKKKKK..",
    "KWWWWWWWWWWWWWLK......",
    "KWWWWWWWWWWWWWLK......",
    "KLWWWWWWWWWWWLLK......",
    ".KLLLLLLLLLLLLK.......",
    "..KKKKKKKKKKKK........",
};
static const char *const k_burger[] = {
    "......KKKKKKKKKK......",
    "...KKKuuuuuuuuuuKKK...",
    "..KuuuuWuuuuuuuWuuuK..",
    ".KuuWuuuuuuuWuuuuuuuK.",
    "KuuuuuuuuuuuuuuuuuuuuK",
    "KuuuuuuuuuuuuuuuuuuuuK",
    "KKKKKKKKKKKKKKKKKKKKKK",
    "KGGnGGGGGnGGGGGnGGGGGK",
    ".KnGGnGGGnGGnGGGnGGnK.",
    "KYYYYYYYYYYYYYYYYYYYYK",
    "KmmmmmmmmmmmmmmmmmmmmK",
    "KmommmommmmommmmommmmK",
    "KmmmmmmmmmmmmmmmmmmmmK",
    "KKKKKKKKKKKKKKKKKKKKKK",
    "KuuuuuuuuuuuuuuuuuuuuK",
    ".KuuuuuuuuuuuuuuuuuuK.",
    "..KKKKKKKKKKKKKKKKKK..",
};
static const char *const k_cookie[] = {
    ".......KKKKKK.......",
    ".....KKuuuuuuKK.....",
    "....KuuuuuuuuuuK....",
    "...KuuuoouuuuuuuK...",
    "..KuuuuoouuuuuoouK..",
    "..KuuuuuuuuuuuoouK..",
    ".KuuuuuuuuuuuuuuuuK.",
    ".KuuoouuuuuuuuuuuuK.",
    ".KuuoouuuuuuuoouuuK.",
    ".KuuuuuuuuuuuoouuuK.",
    ".KuuuuuuoouuuuuuuuK.",
    ".KuuuuuuoouuuuuuuuK.",
    "..KuuuuuuuuuuuoouK..",
    "..KuuouuuuuuuuoouK..",
    "...KuuuuuuuuuuuuK...",
    "....KuuuuuuuuuuK....",
    ".....KKuuuuuuKK.....",
    ".......KKKKKK.......",
};
static const char *const k_bubbles[] = { // a white speech bubble and an amber one answering it: a meeting
    ".KKKKKKKKKK.........",
    "KWWWWWWWWWWK........",
    "KWWWWWWWWWWK........",
    "KWKWWKWWKWWK........",
    "KWWWWWWWWWWK........",
    "KWWWWWWWWKKKKKKKKKK.",
    ".KKWKKKKKaaaaaaaaaaK",
    "..KK....KaaaaaaaaaaK",
    "........KadaadaadaaK",
    "........KaaaaaaaaaaK",
    "........KaaaaaaaaaaK",
    ".........KKKKKKKaKK.",
    "................KK..",
};
static const char *const k_sneaker[] = { // a running shoe: stretch your legs
    "........KKKKK.........",
    ".......KWWWWWK........",
    "......KWKKKKWK........",
    ".....KWWKWKWWK........",
    ".....KWWWKWKWWK.......",
    "....KWWWWWWWWWWKKK....",
    "...KWWWWWWWWWWWWWWKK..",
    "..KWWWWWWWWWWWWWWWWWK.",
    ".KRRRRRRRRRRRRRRRRRRRK",
    "KWWWWWWWWWWWWWWWWWWWWK",
    "KLLLLLLLLLLLLLLLLLLLLK",
    ".KKKKKKKKKKKKKKKKKKKK.",
};
static const char *const k_gear[] = { // a brass gear: the Mechanical Skeleton face
    ".......KKKK.......",
    ".......KhhK.......",
    "...KK..KhhK..KK...",
    "..KhhKKhhhaKKaaK..",
    "..KhhhhhhaaaaaaK..",
    "...KhhhhaaaaaaK...",
    "...KhhhKKKKaaaK...",
    "KKKhhhK....KaadKKK",
    "KhhhhaK....KaddddK",
    "KhhhaaK....KdddddK",
    "KKKaaaK....KdddKKK",
    "...KaaaKKKKdddK...",
    "...KaaaaadddddK...",
    "..KaaaaadddddddK..",
    "..KaaKKddddKKddK..",
    "...KK..KddK..KK...",
    ".......KddK.......",
    ".......KKKK.......",
};
static const char *const k_led[] = {
    ".KKKK.",
    "KEWEEK",
    "KWEEEK",
    "KEEEEK",
    "KEEEEK",
    ".KKKK.",
};
static const char *const k_triangle[] = {"W....", "WW...", "WWW..", "WWWW.", "WWWWW", "WWWW.", "WWW..", "WW...", "W...."};
static const char *const k_chevron[] = {"W....", "WW...", ".WW..", "..WW.", "...WW", "..WW.", ".WW..", "WW...", "W...."};

#define ART(rows) {rows, (int)(sizeof(rows) / sizeof(rows[0]))}
static const struct {
    const char *const *rows;
    int n;
} k_icon_art[MENU_ICON_COUNT] = {
    [MENU_ICON_ACTIVITIES] = ART(k_star), [MENU_ICON_FOOD] = ART(k_apple),   [MENU_ICON_GAMES] = ART(k_gamepad),
    [MENU_ICON_SCHEDULE] = ART(k_calendar), [MENU_ICON_OFFICE] = ART(k_moon), [MENU_ICON_WATER] = ART(k_water),
    [MENU_ICON_COFFEE] = ART(k_coffee),   [MENU_ICON_LUNCH] = ART(k_burger), [MENU_ICON_SNACKS] = ART(k_cookie),
    [MENU_ICON_MEETING] = ART(k_bubbles), [MENU_ICON_WALK] = ART(k_sneaker),
    [MENU_ICON_GEAR] = ART(k_gear),
};

static uint8_t s_pool[4096]; // decoded pixels, internal RAM
static size_t s_used;
static gfx_image_t s_icons[MENU_ICON_COUNT], s_led, s_triangle, s_chevron;
static bool s_ready;

static bool decode(gfx_image_t *img, const char *const *rows, int n)
{
    if (!gfx_image_from_ascii(img, s_pool + s_used, sizeof(s_pool) - s_used, rows, n, k_keys)) {
        return false;
    }
    s_used += (size_t)img->w * img->h;
    return true;
}

static gfx_color_t fade(gfx_color_t c)
{
    const int r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
    return GFX_RGB((r * 2 + PANEL_R * 3) / 5, (g * 2 + PANEL_G * 3) / 5, (b * 2 + PANEL_B * 3) / 5);
}

static bool load(void)
{
    const buddy_art_t *a = buddy_art_get();
    if (!a || a->palette.count != INK_BUDDY_COUNT || strlen(k_keys) != INK_COUNT) {
        return false;
    }
    gfx_color_t *normal = s_colors[MENU_PAL_NORMAL];
    memcpy(normal, a->palette.colors, INK_BUDDY_COUNT * sizeof(gfx_color_t));
    memcpy(normal + INK_BUDDY_COUNT, k_own, sizeof(k_own));
    for (int i = 0; i < INK_COUNT; i++) {
        const bool keep = i == INK_AMBER || i == INK_AMBER_HOT || i == INK_AMBER_DARK;
        s_colors[MENU_PAL_DIM][i] = keep ? normal[i] : fade(normal[i]);
    }
    memcpy(s_colors[MENU_PAL_LED_ON], normal, sizeof(s_colors[0]));
    memcpy(s_colors[MENU_PAL_LED_OFF], normal, sizeof(s_colors[0]));
    s_colors[MENU_PAL_LED_OFF][INK_CYAN] = normal[INK_CYAN_DARK];
    s_colors[MENU_PAL_LED_OFF][INK_WHITE] = normal[INK_CYAN_DARK];
    for (int p = 0; p < MENU_PAL_COUNT; p++) {
        s_palettes[p] = (gfx_palette_t){s_colors[p], INK_COUNT};
    }

    s_used = 0;
    for (int i = 0; i < MENU_ICON_COUNT; i++) {
        if (k_icon_art[i].rows && !decode(&s_icons[i], k_icon_art[i].rows, k_icon_art[i].n)) {
            return false;
        }
    }
    return decode(&s_led, k_led, 6) && decode(&s_triangle, k_triangle, MENU_ARROW_GLYPH_H) &&
           decode(&s_chevron, k_chevron, MENU_ARROW_GLYPH_H);
}

bool menu_art_init(void)
{
    if (!s_ready) {
        s_ready = load();
    }
    return s_ready;
}

const gfx_palette_t *menu_art_palette(menu_pal_t which)
{
    return &s_palettes[which];
}

const gfx_image_t *menu_art_led(void)
{
    return &s_led;
}

// --- Drawing --------------------------------------------------------------------------------------------------------

void menu_art_fill(menu_canvas_t *c, int x, int y, int w, int h, uint8_t ink)
{
    for (int yy = y < 0 ? 0 : y; yy < y + h && yy < c->h; yy++) {
        for (int xx = x < 0 ? 0 : x; xx < x + w && xx < c->w; xx++) {
            c->px[yy * c->w + xx] = ink;
        }
    }
}

void menu_art_clear(menu_canvas_t *c)
{
    memset(c->px, INK_CLEAR, (size_t)c->w * c->h);
}

// Copies an image's opaque pixels (palette indices as they are; ink != 0: all of them in that colour).
static void blit(menu_canvas_t *c, const gfx_image_t *img, int x0, int y0, bool flip, uint8_t ink)
{
    for (int y = 0; y < img->h; y++) {
        const int cy = y0 + y;
        if (cy < 0 || cy >= c->h) {
            continue;
        }
        for (int x = 0; x < img->w; x++) {
            const int cx = x0 + x;
            const uint8_t v = img->px[y * img->w + (flip ? img->w - 1 - x : x)];
            if (v && cx >= 0 && cx < c->w) {
                c->px[cy * c->w + cx] = ink ? ink : v;
            }
        }
    }
}

void menu_art_button(menu_canvas_t *c, bool sunk, uint8_t face, uint8_t rim, uint8_t glow)
{
    menu_art_clear(c);
    const int top = sunk ? MENU_LEDGE : 0, h = c->h - MENU_LEDGE, w = c->w;
    if (!sunk) { // the ledge: the button's dark side, visible below it
        menu_art_fill(c, 1, h, w - 2, MENU_LEDGE, INK_LEDGE);
    }
    menu_art_fill(c, 1, top, w - 2, h, rim); // rounded: the corner pixels stay clear
    menu_art_fill(c, 0, top + 1, w, h - 2, rim);
    menu_art_fill(c, 1, top + 1, w - 2, h - 2, glow ? glow : face);
    menu_art_fill(c, glow ? 2 : 1, top + (glow ? 2 : 1), w - (glow ? 4 : 2), h - (glow ? 4 : 2), face);
    // Light falls from above: a lit inner top edge; pressed in, the top lies in shadow instead.
    menu_art_fill(c, glow ? 2 : 1, top + (glow ? 2 : 1), w - (glow ? 4 : 2), 1, sunk ? INK_LEDGE : INK_PLATE_LIT);
}

void menu_art_icon_size(menu_icon_t icon, uint8_t arg, int *w, int *h)
{
    (void)arg;
    const buddy_art_t *a = buddy_art_get();
    switch (icon) {
    case MENU_ICON_MOOD:
    case MENU_ICON_MOODS:
    case MENU_ICON_SURPRISE:
    case MENU_ICON_SNEEZE:
    case MENU_ICON_STARE:
    case MENU_ICON_MEDITATE:
    case MENU_ICON_FOCUS:
    case MENU_ICON_PEUKIE:
    case MENU_ICON_ZDMW:
    case MENU_ICON_KITT:
        *w = MENU_FACE_W;
        *h = MENU_FACE_H;
        return;
    case MENU_ICON_MONSTER:
        *w = a->props.can[BUDDY_CAN_LEVELS - 1].w;
        *h = a->props.can[BUDDY_CAN_LEVELS - 1].h;
        return;
    default:
        *w = icon < MENU_ICON_COUNT ? s_icons[icon].w : 0;
        *h = icon < MENU_ICON_COUNT ? s_icons[icon].h : 0;
        return;
    }
}

static void draw_face(menu_canvas_t *c, buddy_face_t face, bool blink, bool kitt, int cx, int cy);

void menu_art_icon(menu_canvas_t *c, menu_icon_t icon, uint8_t arg, int cx, int cy)
{
    switch (icon) {
    case MENU_ICON_MOOD:
        menu_art_face(c, buddy_mood_face((buddy_mood_t)arg), false, cx, cy);
        return;
    case MENU_ICON_MOODS:
        menu_art_face(c, buddy_mood_face(BUDDY_MOOD_HAPPY), false, cx, cy);
        return;
    case MENU_ICON_SURPRISE:
        menu_art_face(c, (buddy_face_t){BUDDY_EYES_WIDE, BUDDY_MOUTH_OPEN, true, false}, false, cx, cy);
        return;
    case MENU_ICON_SNEEZE: // the activities' faces, as the companion's tiles show them too
        menu_art_face(c, buddy_act_face(BUDDY_EVENT_SNEEZE), false, cx, cy);
        return;
    case MENU_ICON_STARE:
        menu_art_face(c, buddy_act_face(BUDDY_EVENT_STARE), false, cx, cy);
        return;
    case MENU_ICON_MEDITATE:
        menu_art_face(c, buddy_act_face(BUDDY_EVENT_MEDITATE), false, cx, cy);
        return;
    case MENU_ICON_FOCUS:
        menu_art_face(c, buddy_act_face(BUDDY_EVENT_FOCUS), false, cx, cy);
        return;
    case MENU_ICON_PEUKIE: // the question offers a smoke: his smoking face
        menu_art_face(c, buddy_mood_face(BUDDY_MOOD_PEUKIE), false, cx, cy);
        return;
    case MENU_ICON_ZDMW: // his reaction to it: exasperated
        menu_art_face(c, (buddy_face_t){BUDDY_EYES_ANGRY, BUDDY_MOUTH_GRIT, true, false}, false, cx, cy);
        return;
    case MENU_ICON_KITT:
        draw_face(c, (buddy_face_t){BUDDY_EYES_CLOSED, BUDDY_MOUTH_NEUTRAL, true, false}, false, true, cx, cy);
        return;
    case MENU_ICON_MONSTER: {
        const gfx_image_t *can = &buddy_art_get()->props.can[BUDDY_CAN_LEVELS - 1];
        blit(c, can, cx - can->w / 2, cy - can->h / 2, false, 0);
        return;
    }
    default:
        if (icon < MENU_ICON_COUNT && s_icons[icon].px) {
            blit(c, &s_icons[icon], cx - s_icons[icon].w / 2, cy - s_icons[icon].h / 2, false, 0);
        }
        return;
    }
}

// Buddy's head as an icon; kitt: KITT's black face screen with the scanner's housing and its light instead of a face.
static void draw_face(menu_canvas_t *c, buddy_face_t face, bool blink, bool kitt, int cx, int cy)
{
    const buddy_art_t *a = buddy_art_get();
    // Assembled as the view does (icons.c exports the same heads), in art coordinates, then cut to the head.
    static uint8_t px[40 * 40];
    menu_canvas_t head = {px, 40, 40};
    menu_art_clear(&head);
    const buddy_eyes_t eyes = blink ? BUDDY_EYES_CLOSED : face.eyes;
    blit(&head, face.lit ? &a->antenna_on : &a->antenna_off, a->antenna_at.x, a->antenna_at.y, false, 0);
    blit(&head, &a->head, a->head_at.x, a->head_at.y, false, 0);
    if (kitt) {
        const gfx_rect_t f = a->face;
        const int mid = f.y + f.h / 2 - 1;
        menu_art_fill(&head, f.x, f.y, f.w, f.h, (uint8_t)(strchr(k_keys, 'D') - k_keys));
        menu_art_fill(&head, f.x + 1, mid, f.w - 2, 2, (uint8_t)(strchr(k_keys, 'r') - k_keys));
        menu_art_fill(&head, f.x + 5, mid, 3, 2, (uint8_t)(strchr(k_keys, 'R') - k_keys));
    } else {
        for (int i = 0; i < 2; i++) {
            blit(&head, &a->eyes[eyes], a->eye_at[i].x, a->eye_at[i].y, i == 1 && a->eye_mirror[eyes], 0);
        }
        blit(&head, &a->mouth[face.mouth], a->mouth_at.x, a->mouth_at.y, false, 0);
    }
    if (face.cig) { // in the corner of his mouth, glowing
        const buddy_point_t m = a->props.hand_mouth;
        blit(&head, &a->props.cig[1][1], m.x - BUDDY_CIG_GRIP, m.y - 1, false, 0);
    }
    int x0 = head.w, y0 = head.h, x1 = -1, y1 = -1;
    for (int y = 0; y < head.h; y++) {
        for (int x = 0; x < head.w; x++) {
            if (px[y * head.w + x]) {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t v = px[(y0 + y) * head.w + x0 + x];
            const int dx = cx - w / 2 + x, dy = cy - h / 2 + y;
            if (v && dx >= 0 && dy >= 0 && dx < c->w && dy < c->h) {
                c->px[dy * c->w + dx] = v;
            }
        }
    }
}

void menu_art_face(menu_canvas_t *c, buddy_face_t face, bool blink, int cx, int cy)
{
    draw_face(c, face, blink, false, cx, cy);
}

int menu_art_text_width(const char *s)
{
    uint8_t cols[192];
    return marquee_text_columns(s, cols, (int)sizeof(cols));
}

int menu_art_text(menu_canvas_t *c, const char *s, int x, int y, uint8_t ink)
{
    uint8_t cols[192];
    const int n = marquee_text_columns(s, cols, (int)sizeof(cols));
    for (int i = 0; i < n; i++) {
        for (int r = 0; r < 7; r++) {
            if ((cols[i] >> r) & 1) {
                menu_art_fill(c, x + i, y + r, 1, 1, ink);
            }
        }
    }
    return x + n;
}

void menu_art_arrow(menu_canvas_t *c, int x, int y, bool right, bool thin, uint8_t ink)
{
    blit(c, thin ? &s_chevron : &s_triangle, x, y, !right, ink);
}

void menu_art_tag(menu_canvas_t *c, int x, int y, bool hot)
{
    const uint8_t ink = hot ? INK_AMBER_HOT : INK_AMBER;
    menu_art_fill(c, x + 1, y, MENU_TAG_W - 2, MENU_TAG_H, ink);
    menu_art_fill(c, x, y + 1, MENU_TAG_W, MENU_TAG_H - 2, ink);
    menu_art_text(c, "SOON", x + (MENU_TAG_W - menu_art_text_width("SOON")) / 2, y + 2, INK_LEDGE);
}
