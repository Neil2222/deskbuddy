// Draws the touchscreen menu (menu.h) into the scene, in the control panel.
//
// Every button is one sprite, drawn into its own indexed image at MENU_SCALE. A button is redrawn only when what it
// shows changes (another item, pressed, active, tapped), and only its own rectangle goes to the renderer: the panel
// is never redrawn as a whole. On a page change the tiles flip over one by one (MENU_FLIP_MS apart), each sinking in
// and popping up; an old tile disappears early only where a new one would overlap it.
//
// The header holds Back, the page title in amber, and Buddy's head wearing his current mood (it blinks now and then,
// and shows lightning eyes while he charges on a Monster). The footer tells his mood, or holds the page arrows when a
// category has more pages. The active item (his mood) has a cyan rim and a small blinking light.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx_scene.h"
#include "menu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_FACE_W 24 // Buddy's head, menu pixels
#define MENU_FACE_H 21
#define MENU_TITLE_W 96
#define MENU_TITLE_H 9

// Pixel memory the view needs: list rows for the first MENU_LIST_ROWS slots, tiles for the rest, and the header and
// footer pieces (the face twice: eyes open and blinking).
#define MENU_VIEW_MEMORY                                                                                             \
    (MENU_LIST_ROWS * MENU_ROW_W * MENU_ROW_H + (MENU_SLOTS - MENU_LIST_ROWS) * MENU_TILE_W * MENU_TILE_H +           \
     MENU_BACK_W * MENU_BACK_H + MENU_TITLE_W * MENU_TITLE_H + 2 * MENU_FACE_W * MENU_FACE_H +                       \
     MENU_FOOTER_W * MENU_FOOTER_H)

typedef struct {
    const menu_item_t *item; // NULL: empty
    uint8_t layout;
    uint8_t look;            // see menu_view.c
} menu_slot_look_t;

typedef struct {
    gfx_scene_t *scene;
    int x0, y0; // screen position of the menu's top-left corner
    bool visible;
    gfx_sprite_t slot[MENU_SLOTS], back, title, face, footer, led;
    gfx_image_t slot_img[MENU_SLOTS], back_img, title_img, face_img[2], footer_img;
    uint8_t *slot_px[MENU_SLOTS], *back_px, *title_px, *face_px[2], *footer_px;
    // What is on screen, so only changes are redrawn.
    menu_slot_look_t shown[MENU_SLOTS];
    int back_look;   // -1: not drawn
    int title_shown; // category; -1: not drawn
    int face_shown;  // buddy_mood_t, BUDDY_MOOD_COUNT for the charging face; -1: not drawn
    uint32_t footer_shown; // a summary of what the footer shows; 0: not drawn
    const char *doing_shown; // ...and what he was doing then (menu_status_t.doing)
    bool closing;          // folding away (menu_view_close) since close_at
    uint32_t close_at;
} menu_view_t;

// Uses mem (MENU_VIEW_MEMORY bytes, kept for the view's lifetime; fast memory is best, the compositor reads it) and
// adds the view's sprites to the scene at z..z+1, hidden; the next apply draws them. Places the menu at m's area.
// Returns false if the memory is too small, the scene is full or the art is malformed.
bool menu_view_init(menu_view_t *v, gfx_scene_t *scene, const menu_t *m, uint8_t *mem, size_t mem_size, int z);

// Hides the whole menu at once, or shows it again: the next apply redraws everything as it is then (after
// menu_reveal(), its tiles flip in one by one).
void menu_view_show(menu_view_t *v, bool visible);

// Folds the menu away (Buddy going into Office Mode): its buttons vanish one by one, MENU_FLIP_MS apart as they flip
// in, then the header and footer; then it is hidden as with menu_view_show(v, false).
void menu_view_close(menu_view_t *v, uint32_t now);

// Brings the sprites up to date with the menu and Buddy's state at now. Returns ms until the view changes next on
// its own (a flip, a blink, the active light); UINT32_MAX when nothing is scheduled.
uint32_t menu_view_apply(menu_view_t *v, const menu_t *m, const menu_status_t *st, uint32_t now);

#ifdef __cplusplus
}
#endif
