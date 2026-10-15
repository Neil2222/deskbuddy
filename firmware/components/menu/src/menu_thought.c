#include "menu_thought.h"

#include <math.h>
#include <string.h>

#include "menu_art.h"

#define INK_CHALK INK_WHITE // the plan's lines
#define INK_FAINT INK_GREY  // the thought's dashed line to his head
#define NODE_R 4            // a node: a circle with a letter

// --- Drawing primitives (canvas pixels; everything clips) -----------------------------------------------------------

static void dot(menu_canvas_t *c, int x, int y, uint8_t ink)
{
    menu_art_fill(c, x, y, 1, 1, ink);
}

static void line(menu_canvas_t *c, int x0, int y0, int x1, int y1, uint8_t ink, int dash)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, n = 0;
    for (;;) {
        if (!dash || (n++ / dash) % 2 == 0) {
            dot(c, x0, y0, ink);
        }
        if (x0 == x1 && y0 == y1) {
            return;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// An arrow from (x0, y0) to (x1, y1), its head at the end.
static void arrow(menu_canvas_t *c, int x0, int y0, int x1, int y1)
{
    line(c, x0, y0, x1, y1, INK_CHALK, 0);
    const float len = sqrtf((float)((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
    if (len < 1.0f) {
        return;
    }
    const float ux = (float)(x1 - x0) / len, uy = (float)(y1 - y0) / len;
    for (int side = -1; side <= 1; side += 2) { // two barbs, 3 pixels back and 2 aside
        const int bx = x1 - (int)lroundf(3 * ux - 2 * side * uy), by = y1 - (int)lroundf(3 * uy + 2 * side * ux);
        line(c, x1, y1, bx, by, INK_CHALK, 0);
    }
}

// An arrow between two nodes (centres), from rim to rim.
static void join(menu_canvas_t *c, int x0, int y0, int x1, int y1)
{
    const float len = sqrtf((float)((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
    if (len <= 2 * (NODE_R + 1)) {
        return;
    }
    const float ux = (float)(x1 - x0) / len, uy = (float)(y1 - y0) / len, r = NODE_R + 1.5f;
    arrow(c, x0 + (int)lroundf(ux * r), y0 + (int)lroundf(uy * r), x1 - (int)lroundf(ux * r), y1 - (int)lroundf(uy * r));
}

static void disc(menu_canvas_t *c, int cx, int cy, int r, uint8_t fill, uint8_t rim)
{
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            const int d = x * x + y * y;
            if (d <= r * r + r) {
                dot(c, cx + x, cy + y, d >= (r - 1) * (r - 1) + (r - 1) && rim ? rim : fill);
            }
        }
    }
}

static void node(menu_canvas_t *c, int cx, int cy, char letter)
{
    disc(c, cx, cy, NODE_R, INK_CLEAR, INK_CHALK);
    // Inside: the dark stage shows through; a 3x5 letter in the middle (the sign's font is 5x7, too big here).
    static const char *const k_letters[5][5] = {
        {".#.", "#.#", "###", "#.#", "#.#"}, // A
        {"##.", "#.#", "##.", "#.#", "##."}, // B
        {".##", "#..", "#..", "#..", ".##"}, // C
        {"##.", "#.#", "#.#", "#.#", "##."}, // D
        {"###", "#..", "##.", "#..", "###"}, // E
    };
    if (letter < 'A' || letter > 'E') {
        return;
    }
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 3; x++) {
            if (k_letters[letter - 'A'][y][x] == '#') {
                dot(c, cx - 1 + x, cy - 2 + y, INK_CHALK);
            }
        }
    }
}

static void box(menu_canvas_t *c, int cx, int cy)
{
    menu_art_fill(c, cx - 4, cy - 3, 9, 1, INK_CHALK);
    menu_art_fill(c, cx - 4, cy + 3, 9, 1, INK_CHALK);
    menu_art_fill(c, cx - 4, cy - 3, 1, 7, INK_CHALK);
    menu_art_fill(c, cx + 4, cy - 3, 1, 7, INK_CHALK);
}

static void diamond(menu_canvas_t *c, int cx, int cy)
{
    line(c, cx, cy - 4, cx + 4, cy, INK_CHALK, 0);
    line(c, cx + 4, cy, cx, cy + 4, INK_CHALK, 0);
    line(c, cx, cy + 4, cx - 4, cy, INK_CHALK, 0);
    line(c, cx - 4, cy, cx, cy - 4, INK_CHALK, 0);
}

static void text(menu_canvas_t *c, const char *s, int x, int y)
{
    menu_art_text(c, s, x, y, INK_CHALK);
}

// The little squiggle that says where a voice comes from (from the right, off-stage).
static void squiggle(menu_canvas_t *c, int x, int y)
{
    static const int8_t k_wave[] = {0, -1, -1, 0, 1, 1, 0, -1, -1, 0};
    for (int i = 0; i < (int)sizeof(k_wave); i++) {
        dot(c, x + i, y + k_wave[i], INK_CHALK);
    }
    arrow(c, x + 2, y + 3, x - 2, y + 6);
}

// --- The plan --------------------------------------------------------------------------------------------------------
// Positions relative to his head's centre (cx) and the canvas top, after the comic: A left of his head, B up and to
// the right, C below B, D further right; a flowchart grows to the left; E joins in above.

typedef struct {
    int x, y;
} pt_t;

static void plan(menu_canvas_t *c, int cx, int steps)
{
    const pt_t a = {cx - 34, 26}, b = {cx - 4, 14}, cc = {cx + 16, 32}, d = {cx + 36, 10}, e = {cx - 20, 5};
    const pt_t d1 = {cx - 66, 11}, b1 = {cx - 86, 23}, b2 = {cx - 64, 27}, d2 = {cx - 62, 41};
    if (steps >= 1) {
        line(c, cx - 8, 52, a.x + 2, a.y + NODE_R + 2, INK_FAINT, 2); // the thought, from his head
        node(c, a.x, a.y, 'A');
    }
    if (steps >= 2) {
        join(c, a.x, a.y, b.x, b.y);
        node(c, b.x, b.y, 'B');
    }
    if (steps >= 3) {
        join(c, b.x, b.y, cc.x, cc.y);
        node(c, cc.x, cc.y, 'C');
        join(c, b.x, b.y, d.x, d.y);
        node(c, d.x, d.y, 'D');
    }
    if (steps >= 4) { // the flowchart
        arrow(c, a.x - NODE_R - 1, a.y - 2, d1.x + 4, d1.y + 2);
        diamond(c, d1.x, d1.y);
    }
    if (steps >= 5) {
        arrow(c, d1.x - 4, d1.y, b1.x + 1, b1.y - 4);
        box(c, b1.x, b1.y);
    }
    if (steps >= 6) {
        arrow(c, d1.x, d1.y + 4, b2.x, b2.y - 4);
        box(c, b2.x, b2.y);
        arrow(c, b2.x, b2.y + 3, d2.x, d2.y - 5);
        diamond(c, d2.x, d2.y);
    }
    if (steps >= 7) {
        join(c, b.x, b.y, e.x, e.y);
        node(c, e.x, e.y, 'E');
        join(c, e.x, e.y, a.x, a.y);
    }
    if (steps >= 8) { // loose ends: it keeps going
        arrow(c, b1.x - 4, b1.y + 3, b1.x - 9, b1.y + 13);
        arrow(c, d2.x + 4, d2.y, d2.x + 12, d2.y + 2);
        arrow(c, d.x + NODE_R + 1, d.y, d.x + 14, d.y - 3);
        arrow(c, cc.x + NODE_R + 1, cc.y + 1, cc.x + 13, cc.y + 5);
    }
}

// The plan going up in smoke: clouds where it was, "POOF" in the middle, burst lines around.
static void poof(menu_canvas_t *c, int cx, int frame)
{
    const int x0 = cx - 26, y0 = 22, spread = frame >= 2 ? 6 : 0;
    static const int8_t k_clouds[][3] = {
        {0, 0, 10}, {-16, -6, 8}, {16, -7, 8}, {-28, 4, 7}, {28, 3, 7}, {-8, 10, 7}, {12, 11, 7}, {-40, -8, 5},
        {42, -10, 5}, {-50, 12, 5}, {52, 14, 4}, {0, -14, 6},
    };
    for (size_t i = 0; i < sizeof(k_clouds) / sizeof(k_clouds[0]); i++) {
        const int x = k_clouds[i][0], y = k_clouds[i][1];
        const int ox = x > 0 ? spread : x < 0 ? -spread : 0, oy = y < 0 ? -spread / 2 : spread / 3;
        disc(c, x0 + x + ox, y0 + y + oy, k_clouds[i][2] + (frame >= 2 ? 1 : 0), INK_SILVER, INK_GREY);
    }
    for (int k = 0; k < 12; k++) { // burst lines
        const float ang = (float)k * 0.5236f + 0.2f;
        const float r0 = frame >= 2 ? 26.0f : 20.0f, r1 = r0 + (frame >= 2 ? 10.0f : 6.0f);
        line(c, x0 + (int)lroundf(cosf(ang) * r0 * 1.7f), y0 + (int)lroundf(sinf(ang) * r0 * 0.9f),
             x0 + (int)lroundf(cosf(ang) * r1 * 1.7f), y0 + (int)lroundf(sinf(ang) * r1 * 0.9f), INK_CHALK, 0);
    }
    menu_art_text(c, "POOF", x0 - menu_art_text_width("POOF") / 2, y0 - 3, INK_OUTLINE);
}

// What is left of the plan, drifting about him after the POOF: a few pieces, each on its own slow course.
static void drift(menu_canvas_t *c, int cx, int step)
{
    const int s = step;
    node(c, cx - 70 + s / 2, 10 + s / 4, 'A');
    node(c, cx + 60 - s / 3, 34 - s / 3, 'C');
    box(c, cx - 50 - s / 3, 40 - s / 5);
    diamond(c, cx + 44 + s / 4, 8 + s / 5);
    arrow(c, cx - 24 + s / 3, 2 + s / 4, cx - 14 + s / 3, 7 + s / 4);
    arrow(c, cx + 80 - s / 2, 46 - s / 4, cx + 72 - s / 2, 40 - s / 4);
}

static void draw(menu_thought_t *t, const buddy_thought_t *th)
{
    menu_canvas_t c = {t->px, MENU_THOUGHT_W, MENU_THOUGHT_H};
    menu_art_clear(&c);
    if (th->drift) {
        drift(&c, t->cx, th->drift);
    } else if (th->poof) {
        poof(&c, t->cx, th->poof);
    } else {
        plan(&c, t->cx, th->plan);
    }
    const int right = MENU_THOUGHT_W - 3;
    switch (th->speech) {
    case BUDDY_SPEECH_HEY: { // from off-stage, right of him
        const int w = menu_art_text_width("HEY! DO YOU");
        text(&c, "HEY! DO YOU", right - w, 20);
        text(&c, "HAVE 1 SEC?", right - w, 29);
        squiggle(&c, right - 10, 39);
        break;
    }
    case BUDDY_SPEECH_NEVERMIND: {
        const int w = menu_art_text_width("NEVERMIND");
        text(&c, "NEVERMIND", right - w, 30);
        squiggle(&c, right - 10, 40);
        break;
    }
    case BUDDY_SPEECH_WHAT: // Buddy himself, above his head
        text(&c, "WHAT WAS I DOING?", t->cx - menu_art_text_width("WHAT WAS I DOING?") / 2, 18);
        break;
    default:
        break;
    }
}

// --- The view --------------------------------------------------------------------------------------------------------

bool menu_thought_init(menu_thought_t *t, gfx_scene_t *scene, gfx_rect_t stage, int head_cx, uint8_t *mem,
                       size_t mem_size, int z)
{
    memset(t, 0, sizeof(*t));
    const int w = MENU_THOUGHT_W * MENU_THOUGHT_SCALE, h = MENU_THOUGHT_H * MENU_THOUGHT_SCALE;
    if (!mem || mem_size < MENU_THOUGHT_MEMORY || stage.w < w || stage.h < h || !menu_art_init()) {
        return false;
    }
    t->scene = scene;
    t->px = mem;
    t->image = (gfx_image_t){MENU_THOUGHT_W, MENU_THOUGHT_H, t->px};
    t->sprite = (gfx_sprite_t){.image = &t->image, .palette = menu_art_palette(MENU_PAL_NORMAL),
                               .x = stage.x + (stage.w - w) / 2, .y = stage.y, .scale = MENU_THOUGHT_SCALE,
                               .flags = GFX_SPRITE_HIDDEN};
    t->cx = (head_cx - t->sprite.x) / MENU_THOUGHT_SCALE;
    return gfx_scene_add(scene, &t->sprite, z);
}

void menu_thought_apply(menu_thought_t *t, const buddy_thought_t *thought)
{
    const bool none = !thought || (!thought->plan && !thought->poof && !thought->speech && !thought->drift);
    if (none) {
        if (t->visible) {
            t->sprite.flags = (uint8_t)(t->sprite.flags | GFX_SPRITE_HIDDEN);
            t->visible = false;
        }
        return;
    }
    if (t->visible && memcmp(thought, &t->shown, sizeof(t->shown)) == 0) {
        return;
    }
    t->shown = *thought;
    draw(t, thought);
    t->sprite.flags = (uint8_t)(t->sprite.flags & ~GFX_SPRITE_HIDDEN);
    t->visible = true;
    gfx_scene_invalidate(t->scene, gfx_sprite_bounds(&t->sprite)); // drawn in place: the renderer must be told
}
