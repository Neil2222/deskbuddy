// The touchscreen menu in the control panel: what it offers (a table of items in categories), how it is navigated,
// and how touches become actions. Pure logic: no pixels (menu_view.h draws it), no hardware, no clock of its own.
//
// What goes where, so the menu can grow without being redesigned:
//   moods        how Buddy is: ongoing states, exactly one active (MENU_DO_MOOD)
//   activities   things he does now, on top of his mood: short animations and modes (MENU_DO_EVENT, _OFFICE)
//   food & drink activities that are meals: Monster today; water, coffee, lunch and snacks later (his shows)
//   my day       what YOU are doing now, as you tell Buddy (MENU_DO_LOG): coffee, a smoke, a meeting. The companion
//                records it (evt did); Buddy only cheers. Never one of his shows, which record nothing
//   games        later: interactive experiences with their own screens
//   pdc          the PDC Manager integration (ZDMW), only while the companion offers it (menu_status_t.pdc)
// Scenarios (combinations of actions run by the companion) and needs (hunger, thirst, energy) are not menu items:
// they will trigger the same actions and change what the items show.
//
// Adding an item: one row in menu_items.c (plus an icon in menu_art.c if it needs a new one). A new kind of action:
// a MENU_DO_* value, its availability rule in menu_item_avail(), and what it does where the firmware runs actions.
//
// Navigation: Home lists the categories as wide rows; a category shows its items as a grid of tiles, in pages when
// there are more than fit. Back (top left) goes up one level. Tiles act when the finger is lifted on the tile it went
// down on; sliding away cancels, so scrolling or a swipe never triggers anything. A swipe sideways turns pages.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buddy_mood.h"
#include "gfx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Definitions --------------------------------------------------------------------------------------------------

typedef enum {
    MENU_HOME,
    MENU_MOODS,
    MENU_ACTIVITIES,
    MENU_FOOD,
    MENU_GAMES,
    MENU_MYDAY,
    MENU_PDC,
    MENU_CLOCKFACES, // which face Clock Mode shows: opened by a long press on the header's Buddy head
    MENU_CATEGORY_COUNT,
} menu_category_t;

typedef enum {
    MENU_LIST, // wide rows: icon, label, arrow (Home)
    MENU_GRID, // tiles in two columns
} menu_layout_t;

typedef struct {
    const char *title; // capitals, shown in the header
    uint8_t layout;    // menu_layout_t
    uint8_t parent;    // where Back goes
} menu_category_def_t;

typedef enum {
    MENU_DO_NOTHING, // no action of its own (an item that is coming soon)
    MENU_DO_OPEN,    // opens category `arg`
    MENU_DO_MOOD,    // sets mood `arg` (buddy_mood_t) until it is changed
    MENU_DO_EVENT,   // delivers buddy_event_t `arg` (Surprise!, Monster)
    MENU_DO_OFFICE,  // switches to the Office Mode clock (a tap on the header's Buddy head)
    MENU_DO_PDC,     // asks to confirm a ZDMW click (then the companion sends it to PDC Manager)
    MENU_DO_LOG,     // you are doing what the item says (MY DAY): reported to the companion, never refused
    MENU_DO_FACE,    // selects Clock Mode's face `arg` (office_face_t); a tap on the header's head then shows that face
} menu_do_t;

typedef enum {
    MENU_ICON_NONE,
    MENU_ICON_MOOD, // Buddy's face for the mood in `arg`
    MENU_ICON_MOODS,
    MENU_ICON_ACTIVITIES,
    MENU_ICON_FOOD,
    MENU_ICON_GAMES,
    MENU_ICON_SCHEDULE, // the calendar: MY DAY
    MENU_ICON_SURPRISE,
    MENU_ICON_MONSTER,
    MENU_ICON_OFFICE,
    MENU_ICON_WATER,
    MENU_ICON_COFFEE,
    MENU_ICON_LUNCH,
    MENU_ICON_SNACKS,
    MENU_ICON_SNEEZE,   // Buddy's face, for activities that are him (no props)
    MENU_ICON_STARE,
    MENU_ICON_MEDITATE,
    MENU_ICON_FOCUS,
    MENU_ICON_PEUKIE,   // the smoking face, for "PEUKIE?" (the question)
    MENU_ICON_ZDMW,     // the exasperated face: "ZE DOEN MAAR WAT!" (PDC Manager)
    MENU_ICON_MEETING,  // two speech bubbles
    MENU_ICON_WALK,     // a sneaker: stretch your legs
    MENU_ICON_GEAR,     // a brass gear: the Mechanical Skeleton face
    MENU_ICON_KITT,     // his head with KITT's scanner on the face screen
    MENU_ICON_COUNT,
} menu_icon_t;

typedef struct {
    const char *id;    // stable name ("mood.relaxed"), for logs and later for the companion
    const char *label; // capitals in the sign's 5x7 font; must fit a tile (host-tested)
    uint8_t category;  // menu_category_t it is listed in
    uint8_t icon;      // menu_icon_t
    uint8_t action;    // menu_do_t
    uint8_t arg;
    bool soon;         // planned: shown with a SOON tag, does nothing yet
} menu_item_t;

typedef struct {
    const menu_category_def_t *categories; // MENU_CATEGORY_COUNT entries
    const menu_item_t *items;              // in display order
    int count;
} menu_def_t;

// DeskBuddy's menu (menu_items.c).
extern const menu_def_t menu_default;

// What the menu shows depends on Buddy's state; the firmware owns it and passes it in on every call.
typedef struct {
    buddy_mood_t mood;   // the brain's current mood
    bool monster;        // a Monster sequence is running (his face in the header charges)
    bool busy;           // an activity is playing (the Monster sequence or another): he ignores other ones
    const char *doing;   // what he is doing ("drinking"; buddy_activity_doing), NULL: his mood is shown
    bool office;         // Clock Mode (first called Office Mode) is available
    bool pdc;            // the companion offers the PDC Manager integration: the PDC category shows
    bool pdc_ready;      // ...and a ZDMW click can be sent now (none under way, no cooldown)
    int32_t pdc_count;   // the ZDMW counter; -1: unknown
    uint8_t faces;       // Clock Mode faces that can be selected: bit n for office_face_t n (CLOCK FACES shows
                         // with two or more)
    uint8_t face;        // the selected face (office_face_t)
    bool kitt;           // he is KITT (BUDDY_STATE_KITT): its item stands for what he is, not his mood's
} menu_status_t;

typedef enum {
    MENU_ITEM_HIDDEN, // not offered (Office Mode without its clock, PDC without the companion offering it)
    MENU_ITEM_SOON,   // shown with a SOON tag, not interactive
    MENU_ITEM_BUSY,   // shown dimmed: not right now (Surprise! while Buddy drinks a Monster)
    MENU_ITEM_READY,
} menu_avail_t;

menu_avail_t menu_item_avail(const menu_item_t *it, const menu_status_t *st);

// The item stands for what Buddy is doing now (his current mood, or KITT).
bool menu_item_active(const menu_item_t *it, const menu_status_t *st);

// What Buddy puts on his sign when a MY DAY item is tapped ("ENJOY YOUR COFFEE!"); NULL for other items.
const char *menu_item_note(const menu_item_t *it);

// --- Geometry -----------------------------------------------------------------------------------------------------
// The menu is drawn in menu pixels of MENU_SCALE x MENU_SCALE screen pixels, MENU_W x MENU_H of them.

#define MENU_SCALE 2
#define MENU_W 175
#define MENU_H 300
#define MENU_SLOTS 8      // most items on one page (the grid: 2 x 4)
#define MENU_LIST_ROWS 5  // rows on a list page
#define MENU_TILE_W 80    // grid tile, including its 2-pixel ledge at the bottom
#define MENU_TILE_H 54
#define MENU_ROW_W 165    // list row, including its ledge
#define MENU_ROW_H 42
#define MENU_LEDGE 2      // a button's depth: it sinks this far when pressed
#define MENU_LABEL_MAX 74 // widest label (menu pixels) that fits a tile
#define MENU_BACK_W 40
#define MENU_BACK_H 26
#define MENU_FOOTER_W 165
#define MENU_FOOTER_H 26
#define MENU_ARROW_W 36   // the page arrows at the ends of the footer

// Timing, in ms.
#define MENU_FLIP_MS 30   // after a page change, tile i appears i * MENU_FLIP_MS later
#define MENU_POP_MS 80    // a tile appears sunk in, then pops up
#define MENU_FLASH_MS 220 // a tile lights up after it was tapped
#define MENU_NOPE_MS 450  // the SOON tag blinks when a planned item is tapped
#define MENU_HOLD_MS 650  // a finger held this long on the header's Buddy head is a long press, not a tap

// Touch: a finger that moves further than this from where it went down no longer taps; a sideways movement of at
// least MENU_SWIPE_PX (and clearly more sideways than up or down) turns the page. Screen pixels.
#define MENU_SLOP_PX 24
#define MENU_SWIPE_PX 64

// Places in menu pixels, relative to the menu's top-left corner.
gfx_rect_t menu_slot_rect(menu_layout_t layout, int slot);
gfx_rect_t menu_back_rect(void);
gfx_rect_t menu_footer_rect(void);
// The header's Buddy head as a touch target: wider and taller than the head itself (a fingertip), up to the header's
// right edge and its bottom, clear of Back and the tiles.
gfx_rect_t menu_head_rect(void);

// --- Navigation and touch -----------------------------------------------------------------------------------------

// Touch targets besides the slots (0 .. MENU_SLOTS - 1).
enum {
    MENU_HIT_NONE = -1,
    MENU_HIT_BACK = MENU_SLOTS,
    MENU_HIT_PREV,
    MENU_HIT_NEXT,
    MENU_HIT_HEAD, // the header's Buddy head, while Clock Mode is available: tap for the clock, hold for its faces
};

typedef enum {
    MENU_TOUCH_DOWN,
    MENU_TOUCH_MOVE,
    MENU_TOUCH_UP,
} menu_touch_t;

typedef struct {
    const menu_def_t *def;
    gfx_rect_t area;   // screen rectangle of the menu
    uint8_t category;  // shown
    uint8_t page;      // within the category
    uint32_t opened;   // when the page was opened (its tiles flip in from then on)
    // A touch in progress.
    bool down;
    int16_t down_x, down_y;
    uint32_t down_at;  // when the finger went down
    int8_t target;     // what the finger went down on (a slot or MENU_HIT_*); NONE once it slid away
    // Feedback for the view.
    int8_t pressed;    // drawn pressed in: the target while the finger is on it
    int8_t flash;      // tapped at flash_at (lights up for MENU_FLASH_MS)
    int8_t nope;       // a planned item tapped at nope_at
    uint32_t flash_at, nope_at;
} menu_t;

// What a tap asks the firmware to do (navigation is handled inside the menu).
typedef struct {
    menu_do_t action;  // MENU_DO_NOTHING: nothing to do
    uint8_t arg;
    const menu_item_t *item;
} menu_result_t;

// The menu takes `area` (screen pixels; at least MENU_W x MENU_H menu pixels, centred in it) and starts at Home.
// Returns false if it does not fit.
bool menu_init(menu_t *m, const menu_def_t *def, gfx_rect_t area, uint32_t now);

// Opens a category's first page.
void menu_open(menu_t *m, menu_category_t category, uint32_t now);

// Plays the current page's flip-in again (when the menu comes back after Office Mode), keeping page and category.
void menu_reveal(menu_t *m, uint32_t now);

// The items on the current page (up to MENU_SLOTS, in slot order) and the number of pages. Hidden items take no
// slot; pages beyond the last are clamped.
int menu_page_items(const menu_t *m, const menu_status_t *st, const menu_item_t *out[MENU_SLOTS], int *pages);

// True once slot `slot` of the current page has flipped in (only then can it be touched).
bool menu_slot_shown(const menu_t *m, int slot, uint32_t now);

// The target at screen point (x, y): a slot with an item, Back, a page arrow, or MENU_HIT_NONE.
int menu_hit(const menu_t *m, const menu_status_t *st, int x, int y, uint32_t now);

// Feeds a touch event. Returns the action a completed tap asks for. A tap on the header's Buddy head asks for
// MENU_DO_OFFICE (item NULL); held there for MENU_HOLD_MS it opens the CLOCK FACES page instead (and its release
// does nothing), see menu_hold().
menu_result_t menu_touch(menu_t *m, menu_touch_t type, int x, int y, const menu_status_t *st, uint32_t now);

// Call on every frame while the menu shows: a finger held on the header's Buddy head for MENU_HOLD_MS opens the CLOCK
// FACES page right then (returns true; with only one face there is nothing to choose: the press just ends).
// *wait_ms: ms until that would happen (UINT32_MAX: no such press under way).
bool menu_hold(menu_t *m, const menu_status_t *st, uint32_t now, uint32_t *wait_ms);

// Forgets a touch in progress (e.g. when the screen switches to Office Mode).
void menu_cancel(menu_t *m);

#ifdef __cplusplus
}
#endif
