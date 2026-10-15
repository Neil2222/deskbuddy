#include "menu_view.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "menu_art.h"

#define TITLE_X 48 // menu pixels: between Back and the face
#define TITLE_Y 12
#define FACE_X 146
#define FACE_Y 7
#define BLINK_EVERY_MS 4200 // the face blinks this often...
#define BLINK_MS 150        // ...for this long
#define LED_ON_MS 900       // the active light: a slow heartbeat
#define LED_OFF_MS 300
#define NOPE_STEP_MS 150    // the SOON tag flashes in steps of this

// How a slot looks (bits).
enum {
    LOOK_SUNK = 1,   // pressed in (or popping in)
    LOOK_ACTIVE = 2, // Buddy's current mood
    LOOK_FLASH = 4,  // just tapped
    LOOK_DIM = 8,    // planned or busy
    LOOK_SOON = 16,  // with the SOON tag
    LOOK_HOT = 32,   // the tag lit up
};

static uint32_t sooner(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static void set_hidden(gfx_sprite_t *sp, bool hidden)
{
    sp->flags = (uint8_t)(hidden ? sp->flags | GFX_SPRITE_HIDDEN : sp->flags & ~GFX_SPRITE_HIDDEN);
}

static void place(const menu_view_t *v, gfx_sprite_t *sp, int x, int y)
{
    sp->x = v->x0 + x * MENU_SCALE;
    sp->y = v->y0 + y * MENU_SCALE;
}

// After drawing into a sprite's image in place: shows it and has the renderer redraw it (gfx_scene_collect() only
// notices changes to the sprite fields).
static void redrawn(const menu_view_t *v, gfx_sprite_t *sp)
{
    set_hidden(sp, false);
    gfx_scene_invalidate(v->scene, gfx_sprite_bounds(sp));
}

// Copies the opaque pixels of src into dst at (x, y).
static void stamp(menu_canvas_t *dst, const menu_canvas_t *src, int x, int y)
{
    for (int sy = 0; sy < src->h; sy++) {
        for (int sx = 0; sx < src->w; sx++) {
            const uint8_t p = src->px[sy * src->w + sx];
            if (p && x + sx >= 0 && x + sx < dst->w && y + sy >= 0 && y + sy < dst->h) {
                dst->px[(y + sy) * dst->w + x + sx] = p;
            }
        }
    }
}

// --- Slots ----------------------------------------------------------------------------------------------------------

static uint8_t look_of(const menu_t *m, const menu_status_t *st, int i, const menu_item_t *it, uint32_t now,
                       uint32_t *next)
{
    uint8_t look = 0;
    const uint32_t since = now - (m->opened + (uint32_t)i * MENU_FLIP_MS); // since the tile flipped in
    if (since < MENU_POP_MS) {
        look |= LOOK_SUNK;
        *next = sooner(*next, MENU_POP_MS - since);
    }
    switch (menu_item_avail(it, st)) {
    case MENU_ITEM_SOON:
        look |= LOOK_DIM | LOOK_SOON;
        if (m->nope == i && now - m->nope_at < MENU_NOPE_MS) {
            const uint32_t t = now - m->nope_at;
            look |= (t / NOPE_STEP_MS) % 2 == 0 ? LOOK_HOT : 0;
            *next = sooner(*next, NOPE_STEP_MS - t % NOPE_STEP_MS);
        }
        return look;
    case MENU_ITEM_BUSY:
        return look | LOOK_DIM;
    default:
        break;
    }
    if (m->pressed == i) {
        look |= LOOK_SUNK;
    }
    if (m->flash == i && now - m->flash_at < MENU_FLASH_MS) {
        look |= LOOK_FLASH;
        *next = sooner(*next, MENU_FLASH_MS - (now - m->flash_at));
    }
    if (menu_item_active(it, st)) {
        look |= LOOK_ACTIVE;
    }
    return look;
}

static void draw_slot(menu_view_t *v, int i, const menu_item_t *it, menu_layout_t layout, uint8_t look)
{
    const gfx_rect_t r = menu_slot_rect(layout, i);
    v->slot_img[i] = (gfx_image_t){(uint16_t)r.w, (uint16_t)r.h, v->slot_px[i]};
    menu_canvas_t c = {v->slot_px[i], r.w, r.h};
    const bool sunk = look & LOOK_SUNK, active = look & LOOK_ACTIVE, flash = look & LOOK_FLASH;
    menu_art_button(&c, sunk, flash ? INK_PLATE_LIT : sunk ? INK_PLATE_DOWN : INK_PLATE,
                    flash ? INK_WHITE : active ? INK_CYAN : INK_EDGE, active ? INK_CYAN_DARK : 0);
    const int dy = sunk ? MENU_LEDGE : 0, body = r.h - MENU_LEDGE;
    const uint8_t ink = flash ? INK_WHITE : active ? INK_CYAN : INK_SILVER;
    if (layout == MENU_LIST) {
        menu_art_icon(&c, (menu_icon_t)it->icon, it->arg, 22, body / 2 + dy);
        menu_art_text(&c, it->label, 44, (body - 7) / 2 + dy, ink);
        if (look & LOOK_SOON) {
            menu_art_tag(&c, r.w - MENU_TAG_W - 6, (body - MENU_TAG_H) / 2 + dy, look & LOOK_HOT);
        } else {
            menu_art_arrow(&c, r.w - 12, (body - MENU_ARROW_GLYPH_H) / 2 + dy, true, true, INK_GREY);
        }
    } else {
        menu_art_icon(&c, (menu_icon_t)it->icon, it->arg, r.w / 2, 21 + dy);
        menu_art_text(&c, it->label, (r.w - menu_art_text_width(it->label)) / 2, 41 + dy, ink);
        if (look & LOOK_SOON) {
            menu_art_tag(&c, r.w - MENU_TAG_W - 3, 3 + dy, look & LOOK_HOT);
        }
    }
    gfx_sprite_t *sp = &v->slot[i];
    sp->image = &v->slot_img[i];
    sp->palette = menu_art_palette(look & LOOK_DIM ? MENU_PAL_DIM : MENU_PAL_NORMAL);
    place(v, sp, r.x, r.y);
    redrawn(v, sp);
}

static void set_slot(menu_view_t *v, int i, const menu_item_t *it, menu_layout_t layout, uint8_t look)
{
    menu_slot_look_t *s = &v->shown[i];
    if (s->item == it && (!it || (s->layout == layout && s->look == look))) {
        return;
    }
    *s = (menu_slot_look_t){it, (uint8_t)layout, look};
    if (it) {
        draw_slot(v, i, it, layout, look);
    } else {
        set_hidden(&v->slot[i], true);
    }
}

// --- Header and footer ----------------------------------------------------------------------------------------------

static void draw_back(menu_view_t *v, bool sunk)
{
    menu_canvas_t c = {v->back_px, MENU_BACK_W, MENU_BACK_H};
    menu_art_button(&c, sunk, sunk ? INK_PLATE_DOWN : INK_PLATE, INK_EDGE, 0);
    const int dy = sunk ? MENU_LEDGE : 0, body = MENU_BACK_H - MENU_LEDGE;
    menu_art_arrow(&c, 5, (body - MENU_ARROW_GLYPH_H) / 2 + dy, false, false, INK_CYAN);
    menu_art_text(&c, "BACK", 13, (body - 7) / 2 + dy, INK_SILVER);
    redrawn(v, &v->back);
}

static void draw_title(menu_view_t *v, const char *title)
{
    menu_canvas_t c = {v->title_px, MENU_TITLE_W, MENU_TITLE_H};
    menu_art_clear(&c);
    const int x = (MENU_TITLE_W - menu_art_text_width(title)) / 2;
    menu_art_text(&c, title, x, 2, INK_AMBER_DARK); // a glow beneath, like the LED sign
    menu_art_text(&c, title, x, 1, INK_AMBER);
    redrawn(v, &v->title);
}

static void draw_face(menu_view_t *v, int face_id)
{
    const buddy_face_t face = face_id == BUDDY_MOOD_COUNT
                                  ? (buddy_face_t){BUDDY_EYES_BOLT, BUDDY_MOUTH_GRIN, true, false} // charging
                                  : buddy_mood_face((buddy_mood_t)face_id);
    for (int blink = 0; blink < 2; blink++) {
        menu_canvas_t c = {v->face_px[blink], MENU_FACE_W, MENU_FACE_H};
        menu_art_clear(&c);
        menu_art_face(&c, face, blink, MENU_FACE_W / 2, MENU_FACE_H / 2);
    }
    redrawn(v, &v->face);
}

// A page arrow on its own small button, into the footer canvas.
static void arrow_button(menu_canvas_t *footer, int x, bool right, bool sunk)
{
    static uint8_t px[MENU_ARROW_W * MENU_FOOTER_H];
    menu_canvas_t c = {px, MENU_ARROW_W, MENU_FOOTER_H};
    menu_art_button(&c, sunk, sunk ? INK_PLATE_DOWN : INK_PLATE, INK_EDGE, 0);
    const int body = MENU_FOOTER_H - MENU_LEDGE;
    menu_art_arrow(&c, (MENU_ARROW_W - MENU_ARROW_GLYPH_W) / 2, (body - MENU_ARROW_GLYPH_H) / 2 + (sunk ? MENU_LEDGE : 0),
                   right, false, INK_CYAN);
    stamp(footer, &c, x, 0);
}

static void draw_footer(menu_view_t *v, const menu_t *m, const menu_status_t *st, int pages)
{
    menu_canvas_t c = {v->footer_px, MENU_FOOTER_W, MENU_FOOTER_H};
    menu_art_clear(&c);
    const int y = (MENU_FOOTER_H - MENU_LEDGE - 7) / 2;
    if (pages > 1) {
        char text[24];
        snprintf(text, sizeof(text), "%d/%d", m->page + 1, pages);
        menu_art_text(&c, text, (MENU_FOOTER_W - menu_art_text_width(text)) / 2, y, INK_AMBER);
        if (m->page > 0) {
            arrow_button(&c, 0, false, m->pressed == MENU_HIT_PREV);
        }
        if (m->page + 1 < pages) {
            arrow_button(&c, MENU_FOOTER_W - MENU_ARROW_W, true, m->pressed == MENU_HIT_NEXT);
        }
    } else { // what Buddy is up to
        char word[16];
        const char *name = st->monster ? "charging!" : buddy_mood_name(st->mood);
        size_t n = 0;
        for (; name[n] && n + 1 < sizeof(word); n++) {
            word[n] = (char)toupper((unsigned char)name[n]);
        }
        word[n] = 0;
        const int w = menu_art_text_width("BUDDY IS") + 4 + menu_art_text_width(word);
        const int x = menu_art_text(&c, "BUDDY IS", (MENU_FOOTER_W - w) / 2, y, INK_GREY) + 4;
        menu_art_text(&c, word, x, y, st->monster ? INK_AMBER : INK_CYAN);
    }
    redrawn(v, &v->footer);
}

// --- The view -------------------------------------------------------------------------------------------------------

static void forget(menu_view_t *v)
{
    memset(v->shown, 0, sizeof(v->shown));
    v->back_look = v->title_shown = v->face_shown = -1;
    v->footer_shown = 0;
}

bool menu_view_init(menu_view_t *v, gfx_scene_t *scene, const menu_t *m, uint8_t *mem, size_t mem_size, int z)
{
    memset(v, 0, sizeof(*v));
    if (!mem || mem_size < MENU_VIEW_MEMORY || !menu_art_init()) {
        return false;
    }
    v->scene = scene;
    v->x0 = m->area.x;
    v->y0 = m->area.y;
    uint8_t *p = mem;
    for (int i = 0; i < MENU_SLOTS; i++) { // the first slots also hold Home's wide rows
        v->slot_px[i] = p;
        p += i < MENU_LIST_ROWS ? MENU_ROW_W * MENU_ROW_H : MENU_TILE_W * MENU_TILE_H;
    }
    v->back_px = p;
    p += MENU_BACK_W * MENU_BACK_H;
    v->title_px = p;
    p += MENU_TITLE_W * MENU_TITLE_H;
    for (int i = 0; i < 2; i++) {
        v->face_px[i] = p;
        p += MENU_FACE_W * MENU_FACE_H;
        v->face_img[i] = (gfx_image_t){MENU_FACE_W, MENU_FACE_H, v->face_px[i]};
    }
    v->footer_px = p;
    v->back_img = (gfx_image_t){MENU_BACK_W, MENU_BACK_H, v->back_px};
    v->title_img = (gfx_image_t){MENU_TITLE_W, MENU_TITLE_H, v->title_px};
    v->footer_img = (gfx_image_t){MENU_FOOTER_W, MENU_FOOTER_H, v->footer_px};

    const gfx_palette_t *pal = menu_art_palette(MENU_PAL_NORMAL);
    const gfx_sprite_t blank = {.palette = pal, .scale = MENU_SCALE, .flags = GFX_SPRITE_HIDDEN};
    bool ok = true;
    for (int i = 0; i < MENU_SLOTS; i++) {
        v->slot[i] = blank;
        ok = ok && gfx_scene_add(scene, &v->slot[i], z);
    }
    const gfx_rect_t back = menu_back_rect(), footer = menu_footer_rect();
    gfx_sprite_t *fixed[] = {&v->back, &v->title, &v->face, &v->footer, &v->led};
    const gfx_image_t *images[] = {&v->back_img, &v->title_img, &v->face_img[0], &v->footer_img, menu_art_led()};
    const int at[][2] = {{back.x, back.y}, {TITLE_X, TITLE_Y}, {FACE_X, FACE_Y}, {footer.x, footer.y}, {0, 0}};
    for (int i = 0; i < 5; i++) {
        *fixed[i] = blank;
        fixed[i]->image = images[i];
        place(v, fixed[i], at[i][0], at[i][1]);
        ok = ok && gfx_scene_add(scene, fixed[i], i == 4 ? z + 1 : z); // the light sits on a tile
    }
    forget(v);
    v->visible = true;
    return ok;
}

void menu_view_show(menu_view_t *v, bool visible)
{
    v->visible = visible;
    v->closing = false;
    if (!visible) {
        for (int i = 0; i < MENU_SLOTS; i++) {
            set_hidden(&v->slot[i], true);
        }
        set_hidden(&v->back, true);
        set_hidden(&v->title, true);
        set_hidden(&v->face, true);
        set_hidden(&v->footer, true);
        set_hidden(&v->led, true);
    }
    forget(v);
}

void menu_view_close(menu_view_t *v, uint32_t now)
{
    if (v->visible && !v->closing) {
        v->closing = true;
        v->close_at = now;
    }
}

// Folding away: the buttons vanish one by one, then the rest. Returns ms until the next one goes.
static uint32_t apply_close(menu_view_t *v, uint32_t now)
{
    const uint32_t t = now - v->close_at;
    set_hidden(&v->led, true);
    for (int i = 0; i < MENU_SLOTS; i++) {
        if (t < (uint32_t)i * MENU_FLIP_MS) {
            return (uint32_t)i * MENU_FLIP_MS - t;
        }
        set_slot(v, i, NULL, MENU_GRID, 0);
    }
    if (t < MENU_SLOTS * MENU_FLIP_MS) {
        return MENU_SLOTS * MENU_FLIP_MS - t;
    }
    menu_view_show(v, false);
    return UINT32_MAX;
}

uint32_t menu_view_apply(menu_view_t *v, const menu_t *m, const menu_status_t *st, uint32_t now)
{
    if (!v->visible) {
        return UINT32_MAX;
    }
    if (v->closing) {
        return apply_close(v, now);
    }
    uint32_t next = UINT32_MAX;
    const menu_item_t *items[MENU_SLOTS];
    int pages;
    const int n = menu_page_items(m, st, items, &pages);
    const menu_layout_t layout = (menu_layout_t)m->def->categories[m->category].layout;

    // Slots whose turn has come show the page; the others keep the old page's tile until theirs.
    gfx_rect_t fresh[MENU_SLOTS];
    int n_fresh = 0, active = -1;
    for (int i = 0; i < MENU_SLOTS; i++) {
        if (!menu_slot_shown(m, i, now)) {
            next = sooner(next, m->opened + (uint32_t)i * MENU_FLIP_MS - now);
            continue;
        }
        const menu_item_t *it = i < n ? items[i] : NULL;
        const uint8_t look = it ? look_of(m, st, i, it, now, &next) : 0;
        set_slot(v, i, it, layout, look);
        if (it) {
            fresh[n_fresh++] = menu_slot_rect(layout, i);
            active = (look & LOOK_ACTIVE) ? i : active;
        }
    }
    // An old tile still waiting for its turn makes way where a new one has appeared (list rows and grid tiles
    // overlap), so the two never show on top of each other.
    for (int i = 0; i < MENU_SLOTS; i++) {
        if (menu_slot_shown(m, i, now) || !v->shown[i].item) {
            continue;
        }
        const gfx_rect_t old = menu_slot_rect((menu_layout_t)v->shown[i].layout, i);
        for (int j = 0; j < n_fresh; j++) {
            if (gfx_rect_overlaps(old, fresh[j])) {
                set_slot(v, i, NULL, layout, 0);
                break;
            }
        }
    }

    // The active light, in the tile's top right corner.
    if (active >= 0) {
        const gfx_rect_t r = menu_slot_rect(layout, active);
        place(v, &v->led, r.x + r.w - 10, r.y + 4 + ((v->shown[active].look & LOOK_SUNK) ? MENU_LEDGE : 0));
        const uint32_t phase = now % (LED_ON_MS + LED_OFF_MS);
        v->led.palette = menu_art_palette(phase < LED_ON_MS ? MENU_PAL_LED_ON : MENU_PAL_LED_OFF);
        set_hidden(&v->led, false);
        next = sooner(next, phase < LED_ON_MS ? LED_ON_MS - phase : LED_ON_MS + LED_OFF_MS - phase);
    } else {
        set_hidden(&v->led, true);
    }

    // Header.
    const int back = m->category == MENU_HOME ? -1 : m->pressed == MENU_HIT_BACK;
    if (back != v->back_look) {
        v->back_look = back;
        if (back < 0) {
            set_hidden(&v->back, true);
        } else {
            draw_back(v, back);
        }
    }
    if (m->category != v->title_shown) {
        v->title_shown = m->category;
        draw_title(v, m->def->categories[m->category].title);
    }
    const int face = st->monster ? BUDDY_MOOD_COUNT : (int)st->mood;
    if (face != v->face_shown) {
        v->face_shown = face;
        draw_face(v, face);
    }
    const uint32_t blink = now % BLINK_EVERY_MS;
    v->face.image = &v->face_img[blink < BLINK_MS];
    next = sooner(next, blink < BLINK_MS ? BLINK_MS - blink : BLINK_EVERY_MS - blink);

    // Footer: a summary of what it shows, so it is only redrawn when that changes.
    const uint32_t footer = 1u | (pages > 1 ? 2u : 0u) | (uint32_t)m->page << 2 |
                            (m->pressed == MENU_HIT_PREV ? 1u << 10 : 0u) | (m->pressed == MENU_HIT_NEXT ? 1u << 11 : 0u) |
                            (pages > 1 ? 0u : (uint32_t)st->mood << 12 | (st->monster ? 1u << 20 : 0u)) |
                            (uint32_t)pages << 21;
    if (footer != v->footer_shown) {
        v->footer_shown = footer;
        draw_footer(v, m, st, pages);
    }
    return next;
}
