#include "menu_ask.h"

#include <stdlib.h>
#include <string.h>

#include "menu_art.h"

#define MARGIN 36    // screen pixels between the buttons and the stage's edges
#define TOUCH_PAD 12 // fingers are wider than outlines
#define BAR_W (MENU_ASK_TIMER_W - 2)

static void set_hidden(gfx_sprite_t *sp, bool hidden)
{
    sp->flags = (uint8_t)(hidden ? sp->flags | GFX_SPRITE_HIDDEN : sp->flags & ~GFX_SPRITE_HIDDEN);
}

// Drawn in place: show it and have the renderer redraw it (gfx_scene_collect() only notices the sprite fields).
static void redrawn(const menu_ask_t *a, gfx_sprite_t *sp)
{
    set_hidden(sp, false);
    gfx_scene_invalidate(a->scene, gfx_sprite_bounds(sp));
}

// A speech bubble with the question, its tail pointing down at his head.
static void draw_bubble(menu_ask_t *a)
{
    menu_canvas_t c = {a->bubble_px, MENU_ASK_BUBBLE_W, MENU_ASK_BUBBLE_H};
    const int body = MENU_ASK_BUBBLE_H - 5, mid = MENU_ASK_BUBBLE_W / 2;
    menu_art_clear(&c);
    menu_art_fill(&c, 1, 0, MENU_ASK_BUBBLE_W - 2, body, INK_OUTLINE); // rounded corners: those pixels stay clear
    menu_art_fill(&c, 0, 1, MENU_ASK_BUBBLE_W, body - 2, INK_OUTLINE);
    menu_art_fill(&c, 1, 1, MENU_ASK_BUBBLE_W - 2, body - 2, INK_WHITE);
    for (int r = 0; r < 5; r++) { // the tail
        const int half = 4 - r;
        menu_art_fill(&c, mid - half, body - 1 + r, 2 * half + 1, 1, INK_WHITE);
        menu_art_fill(&c, mid - half, body - 1 + r, 1, 1, INK_OUTLINE);
        menu_art_fill(&c, mid + half, body - 1 + r, 1, 1, INK_OUTLINE);
    }
    menu_art_text(&c, a->question, (MENU_ASK_BUBBLE_W - menu_art_text_width(a->question)) / 2, (body - 7) / 2,
                  INK_OUTLINE);
    redrawn(a, &a->bubble);
}

static void draw_button(menu_ask_t *a, int i)
{
    menu_canvas_t c = {a->button_px[i], MENU_ASK_BUTTON_W, MENU_ASK_BUTTON_H};
    const bool sunk = a->pressed == i;
    const uint8_t ink = i == MENU_ANSWER_YES ? INK_GREEN : INK_RED;
    const char *label = i == MENU_ANSWER_YES ? "JA" : "NEE";
    menu_art_button(&c, sunk, sunk ? INK_PLATE_DOWN : INK_PLATE, ink, 0);
    menu_art_text(&c, label, (MENU_ASK_BUTTON_W - menu_art_text_width(label)) / 2,
                  (MENU_ASK_BUTTON_H - MENU_LEDGE - 7) / 2 + (sunk ? MENU_LEDGE : 0), ink);
    redrawn(a, &a->button[i]);
}

static void draw_bar(menu_ask_t *a, int filled, bool low)
{
    menu_canvas_t c = {a->timer_px, MENU_ASK_TIMER_W, MENU_ASK_TIMER_H};
    menu_art_fill(&c, 0, 0, MENU_ASK_TIMER_W, MENU_ASK_TIMER_H, INK_EDGE);
    menu_art_fill(&c, 1, 1, BAR_W, MENU_ASK_TIMER_H - 2, INK_LEDGE);
    menu_art_fill(&c, 1, 1, filled, MENU_ASK_TIMER_H - 2, low ? INK_RED : INK_AMBER);
    redrawn(a, &a->timer);
}

bool menu_ask_init(menu_ask_t *a, gfx_scene_t *scene, gfx_rect_t stage, int head_top, const char *question, int z)
{
    memset(a, 0, sizeof(*a));
    const int bw = MENU_ASK_BUTTON_W * MENU_ASK_SCALE, bh = MENU_ASK_BUTTON_H * MENU_ASK_SCALE;
    const int qw = MENU_ASK_BUBBLE_W * MENU_ASK_SCALE, qh = MENU_ASK_BUBBLE_H * MENU_ASK_SCALE;
    const int tw = MENU_ASK_TIMER_W * MENU_ASK_SCALE, th = MENU_ASK_TIMER_H * MENU_ASK_SCALE;
    if (!menu_art_init() || !question || menu_art_text_width(question) > MENU_ASK_BUBBLE_W - 6 ||
        stage.w < 2 * (MARGIN + bw) + tw || head_top - qh < stage.y) {
        return false;
    }
    a->scene = scene;
    a->question = question;
    a->target = a->pressed = MENU_ANSWER_NONE;
    a->shown_pressed = -2;
    a->shown_bar = -1;
    a->bubble_img = (gfx_image_t){MENU_ASK_BUBBLE_W, MENU_ASK_BUBBLE_H, a->bubble_px};
    a->timer_img = (gfx_image_t){MENU_ASK_TIMER_W, MENU_ASK_TIMER_H, a->timer_px};
    const gfx_palette_t *pal = menu_art_palette(MENU_PAL_NORMAL);
    const gfx_sprite_t blank = {.palette = pal, .scale = MENU_ASK_SCALE, .flags = GFX_SPRITE_HIDDEN};
    const int bottom = stage.y + stage.h - MARGIN * 2 / 3 - bh;
    a->bubble = blank;
    a->bubble.image = &a->bubble_img;
    a->bubble.x = stage.x + (stage.w - qw) / 2;
    a->bubble.y = head_top - qh;
    a->timer = blank;
    a->timer.image = &a->timer_img;
    a->timer.x = stage.x + (stage.w - tw) / 2;
    a->timer.y = bottom + (bh - th) / 2;
    bool ok = gfx_scene_add(scene, &a->bubble, z) && gfx_scene_add(scene, &a->timer, z);
    for (int i = 0; i < 2; i++) {
        a->button_img[i] = (gfx_image_t){MENU_ASK_BUTTON_W, MENU_ASK_BUTTON_H, a->button_px[i]};
        a->button[i] = blank;
        a->button[i].image = &a->button_img[i];
        a->button[i].x = i == MENU_ANSWER_NO ? stage.x + MARGIN : stage.x + stage.w - MARGIN - bw;
        a->button[i].y = bottom;
        ok = ok && gfx_scene_add(scene, &a->button[i], z);
    }
    return ok;
}

static void cancel(menu_ask_t *a)
{
    a->down = false;
    a->target = a->pressed = MENU_ANSWER_NONE;
}

uint32_t menu_ask_apply(menu_ask_t *a, uint32_t left_ms, uint32_t total_ms)
{
    if (!left_ms || !total_ms) {
        if (a->visible) {
            set_hidden(&a->bubble, true);
            set_hidden(&a->button[0], true);
            set_hidden(&a->button[1], true);
            set_hidden(&a->timer, true);
            a->visible = false;
            a->shown_pressed = -2;
            a->shown_bar = -1;
            cancel(a);
        }
        return UINT32_MAX;
    }
    if (!a->visible) {
        a->visible = true;
        draw_bubble(a);
    }
    if (a->pressed != a->shown_pressed) {
        for (int i = 0; i < 2; i++) {
            if (a->shown_pressed == -2 || i == a->pressed || i == a->shown_pressed) {
                draw_button(a, i);
            }
        }
        a->shown_pressed = a->pressed;
    }
    left_ms = left_ms < total_ms ? left_ms : total_ms;
    const int filled = (int)(((uint64_t)BAR_W * left_ms + total_ms - 1) / total_ms); // full at the start
    const bool low = left_ms <= MENU_ASK_LOW_MS;
    if (filled * 2 + low != a->shown_bar) {
        a->shown_bar = filled * 2 + low;
        draw_bar(a, filled, low);
    }
    // Next change: the bar loses a pixel when left_ms reaches (filled - 1) * total / BAR_W.
    const uint32_t step = (uint32_t)((uint64_t)(filled - 1) * total_ms / BAR_W);
    uint32_t next = left_ms > step ? left_ms - step : 1;
    if (!low && left_ms - MENU_ASK_LOW_MS < next) {
        next = left_ms - MENU_ASK_LOW_MS;
    }
    return next ? next : 1;
}

static int hit(const menu_ask_t *a, int x, int y)
{
    for (int i = 0; i < 2; i++) {
        gfx_rect_t r = gfx_sprite_bounds(&a->button[i]);
        r = (gfx_rect_t){r.x - TOUCH_PAD, r.y - TOUCH_PAD, r.w + 2 * TOUCH_PAD, r.h + 2 * TOUCH_PAD};
        if (gfx_rect_contains_point(r, x, y)) {
            return i;
        }
    }
    return MENU_ANSWER_NONE;
}

menu_answer_t menu_ask_touch(menu_ask_t *a, menu_touch_t type, int x, int y)
{
    if (!a->visible) {
        cancel(a);
        return MENU_ANSWER_NONE;
    }
    if (type == MENU_TOUCH_DOWN) {
        cancel(a); // a down without the previous up: start over
        const int h = hit(a, x, y);
        if (h != MENU_ANSWER_NONE) {
            a->down = true;
            a->down_x = (int16_t)x;
            a->down_y = (int16_t)y;
            a->target = a->pressed = (int8_t)h;
        }
        return MENU_ANSWER_NONE;
    }
    if (!a->down) {
        return MENU_ANSWER_NONE;
    }
    if (abs(x - a->down_x) > MENU_SLOP_PX || abs(y - a->down_y) > MENU_SLOP_PX) {
        a->target = a->pressed = MENU_ANSWER_NONE; // slid away: no answer
    }
    if (type == MENU_TOUCH_MOVE) {
        return MENU_ANSWER_NONE;
    }
    const int target = a->target;
    cancel(a);
    return target != MENU_ANSWER_NONE && hit(a, x, y) == target ? (menu_answer_t)target : MENU_ANSWER_NONE;
}

bool menu_ask_holding(const menu_ask_t *a)
{
    return a->down;
}
