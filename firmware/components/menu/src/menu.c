#include "menu.h"

#include <stdlib.h>
#include <string.h>

#include "buddy_brain.h" // BUDDY_EVENT_KITT
#include "menu_ttt.h"

#define MARGIN 5     // menu pixels around and between the buttons
#define HEADER_H 38  // the grid and list start below the header
#define FOOTER_Y 272
#define TOUCH_PAD 2  // buttons take touches this far beyond their edges (menu pixels; gaps are 5)

menu_avail_t menu_item_avail(const menu_item_t *it, const menu_status_t *st)
{
    if (it->action == MENU_DO_OFFICE && !st->office) {
        return MENU_ITEM_HIDDEN;
    }
    // Clock faces: only those that exist.
    if (it->action == MENU_DO_FACE && (!st->office || !(st->faces & (1u << it->arg)))) {
        return MENU_ITEM_HIDDEN;
    }
    // The PDC Manager integration exists only while the companion offers it: no dead buttons without it.
    if ((it->action == MENU_DO_PDC || (it->action == MENU_DO_OPEN && it->arg == MENU_PDC)) && !st->pdc) {
        return MENU_ITEM_HIDDEN;
    }
    if (it->soon) {
        return MENU_ITEM_SOON;
    }
    if (it->action == MENU_DO_EVENT && it->arg == BUDDY_EVENT_KITT && st->kitt) {
        return MENU_ITEM_READY; // marked as what he is; tapping it again changes nothing
    }
    if ((it->action == MENU_DO_EVENT || it->action == MENU_DO_PDC) && st->busy) {
        return MENU_ITEM_BUSY;
    }
    if (it->action == MENU_DO_OPEN && it->arg == MENU_TTT && st->busy) {
        return MENU_ITEM_BUSY; // he plays once the show is over
    }
    if (it->action == MENU_DO_PDC && !st->pdc_ready) {
        return MENU_ITEM_BUSY; // a click is under way, or just went out (cooldown)
    }
    return MENU_ITEM_READY;
}

bool menu_item_active(const menu_item_t *it, const menu_status_t *st)
{
    if (it->action == MENU_DO_EVENT && it->arg == BUDDY_EVENT_KITT) {
        return st->kitt;
    }
    return (it->action == MENU_DO_MOOD && it->arg == st->mood && !st->kitt) ||
           (it->action == MENU_DO_FACE && it->arg == st->face);
}

gfx_rect_t menu_slot_rect(menu_layout_t layout, int slot)
{
    if (layout == MENU_LIST) {
        return (gfx_rect_t){MARGIN, HEADER_H + slot * (MENU_ROW_H + MARGIN), MENU_ROW_W, MENU_ROW_H};
    }
    if (layout == MENU_GAME) { // the game's pieces in the first slots (menu_ttt.h)
        return slot < 3   ? (gfx_rect_t){MARGIN, MENU_TTT_TOP + slot * (MENU_TTT_CELL + MENU_TTT_GAP), MENU_TTT_ROW_W,
                                         MENU_TTT_CELL}
               : slot == 3 ? menu_ttt_strip_rect()
                           : (gfx_rect_t){0, 0, 0, 0};
    }
    return (gfx_rect_t){MARGIN + (slot % 2) * (MENU_TILE_W + MARGIN),
                        HEADER_H + (slot / 2) * (MENU_TILE_H + MARGIN), MENU_TILE_W, MENU_TILE_H};
}

gfx_rect_t menu_back_rect(void)
{
    return (gfx_rect_t){MARGIN, MARGIN, MENU_BACK_W, MENU_BACK_H};
}

gfx_rect_t menu_footer_rect(void)
{
    return (gfx_rect_t){MARGIN, FOOTER_Y, MENU_FOOTER_W, MENU_FOOTER_H};
}

gfx_rect_t menu_head_rect(void)
{
    return (gfx_rect_t){140, 0, MENU_W - 140, HEADER_H - 2}; // the head is drawn at 146..169 x 7..27
}

// The CLOCK FACES page has something to offer: Clock Mode and more than one face.
static bool faces_to_choose(const menu_status_t *st)
{
    return st->office && (st->faces & (st->faces - 1)) != 0;
}

static int slots_of(menu_layout_t layout)
{
    return layout == MENU_LIST ? MENU_LIST_ROWS : MENU_SLOTS;
}

static menu_layout_t layout_of(const menu_t *m)
{
    return (menu_layout_t)m->def->categories[m->category].layout;
}

bool menu_init(menu_t *m, const menu_def_t *def, gfx_rect_t area, uint32_t now)
{
    memset(m, 0, sizeof(*m));
    if (area.w < MENU_W * MENU_SCALE || area.h < MENU_H * MENU_SCALE) {
        return false;
    }
    m->def = def;
    m->area = (gfx_rect_t){area.x + (area.w - MENU_W * MENU_SCALE) / 2, area.y + (area.h - MENU_H * MENU_SCALE) / 2,
                           MENU_W * MENU_SCALE, MENU_H * MENU_SCALE};
    m->target = m->pressed = m->flash = m->nope = MENU_HIT_NONE;
    menu_open(m, MENU_HOME, now);
    return true;
}

static void show_page(menu_t *m, int category, int page, uint32_t now)
{
    m->category = (uint8_t)category;
    m->page = (uint8_t)page;
    m->opened = now;
    m->flash = m->nope = MENU_HIT_NONE; // feedback belongs to the tiles that were there
}

void menu_open(menu_t *m, menu_category_t category, uint32_t now)
{
    show_page(m, category < MENU_CATEGORY_COUNT ? category : MENU_HOME, 0, now);
}

void menu_reveal(menu_t *m, uint32_t now)
{
    menu_cancel(m);
    show_page(m, m->category, m->page, now);
}

int menu_page_items(const menu_t *m, const menu_status_t *st, const menu_item_t *out[MENU_SLOTS], int *pages)
{
    const int per_page = slots_of(layout_of(m));
    int total = 0;
    for (int i = 0; i < m->def->count; i++) {
        const menu_item_t *it = &m->def->items[i];
        total += it->category == m->category && menu_item_avail(it, st) != MENU_ITEM_HIDDEN;
    }
    const int n_pages = total > 0 ? (total + per_page - 1) / per_page : 1;
    const int page = m->page < n_pages ? m->page : n_pages - 1;
    int seen = 0, n = 0;
    for (int i = 0; i < m->def->count && n < per_page; i++) {
        const menu_item_t *it = &m->def->items[i];
        if (it->category != m->category || menu_item_avail(it, st) == MENU_ITEM_HIDDEN) {
            continue;
        }
        if (seen++ >= page * per_page) {
            out[n++] = it;
        }
    }
    if (pages) {
        *pages = n_pages;
    }
    return n;
}

bool menu_slot_shown(const menu_t *m, int slot, uint32_t now)
{
    return (int32_t)(now - m->opened) >= slot * MENU_FLIP_MS; // a moment before the page opened counts as before
}

static bool inside(gfx_rect_t r, int pad, int x, int y)
{
    return x >= r.x - pad && y >= r.y - pad && x < r.x + r.w + pad && y < r.y + r.h + pad;
}

int menu_hit(const menu_t *m, const menu_status_t *st, int x, int y, uint32_t now)
{
    if (!gfx_rect_contains_point(m->area, x, y)) {
        return MENU_HIT_NONE;
    }
    const int mx = (x - m->area.x) / MENU_SCALE, my = (y - m->area.y) / MENU_SCALE;
    if (m->category != MENU_HOME) {
        // Back takes the whole top-left corner of the header: easy to hit without looking.
        const gfx_rect_t back = menu_back_rect();
        if (mx < back.x + back.w + 15 && my < HEADER_H - 2) {
            return MENU_HIT_BACK;
        }
    }
    if (st->office && inside(menu_head_rect(), 0, mx, my)) {
        return MENU_HIT_HEAD;
    }
    const menu_item_t *items[MENU_SLOTS];
    int pages;
    const int n = menu_page_items(m, st, items, &pages);
    const menu_layout_t layout = layout_of(m);
    for (int i = 0; i < n; i++) {
        if (inside(menu_slot_rect(layout, i), TOUCH_PAD, mx, my)) {
            return menu_slot_shown(m, i, now) ? i : MENU_HIT_NONE;
        }
    }
    const gfx_rect_t f = menu_footer_rect();
    if (pages > 1 && my >= f.y - MARGIN) {
        if (mx < f.x + MENU_ARROW_W + 15 && m->page > 0) {
            return MENU_HIT_PREV;
        }
        if (mx >= f.x + f.w - MENU_ARROW_W - 15 && m->page + 1 < pages) {
            return MENU_HIT_NEXT;
        }
    }
    return MENU_HIT_NONE;
}

void menu_cancel(menu_t *m)
{
    m->down = false;
    m->target = m->pressed = MENU_HIT_NONE;
}

static void turn_page(menu_t *m, const menu_status_t *st, int delta, uint32_t now)
{
    int pages;
    const menu_item_t *items[MENU_SLOTS];
    menu_page_items(m, st, items, &pages);
    const int page = (m->page < pages ? m->page : pages - 1) + delta;
    if (page >= 0 && page < pages) {
        show_page(m, m->category, page, now);
    }
}

// The finger stayed on the head long enough: the CLOCK FACES page (if there is a choice), and no tap after it.
static bool long_press(menu_t *m, const menu_status_t *st, uint32_t now)
{
    m->target = m->pressed = MENU_HIT_NONE;
    if (!faces_to_choose(st)) {
        return false;
    }
    menu_open(m, MENU_CLOCKFACES, now);
    return true;
}

bool menu_hold(menu_t *m, const menu_status_t *st, uint32_t now, uint32_t *wait_ms)
{
    *wait_ms = UINT32_MAX;
    if (!m->down || m->target != MENU_HIT_HEAD) {
        return false;
    }
    const uint32_t held = now - m->down_at;
    if (held < MENU_HOLD_MS) {
        *wait_ms = MENU_HOLD_MS - held;
        return false;
    }
    return long_press(m, st, now);
}

menu_result_t menu_touch(menu_t *m, menu_touch_t type, int x, int y, const menu_status_t *st, uint32_t now)
{
    const menu_result_t none = {MENU_DO_NOTHING, 0, NULL};
    if (type == MENU_TOUCH_DOWN) {
        menu_cancel(m); // a down without the previous up: start over
        if (!gfx_rect_contains_point(m->area, x, y)) {
            return none;
        }
        m->down = true;
        m->down_x = (int16_t)x;
        m->down_y = (int16_t)y;
        m->down_at = now;
        const int hit = menu_hit(m, st, x, y, now);
        if (hit >= 0 && hit < MENU_SLOTS) {
            const menu_item_t *items[MENU_SLOTS];
            menu_page_items(m, st, items, NULL);
            const menu_avail_t a = menu_item_avail(items[hit], st);
            if (a == MENU_ITEM_SOON) {
                m->nope = (int8_t)hit; // the tag blinks: there, but not yet
                m->nope_at = now;
            }
            if (a != MENU_ITEM_READY) {
                return none;
            }
        }
        m->target = m->pressed = (int8_t)hit;
        return none;
    }
    if (!m->down) {
        return none;
    }
    const int dx = x - m->down_x, dy = y - m->down_y;
    if (abs(dx) > MENU_SLOP_PX || abs(dy) > MENU_SLOP_PX) {
        m->target = m->pressed = MENU_HIT_NONE; // slid away: no tap
    }
    if (type == MENU_TOUCH_MOVE) {
        return none;
    }

    // Lifted.
    const int target = m->target;
    menu_cancel(m);
    if (target == MENU_HIT_NONE) {
        if (abs(dx) >= MENU_SWIPE_PX && abs(dx) > 2 * abs(dy)) {
            turn_page(m, st, dx < 0 ? 1 : -1, now); // swipe left: next page
        }
        return none;
    }
    if (menu_hit(m, st, x, y, now) != target) {
        return none;
    }
    switch (target) {
    case MENU_HIT_BACK:
        menu_open(m, (menu_category_t)m->def->categories[m->category].parent, now);
        return none;
    case MENU_HIT_PREV:
    case MENU_HIT_NEXT:
        turn_page(m, st, target == MENU_HIT_NEXT ? 1 : -1, now);
        return none;
    case MENU_HIT_HEAD: // lifted before it became a long press: the clock (held longer without a frame: the faces)
        if (now - m->down_at >= MENU_HOLD_MS) {
            long_press(m, st, now);
            return none;
        }
        return (menu_result_t){MENU_DO_OFFICE, 0, NULL};
    default:
        break;
    }
    const menu_item_t *items[MENU_SLOTS];
    const int n = menu_page_items(m, st, items, NULL);
    if (target >= n || menu_item_avail(items[target], st) != MENU_ITEM_READY) {
        return none; // changed under the finger (e.g. a Monster sequence started)
    }
    const menu_item_t *it = items[target];
    if (it->action == MENU_DO_OPEN) {
        menu_open(m, (menu_category_t)it->arg, now);
    } else {
        m->flash = (int8_t)target;
        m->flash_at = now;
    }
    return (menu_result_t){(menu_do_t)it->action, it->arg, it};
}
