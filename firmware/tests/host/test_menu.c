#include <stdlib.h>
#include <string.h>

#include "buddy_brain.h"
#include "gfx_repair.h"
#include "gfx_scene.h"
#include "layout.h"
#include "marquee.h"
#include "menu.h"
#include "menu_view.h"
#include "office_faces.h"
#include "test.h"

enum { W = 1024, H = 600, ALIGN = 32 };

static const menu_status_t k_idle = {.mood = BUDDY_MOOD_IDLE, .office = true};

static gfx_rect_t panel_area(void)
{
    const layout_t l = layout_compute(W, H, 65, false, ALIGN);
    return layout_panel_content(&l);
}

static int text_width(const char *s)
{
    uint8_t cols[256];
    return marquee_text_columns(s, cols, (int)sizeof(cols));
}

// Screen point in the middle of a slot of the current page.
static void slot_center(const menu_t *m, int slot, int *x, int *y)
{
    const gfx_rect_t r = menu_slot_rect((menu_layout_t)m->def->categories[m->category].layout, slot);
    *x = m->area.x + (r.x + r.w / 2) * MENU_SCALE;
    *y = m->area.y + (r.y + r.h / 2) * MENU_SCALE;
}

static menu_result_t tap_at(menu_t *m, int x, int y, const menu_status_t *st, uint32_t now)
{
    menu_touch(m, MENU_TOUCH_DOWN, x, y, st, now);
    return menu_touch(m, MENU_TOUCH_UP, x, y, st, now + 60);
}

static menu_result_t tap(menu_t *m, int slot, const menu_status_t *st, uint32_t now)
{
    int x, y;
    slot_center(m, slot, &x, &y);
    return tap_at(m, x, y, st, now);
}

// Slot of the item with this id on the current page, -1 if not there.
static int slot_of(const menu_t *m, const menu_status_t *st, const char *id)
{
    const menu_item_t *items[MENU_SLOTS];
    const int n = menu_page_items(m, st, items, NULL);
    for (int i = 0; i < n; i++) {
        if (strcmp(items[i]->id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static void back_center(const menu_t *m, int *x, int *y)
{
    const gfx_rect_t r = menu_back_rect();
    *x = m->area.x + (r.x + r.w / 2) * MENU_SCALE;
    *y = m->area.y + (r.y + r.h / 2) * MENU_SCALE;
}

void test_menu_definitions_fit_and_connect(void)
{
    const menu_def_t *d = &menu_default;
    // Home, and CLOCK FACES through a long press on the header's Buddy head (test_menu_head_*).
    bool reachable[MENU_CATEGORY_COUNT] = {[MENU_HOME] = true, [MENU_CLOCKFACES] = true};
    int moods[BUDDY_MOOD_COUNT] = {0};
    for (int i = 0; i < d->count; i++) {
        const menu_item_t *it = &d->items[i];
        CHECK(it->category < MENU_CATEGORY_COUNT && it->icon > MENU_ICON_NONE && it->icon < MENU_ICON_COUNT);
        const bool list = d->categories[it->category].layout == MENU_LIST;
        CHECK(text_width(it->label) <= (list ? 100 : MENU_LABEL_MAX)); // the label fits its button
        for (int j = 0; j < i; j++) {
            CHECK(strcmp(it->id, d->items[j].id) != 0); // ids are unique
        }
        if (it->action == MENU_DO_OPEN && !it->soon) {
            CHECK(it->arg < MENU_CATEGORY_COUNT);
            reachable[it->arg] = true;
        }
        if (it->action == MENU_DO_MOOD) {
            CHECK(it->arg < BUDDY_MOOD_COUNT);
            moods[it->arg]++;
        }
        CHECK(it->soon || it->action != MENU_DO_NOTHING); // nothing pretends to work
    }
    for (int m = 0; m < BUDDY_MOOD_COUNT; m++) {
        CHECK_EQ(moods[m], 1); // every mood exactly once
    }
    for (int c = 0; c < MENU_CATEGORY_COUNT; c++) {
        CHECK(text_width(d->categories[c].title) <= MENU_TITLE_W);
        // Every category with something in it can be opened from Home.
        bool used = false;
        for (int i = 0; i < d->count; i++) {
            used |= d->items[i].category == c && !d->items[i].soon;
        }
        CHECK(!used || reachable[c]);
    }
}

// MY DAY: what you are doing, reported (MENU_DO_LOG) under the companion's activity type ids; each has a line for
// the sign; it works while Buddy is busy (telling him is not one of his shows), and nothing else logs.
void test_menu_my_day_reports_what_you_do(void)
{
    static const char *const types[] = {"work", "meeting", "coffee", "water", "smoke", "stretch", "lunch"};
    const menu_def_t *d = &menu_default;
    const menu_status_t busy = {.mood = BUDDY_MOOD_IDLE, .office = true, .busy = true};
    int found = 0;
    for (int i = 0; i < d->count; i++) {
        const menu_item_t *it = &d->items[i];
        const bool myday = it->category == MENU_MYDAY;
        CHECK_EQ(myday, it->action == MENU_DO_LOG); // only MY DAY logs, and all of it does
        CHECK_EQ(myday, menu_item_note(it) != NULL);
        if (!myday) {
            continue;
        }
        CHECK(strncmp(it->id, "myday.", 6) == 0);
        bool known = false;
        for (size_t t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
            known |= strcmp(it->id + 6, types[t]) == 0;
        }
        CHECK(known);
        CHECK(!it->soon && menu_item_avail(it, &busy) == MENU_ITEM_READY);
        const int w = text_width(menu_item_note(it));
        CHECK(w > 0 && strlen(menu_item_note(it)) <= 60);
        found++;
    }
    CHECK_EQ(found, (int)(sizeof(types) / sizeof(types[0])));
    CHECK(found <= MENU_SLOTS); // one page

    static menu_t m;
    CHECK(menu_init(&m, &menu_default, panel_area(), 0));
    const int home_slot = slot_of(&m, &k_idle, "myday");
    CHECK(home_slot >= 0);
    CHECK_EQ(tap(&m, home_slot, &k_idle, 1000).action, MENU_DO_OPEN);
    CHECK_EQ(m.category, MENU_MYDAY);
    const menu_result_t r = tap(&m, slot_of(&m, &busy, "myday.coffee"), &busy, 5000);
    CHECK_EQ(r.action, MENU_DO_LOG);
    CHECK(r.item && strcmp(r.item->id, "myday.coffee") == 0);
}

void test_menu_navigates_and_goes_back(void)
{
    static menu_t m;
    CHECK(menu_init(&m, &menu_default, panel_area(), 0));
    CHECK(!menu_init(&m, &menu_default, (gfx_rect_t){0, 0, 300, 600}, 0)); // a panel too narrow is refused
    CHECK(menu_init(&m, &menu_default, panel_area(), 0));
    CHECK_EQ(m.area.x, 674); // right of the divider
    CHECK_EQ(m.area.w, MENU_W * MENU_SCALE);
    CHECK_EQ(m.category, MENU_HOME);
    uint32_t t = 1000;

    // Home lists the categories; a tap on Moods opens it (navigation, nothing for the firmware to run).
    menu_result_t r = tap(&m, slot_of(&m, &k_idle, "moods"), &k_idle, t);
    CHECK_EQ(r.action, MENU_DO_OPEN);
    CHECK_EQ(m.category, MENU_MOODS);
    int pages;
    const menu_item_t *items[MENU_SLOTS];
    CHECK_EQ(menu_page_items(&m, &k_idle, items, &pages), BUDDY_MOOD_COUNT + 1); // all seven and KITT, one page
    CHECK_EQ(pages, 1);

    // Back, from anywhere in the header's left corner.
    int x, y;
    back_center(&m, &x, &y);
    CHECK_EQ(menu_hit(&m, &k_idle, x, y, t), MENU_HIT_BACK);
    CHECK_EQ(menu_hit(&m, &k_idle, m.area.x + 2, m.area.y + 2, t), MENU_HIT_BACK);
    t += 1000;
    r = tap_at(&m, x, y, &k_idle, t);
    CHECK_EQ(r.action, MENU_DO_NOTHING);
    CHECK_EQ(m.category, MENU_HOME);
    CHECK(menu_hit(&m, &k_idle, x, y, t + 1000) != MENU_HIT_BACK); // Home has no Back

    // Games opens its page; Tic-Tac-Toe opens the game's page (the header only: no items), which Back leaves for
    // Games. While a show plays the game cannot start (its tile is dimmed and does nothing).
    t += 1000;
    CHECK_EQ(tap(&m, slot_of(&m, &k_idle, "games"), &k_idle, t).action, MENU_DO_OPEN);
    CHECK_EQ(m.category, MENU_GAMES);
    const menu_status_t busy = {.mood = BUDDY_MOOD_IDLE, .office = true, .busy = true};
    t += 1000;
    const int ttt = slot_of(&m, &k_idle, "game.ttt");
    CHECK(ttt >= 0);
    CHECK_EQ(tap(&m, ttt, &busy, t).action, MENU_DO_NOTHING);
    CHECK_EQ(m.category, MENU_GAMES);
    t += 1000;
    CHECK_EQ(tap(&m, ttt, &k_idle, t).action, MENU_DO_OPEN);
    CHECK_EQ(m.category, MENU_TTT);
    CHECK_EQ(menu_page_items(&m, &k_idle, items, &pages), 0);
    t += 1000;
    back_center(&m, &x, &y);
    tap_at(&m, x, y, &k_idle, t);
    CHECK_EQ(m.category, MENU_GAMES);
    t += 1000;
    tap_at(&m, x, y, &k_idle, t);
    CHECK_EQ(m.category, MENU_HOME);

    // Every other category opens and comes back.
    static const char *const k_open[] = {"activities", "food"};
    for (unsigned i = 0; i < sizeof(k_open) / sizeof(k_open[0]); i++) {
        t += 1000;
        CHECK_EQ(tap(&m, slot_of(&m, &k_idle, k_open[i]), &k_idle, t).action, MENU_DO_OPEN);
        CHECK(m.category != MENU_HOME);
        t += 1000;
        back_center(&m, &x, &y);
        tap_at(&m, x, y, &k_idle, t);
        CHECK_EQ(m.category, MENU_HOME);
    }
}

void test_menu_selects_every_mood(void)
{
    static menu_t m;
    CHECK(menu_init(&m, &menu_default, panel_area(), 0));
    menu_open(&m, MENU_MOODS, 0);
    menu_status_t st = k_idle;
    uint32_t t = 1000;
    for (int mood = 0; mood < BUDDY_MOOD_COUNT; mood++) {
        char id[32];
        snprintf(id, sizeof(id), "mood.%s", buddy_mood_name((buddy_mood_t)mood));
        const int slot = slot_of(&m, &st, id);
        CHECK(slot >= 0);
        const menu_result_t r = tap(&m, slot, &st, t);
        CHECK_EQ(r.action, MENU_DO_MOOD);
        CHECK_EQ(r.arg, mood);
        CHECK_EQ(m.flash, slot); // lights up
        st.mood = (buddy_mood_t)r.arg; // the brain took it

        // Exactly the chosen mood shows as active.
        const menu_item_t *items[MENU_SLOTS];
        const int n = menu_page_items(&m, &st, items, NULL);
        for (int i = 0; i < n; i++) {
            CHECK_EQ(menu_item_active(items[i], &st), i == slot);
        }
        t += 500;
    }
    // A mood also works while Buddy drinks a Monster (it shows when he is done).
    st.monster = st.busy = true;
    CHECK_EQ(tap(&m, slot_of(&m, &st, "mood.angry"), &st, t).action, MENU_DO_MOOD);
}

void test_menu_activities_and_availability(void)
{
    static menu_t m;
    CHECK(menu_init(&m, &menu_default, panel_area(), 0));
    menu_open(&m, MENU_ACTIVITIES, 0);
    // The shows only: the clock lives on the header's Buddy head now.
    static const char *const ids[] = {"activity.surprise", "activity.sneeze", "activity.stare",
                                      "activity.meditate", "activity.focus",  "activity.peukie"};
    const menu_item_t *items[MENU_SLOTS];
    int pages;
    CHECK_EQ(menu_page_items(&m, &k_idle, items, &pages), 6);
    CHECK_EQ(pages, 1);
    for (int i = 0; i < 6; i++) {
        CHECK(strcmp(items[i]->id, ids[i]) == 0);
        CHECK_EQ(items[i]->action, MENU_DO_EVENT);
    }
    uint32_t t = 1000;
    menu_result_t r = tap(&m, slot_of(&m, &k_idle, "activity.surprise"), &k_idle, t);
    CHECK_EQ(r.action, MENU_DO_EVENT);
    CHECK_EQ(r.arg, BUDDY_EVENT_POKE);

    // While a Monster sequence runs, Buddy ignores reactions: those buttons are dimmed and do nothing.
    menu_status_t busy = k_idle;
    busy.busy = true; // an activity plays (the Monster sequence or another)
    const int surprise = slot_of(&m, &busy, "activity.surprise");
    menu_page_items(&m, &busy, items, NULL);
    CHECK_EQ(menu_item_avail(items[surprise], &busy), MENU_ITEM_BUSY);
    CHECK_EQ(tap(&m, surprise, &busy, t += 500).action, MENU_DO_NOTHING);
    CHECK_EQ(m.pressed, MENU_HIT_NONE);

    // Food & Drink: Monster and Water work (and are busy during any activity); the rest is planned.
    menu_open(&m, MENU_FOOD, t += 500);
    t += 1000;
    r = tap(&m, slot_of(&m, &k_idle, "food.monster"), &k_idle, t);
    CHECK_EQ(r.action, MENU_DO_EVENT);
    CHECK_EQ(r.arg, BUDDY_EVENT_DRINK_MONSTER);
    CHECK_EQ(tap(&m, slot_of(&m, &busy, "food.monster"), &busy, t += 500).action, MENU_DO_NOTHING);
    r = tap(&m, slot_of(&m, &k_idle, "food.water"), &k_idle, t += 500);
    CHECK_EQ(r.action, MENU_DO_EVENT);
    CHECK_EQ(r.arg, BUDDY_EVENT_DRINK_WATER);
    CHECK_EQ(tap(&m, slot_of(&m, &busy, "food.water"), &busy, t += 500).action, MENU_DO_NOTHING);
    const int coffee = slot_of(&m, &k_idle, "food.coffee");
    CHECK_EQ(tap(&m, coffee, &k_idle, t += 500).action, MENU_DO_NOTHING);
    CHECK_EQ(m.nope, coffee);
}

// Screen point of the header's Buddy head (the middle of the head as drawn, inside its touch area).
static void head_center(const menu_t *m, int *x, int *y)
{
    *x = m->area.x + (146 + 12) * MENU_SCALE;
    *y = m->area.y + (7 + 10) * MENU_SCALE;
}

// A finger held at (x, y) for ms, the frames polling menu_hold() every 30 ms as the firmware does; returns whether
// the hold opened the CLOCK FACES page, and the release's result in *up.
static bool hold_at(menu_t *m, int x, int y, const menu_status_t *st, uint32_t now, uint32_t ms, menu_result_t *up)
{
    bool opened = false;
    uint32_t wait;
    menu_touch(m, MENU_TOUCH_DOWN, x, y, st, now);
    for (uint32_t t = now; t < now + ms; t += 30) {
        opened |= menu_hold(m, st, t, &wait);
    }
    *up = menu_touch(m, MENU_TOUCH_UP, x, y, st, now + ms);
    return opened;
}

// The header's Buddy head: a tap enters Clock Mode, a long press opens CLOCK FACES, from Home and from every page.
void test_menu_head_tap_enters_the_clock_from_every_page(void)
{
    static menu_t m;
    CHECK(menu_init(&m, &menu_default, panel_area(), 0));
    menu_status_t st = k_idle;
    st.faces = (1u << OFFICE_FACE_COUNT) - 1;
    int x, y;
    head_center(&m, &x, &y);
    static const menu_category_t pages[] = {MENU_HOME,  MENU_MOODS,      MENU_ACTIVITIES, MENU_FOOD,
                                            MENU_MYDAY, MENU_CLOCKFACES};
    uint32_t t = 1000;
    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        menu_open(&m, pages[i], t);
        CHECK_EQ(menu_hit(&m, &st, x, y, t), MENU_HIT_HEAD);
        menu_result_t up;
        CHECK(!hold_at(&m, x, y, &st, t += 1000, 200, &up));
        CHECK_EQ(up.action, MENU_DO_OFFICE);
        CHECK_EQ(m.category, pages[i]); // the page stays (the menu hides for the zoom and comes back on it)
        t += 1000;
    }
    // Also while he is busy (a show, KITT), as the CLOCK tile was: Clock Mode always wins.
    menu_status_t busy = st;
    busy.busy = busy.kitt = true;
    menu_result_t up;
    hold_at(&m, x, y, &busy, t += 1000, 100, &up);
    CHECK_EQ(up.action, MENU_DO_OFFICE);

    // Without a clock the head is only his head: no target, and a tap there does nothing.
    menu_status_t no_office = st;
    no_office.office = false;
    CHECK_EQ(menu_hit(&m, &no_office, x, y, t), MENU_HIT_NONE);
    CHECK(!hold_at(&m, x, y, &no_office, t += 1000, 100, &up));
    CHECK_EQ(up.action, MENU_DO_NOTHING);
    CHECK(!hold_at(&m, x, y, &no_office, t += 1000, 1000, &up));
    CHECK_EQ(up.action, MENU_DO_NOTHING);
}

void test_menu_head_long_press_opens_clock_faces(void)
{
    static menu_t m;
    CHECK(menu_init(&m, &menu_default, panel_area(), 0));
    menu_status_t st = k_idle;
    st.faces = (1u << OFFICE_FACE_COUNT) - 1;
    st.face = OFFICE_FACE_SKELETON;
    int x, y;
    head_center(&m, &x, &y);
    uint32_t t = 1000;

    // Held: the page opens at MENU_HOLD_MS, while the finger is still down; the release then does nothing.
    menu_open(&m, MENU_ACTIVITIES, t);
    uint32_t wait;
    menu_touch(&m, MENU_TOUCH_DOWN, x, y, &st, t += 1000);
    CHECK(!menu_hold(&m, &st, t + MENU_HOLD_MS - 1, &wait));
    CHECK_EQ(wait, 1);
    CHECK_EQ(m.category, MENU_ACTIVITIES);
    CHECK(menu_hold(&m, &st, t + MENU_HOLD_MS, &wait));
    CHECK_EQ(m.category, MENU_CLOCKFACES);
    CHECK(!menu_hold(&m, &st, t + MENU_HOLD_MS + 30, &wait)); // once
    CHECK_EQ(wait, UINT32_MAX);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x, y, &st, t + 2000).action, MENU_DO_NOTHING); // no tap after it

    // The page: one tile per face this firmware has (its id the face's wire name), the selected one marked; a tap
    // selects; Back goes Home (the page belongs to no category).
    const menu_item_t *items[MENU_SLOTS];
    CHECK_EQ(menu_page_items(&m, &st, items, NULL), OFFICE_FACE_COUNT);
    for (int i = 0; i < OFFICE_FACE_COUNT; i++) {
        office_face_t face;
        CHECK(strncmp(items[i]->id, "face.", 5) == 0 && office_face_parse(items[i]->id + 5, &face));
        CHECK_EQ(items[i]->action, MENU_DO_FACE);
        CHECK_EQ(items[i]->arg, face);
        CHECK_EQ(menu_item_active(items[i], &st), face == OFFICE_FACE_SKELETON);
    }
    t += 3000;
    menu_result_t r = tap(&m, slot_of(&m, &st, "face.classic"), &st, t);
    CHECK_EQ(r.action, MENU_DO_FACE);
    CHECK_EQ(r.arg, OFFICE_FACE_CLASSIC);
    menu_status_t one = st;
    one.faces = 1u << OFFICE_FACE_CLASSIC; // a face this device could not set up: not offered
    CHECK_EQ(slot_of(&m, &one, "face.skeleton"), -1);
    int bx, by;
    back_center(&m, &bx, &by);
    tap_at(&m, bx, by, &st, t += 500);
    CHECK_EQ(m.category, MENU_HOME);

    // From Home, Moods and MY DAY too; a release after MENU_HOLD_MS without a frame in between counts as held.
    static const menu_category_t pages[] = {MENU_HOME, MENU_MOODS, MENU_MYDAY};
    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        menu_open(&m, pages[i], t += 1000);
        menu_result_t up;
        CHECK(hold_at(&m, x, y, &st, t += 1000, MENU_HOLD_MS + 100, &up));
        CHECK_EQ(up.action, MENU_DO_NOTHING);
        CHECK_EQ(m.category, MENU_CLOCKFACES);
    }
    menu_open(&m, MENU_FOOD, t += 1000);
    menu_touch(&m, MENU_TOUCH_DOWN, x, y, &st, t += 1000);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x, y, &st, t + MENU_HOLD_MS).action, MENU_DO_NOTHING);
    CHECK_EQ(m.category, MENU_CLOCKFACES);

    // Nothing to choose (one face): a long press just ends, never turning into a tap.
    menu_open(&m, MENU_HOME, t += 1000);
    menu_result_t up;
    CHECK(!hold_at(&m, x, y, &one, t += 1000, MENU_HOLD_MS + 100, &up));
    CHECK_EQ(up.action, MENU_DO_NOTHING);
    CHECK_EQ(m.category, MENU_HOME);
}

// A finger that slides off the head, or lifts elsewhere, does nothing; the head's area keeps clear of Back and of
// the tiles below the header.
void test_menu_head_touch_cancels_and_keeps_clear(void)
{
    static menu_t m;
    CHECK(menu_init(&m, &menu_default, panel_area(), 0));
    menu_status_t st = k_idle;
    st.faces = (1u << OFFICE_FACE_COUNT) - 1;
    int x, y;
    head_center(&m, &x, &y);
    uint32_t t = 1000, wait;
    menu_open(&m, MENU_MOODS, t);
    t += 1000;

    // Slid away (beyond the slop): neither a tap nor, held on, a long press.
    menu_touch(&m, MENU_TOUCH_DOWN, x, y, &st, t);
    menu_touch(&m, MENU_TOUCH_MOVE, x - MENU_SLOP_PX - 4, y, &st, t + 50);
    CHECK(!menu_hold(&m, &st, t + MENU_HOLD_MS + 10, &wait));
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x, y, &st, t + MENU_HOLD_MS + 20).action, MENU_DO_NOTHING);
    CHECK_EQ(m.category, MENU_MOODS);
    // A small wobble still counts as a tap.
    menu_touch(&m, MENU_TOUCH_DOWN, x, y, &st, t += 1000);
    menu_touch(&m, MENU_TOUCH_MOVE, x + MENU_SLOP_PX / 2, y + 2, &st, t + 50);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x + MENU_SLOP_PX / 2, y + 2, &st, t + 100).action, MENU_DO_OFFICE);
    // Down on a tile, lifted on the head (no move events in between): nothing.
    int sx, sy;
    slot_center(&m, 1, &sx, &sy);
    menu_touch(&m, MENU_TOUCH_DOWN, sx, sy, &st, t += 1000);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x, y, &st, t + 60).action, MENU_DO_NOTHING);
    // Down on the head, lifted on a tile: nothing either.
    menu_touch(&m, MENU_TOUCH_DOWN, x, y, &st, t += 1000);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, sx, sy, &st, t + 60).action, MENU_DO_NOTHING);

    // The area: bigger than the head as drawn, inside the header, apart from Back and the tiles.
    const gfx_rect_t head = menu_head_rect(), back = menu_back_rect();
    CHECK(head.x <= 146 - 4 && head.x + head.w >= 146 + MENU_FACE_W && head.y <= 7 - 4 && head.h >= 21 + 4);
    CHECK(head.x + head.w <= MENU_W && head.y + head.h <= menu_slot_rect(MENU_GRID, 1).y - 2); // above the tiles' pad
    CHECK(head.x >= back.x + back.w + 15);                                                     // right of Back's area
    for (int my = 0; my < head.h; my++) {
        int bx2 = m.area.x + (back.x + back.w + 14) * MENU_SCALE, by2 = m.area.y + my * MENU_SCALE;
        CHECK_EQ(menu_hit(&m, &st, bx2, by2, t), MENU_HIT_BACK); // Back's whole corner is still Back's
    }
}

void test_menu_touch_needs_a_clean_tap(void)
{
    static menu_t m;
    CHECK(menu_init(&m, &menu_default, panel_area(), 0));
    menu_open(&m, MENU_MOODS, 0);
    uint32_t t = 1000;
    int x, y, x2, y2;
    slot_center(&m, 1, &x, &y);
    slot_center(&m, 2, &x2, &y2);

    // Pressed while the finger is down; sliding away cancels, also when it comes back.
    menu_touch(&m, MENU_TOUCH_DOWN, x, y, &k_idle, t);
    CHECK_EQ(m.pressed, 1);
    menu_touch(&m, MENU_TOUCH_MOVE, x, y + MENU_SLOP_PX / 2, &k_idle, t + 10); // a wobble is fine
    CHECK_EQ(m.pressed, 1);
    menu_touch(&m, MENU_TOUCH_MOVE, x, y + MENU_SLOP_PX + 4, &k_idle, t + 20);
    CHECK_EQ(m.pressed, MENU_HIT_NONE);
    menu_touch(&m, MENU_TOUCH_MOVE, x, y, &k_idle, t + 30);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x, y, &k_idle, t + 40).action, MENU_DO_NOTHING);

    // Lifted on another tile (without move events in between): nothing.
    menu_touch(&m, MENU_TOUCH_DOWN, x, y, &k_idle, t += 100);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x2, y2, &k_idle, t + 50).action, MENU_DO_NOTHING);

    // A touch that starts on the stage never acts in the menu, wherever it ends.
    menu_touch(&m, MENU_TOUCH_DOWN, 300, 300, &k_idle, t += 100);
    CHECK(!m.down);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x, y, &k_idle, t + 50).action, MENU_DO_NOTHING);

    // Between tiles: no target.
    const gfx_rect_t a = menu_slot_rect(MENU_GRID, 0), b = menu_slot_rect(MENU_GRID, 1);
    const int gap_x = m.area.x + (a.x + a.w + (b.x - a.x - a.w) / 2) * MENU_SCALE;
    CHECK_EQ(menu_hit(&m, &k_idle, gap_x, y, t), MENU_HIT_NONE);
    // A touch just outside a tile's edge still counts (fingers are wide).
    CHECK_EQ(menu_hit(&m, &k_idle, m.area.x + (a.x + a.w) * MENU_SCALE + 1, m.area.y + (a.y + 10) * MENU_SCALE, t), 0);

    // Rapid taps all count; a second down without an up starts over instead of acting.
    int taps = 0;
    for (int i = 0; i < 10; i++) {
        taps += tap(&m, 1, &k_idle, t += 80).action == MENU_DO_MOOD;
    }
    CHECK_EQ(taps, 10);
    menu_touch(&m, MENU_TOUCH_DOWN, x, y, &k_idle, t += 100);
    menu_touch(&m, MENU_TOUCH_DOWN, x2, y2, &k_idle, t + 10);
    CHECK_EQ(m.pressed, 2);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x2, y2, &k_idle, t + 50).arg, BUDDY_MOOD_PEUKIE);

    // A lift without a touch-down (e.g. after a cancel) does nothing.
    menu_cancel(&m);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x, y, &k_idle, t + 100).action, MENU_DO_NOTHING);

    // Right after a page change, a tile can only be touched once it has flipped in.
    menu_open(&m, MENU_MOODS, t += 1000);
    slot_center(&m, 5, &x, &y);
    CHECK_EQ(menu_hit(&m, &k_idle, x, y, t + 5 * MENU_FLIP_MS - 1), MENU_HIT_NONE);
    CHECK_EQ(menu_hit(&m, &k_idle, x, y, t + 5 * MENU_FLIP_MS), 5);
}

// A category with more than one page of items (only the tests have one so far).
static const menu_category_def_t k_cats[MENU_CATEGORY_COUNT] = {
    [MENU_HOME] = {"HOME", MENU_LIST, MENU_HOME},
    [MENU_MOODS] = {"MANY", MENU_GRID, MENU_HOME},
};
#define ITEM(n) {"item." #n, "ITEM " #n, MENU_MOODS, MENU_ICON_MOOD, MENU_DO_MOOD, (n) % BUDDY_MOOD_COUNT, false}
static const menu_item_t k_many_items[] = {
    {"many", "MANY", MENU_HOME, MENU_ICON_MOODS, MENU_DO_OPEN, MENU_MOODS, false},
    ITEM(0), ITEM(1), ITEM(2), ITEM(3), ITEM(4), ITEM(5), ITEM(6), ITEM(7), ITEM(8), ITEM(9), ITEM(10),
    {"office", "OFFICE", MENU_MOODS, MENU_ICON_OFFICE, MENU_DO_OFFICE, 0, false},
};
static const menu_def_t k_many = {k_cats, k_many_items, (int)(sizeof(k_many_items) / sizeof(k_many_items[0]))};

void test_menu_pages_and_swipes(void)
{
    static menu_t m;
    CHECK(menu_init(&m, &k_many, panel_area(), 0));
    menu_open(&m, MENU_MOODS, 0);
    uint32_t t = 1000;
    const menu_item_t *items[MENU_SLOTS];
    int pages;
    CHECK_EQ(menu_page_items(&m, &k_idle, items, &pages), MENU_SLOTS);
    CHECK_EQ(pages, 2);
    CHECK(strcmp(items[0]->id, "item.0") == 0);

    // The arrows in the footer: no "previous" on the first page.
    const gfx_rect_t f = menu_footer_rect();
    const int fy = m.area.y + (f.y + f.h / 2) * MENU_SCALE;
    const int prev_x = m.area.x + (f.x + 10) * MENU_SCALE, next_x = m.area.x + (f.x + f.w - 10) * MENU_SCALE;
    CHECK_EQ(menu_hit(&m, &k_idle, prev_x, fy, t), MENU_HIT_NONE);
    CHECK_EQ(menu_hit(&m, &k_idle, next_x, fy, t), MENU_HIT_NEXT);
    CHECK_EQ(tap_at(&m, next_x, fy, &k_idle, t).action, MENU_DO_NOTHING);
    CHECK_EQ(m.page, 1);
    CHECK_EQ(m.opened, t + 60); // its tiles flip in
    CHECK_EQ(menu_page_items(&m, &k_idle, items, &pages), 4);
    CHECK(strcmp(items[0]->id, "item.8") == 0);
    CHECK_EQ(menu_hit(&m, &k_idle, next_x, fy, t + 1000), MENU_HIT_NONE); // no "next" on the last page

    // Swipes turn pages and never trigger the tile they start on.
    t += 1000;
    int x, y;
    slot_center(&m, 0, &x, &y);
    menu_touch(&m, MENU_TOUCH_DOWN, x + 40, y, &k_idle, t);
    CHECK_EQ(m.pressed, 0);
    menu_touch(&m, MENU_TOUCH_MOVE, x + 80, y + 4, &k_idle, t + 20);
    CHECK_EQ(m.pressed, MENU_HIT_NONE);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x + 40 + MENU_SWIPE_PX + 10, y + 8, &k_idle, t + 40).action,
             MENU_DO_NOTHING);
    CHECK_EQ(m.page, 0); // swiped right: back
    t += 1000;
    menu_touch(&m, MENU_TOUCH_DOWN, x + 100, y, &k_idle, t);
    CHECK_EQ(menu_touch(&m, MENU_TOUCH_UP, x + 100 - MENU_SWIPE_PX - 10, y, &k_idle, t + 40).action, MENU_DO_NOTHING);
    CHECK_EQ(m.page, 1); // swiped left: next
    // Too short, or mostly up and down: no page turn.
    t += 1000;
    menu_touch(&m, MENU_TOUCH_DOWN, x + 100, y, &k_idle, t);
    menu_touch(&m, MENU_TOUCH_UP, x + 100 + MENU_SWIPE_PX / 2, y, &k_idle, t + 40);
    CHECK_EQ(m.page, 1);
    menu_touch(&m, MENU_TOUCH_DOWN, x + 100, y, &k_idle, t += 1000);
    menu_touch(&m, MENU_TOUCH_UP, x + 100 + MENU_SWIPE_PX + 10, y + MENU_SWIPE_PX + 10, &k_idle, t + 40);
    CHECK_EQ(m.page, 1);
    // Swiping past the ends stays put.
    menu_touch(&m, MENU_TOUCH_DOWN, x + 100, y, &k_idle, t += 1000);
    menu_touch(&m, MENU_TOUCH_UP, x + 100 - MENU_SWIPE_PX - 10, y, &k_idle, t + 40);
    CHECK_EQ(m.page, 1);

    // Fewer items (Clock hidden): the last page shrinks; a page that no longer exists is clamped.
    menu_status_t no_office = k_idle;
    no_office.office = false;
    CHECK_EQ(menu_page_items(&m, &no_office, items, &pages), 3);
    CHECK_EQ(pages, 2);
}

// --- The view -------------------------------------------------------------------------------------------------------

static gfx_color_t s_strip[16 * 1024];
static gfx_color_t s_buf[2][W * H];
static gfx_color_t s_ref[W * H];
static uint8_t s_mem[MENU_VIEW_MEMORY];

static bool shown(const gfx_sprite_t *sp)
{
    return !gfx_rect_is_empty(gfx_sprite_bounds(sp));
}

void test_menu_view_follows_the_state(void)
{
    static gfx_scene_t scene;
    static menu_t m;
    static menu_view_t v;
    static layout_t layout;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(menu_init(&m, &menu_default, layout_panel_content(&layout), 0));
    CHECK(!menu_view_init(&v, &scene, &m, s_mem, MENU_VIEW_MEMORY - 1, 10)); // too little memory
    CHECK(menu_view_init(&v, &scene, &m, s_mem, sizeof(s_mem), 10));

    // Home: five rows (two of them planned), no Back, the title, Buddy's face.
    menu_status_t st = k_idle;
    menu_view_apply(&v, &m, &st, 5000);
    for (int i = 0; i < MENU_SLOTS; i++) {
        CHECK_EQ(shown(&v.slot[i]), i < 5);
        CHECK(!shown(&v.slot[i]) || gfx_rect_contains_rect(m.area, gfx_sprite_bounds(&v.slot[i])));
    }
    CHECK(!shown(&v.back) && shown(&v.title) && shown(&v.face) && shown(&v.footer) && !shown(&v.led));
    // Moods, with relaxed set from the companion: its tile carries the light.
    menu_open(&m, MENU_MOODS, 5000);
    st.mood = BUDDY_MOOD_RELAXED;
    menu_view_apply(&v, &m, &st, 6000);
    CHECK(shown(&v.back));
    CHECK(shown(&v.led));
    const int relaxed = slot_of(&m, &st, "mood.relaxed");
    CHECK(gfx_rect_contains_rect(gfx_sprite_bounds(&v.slot[relaxed]), gfx_sprite_bounds(&v.led)));
    // The companion changes it to happy: the light follows, and the face changes with it.
    const int happy = slot_of(&m, &st, "mood.happy");
    st.mood = BUDDY_MOOD_HAPPY;
    gfx_dirty_t changes;
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_scene_collect(&scene, &changes); // commit what is shown
    gfx_dirty_clear(&changes);
    menu_view_apply(&v, &m, &st, 6100);
    CHECK(gfx_rect_contains_rect(gfx_sprite_bounds(&v.slot[happy]), gfx_sprite_bounds(&v.led)));
    gfx_scene_collect(&scene, &changes);
    // Redrawn: the two tiles, the light, the face and the footer, all inside the panel, far less than the panel.
    CHECK(changes.count > 0);
    for (int i = 0; i < changes.count; i++) {
        CHECK(changes.rect[i].x >= layout.panel.x);
    }
    CHECK(gfx_dirty_area(&changes) < layout.panel.w * layout.panel.h / 2);
    CHECK_EQ(v.face_shown, BUDDY_MOOD_HAPPY);

    // A Monster sequence: the face charges (lightning eyes).
    st.monster = true;
    menu_view_apply(&v, &m, &st, 6200);
    CHECK_EQ(v.face_shown, BUDDY_MOOD_COUNT);

    // Clock Mode hides everything; back, everything is drawn again as it is then.
    menu_view_show(&v, false);
    CHECK_EQ(menu_view_apply(&v, &m, &st, 6300), UINT32_MAX);
    for (int i = 0; i < MENU_SLOTS; i++) {
        CHECK(!shown(&v.slot[i]));
    }
    CHECK(!shown(&v.back) && !shown(&v.title) && !shown(&v.face) && !shown(&v.footer) && !shown(&v.led));
    menu_view_show(&v, true);
    st.monster = false;
    menu_view_apply(&v, &m, &st, 9000);
    CHECK(shown(&v.slot[0]) && shown(&v.back) && shown(&v.title) && shown(&v.face) && shown(&v.footer));
    CHECK_EQ(v.face_shown, BUDDY_MOOD_HAPPY);
}

// The render loop's way, with two alternating buffers repaired from dirty rectangles: browse the menu, pick moods,
// turn pages, run into a Monster. Every buffer must equal a full redraw, and every redraw stays in the panel.
void test_menu_redraws_match_full_redraw(void)
{
    static gfx_scene_t scene;
    static menu_t m;
    static menu_view_t v;
    static gfx_dirty_t changes, redraw, all;
    static gfx_repair_t repair;
    static layout_t layout;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(menu_init(&m, &menu_default, layout_panel_content(&layout), 0));
    CHECK(menu_view_init(&v, &scene, &m, s_mem, sizeof(s_mem), 10));
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
    memset(s_buf, 0, sizeof(s_buf));
    const gfx_surface_t ref = {s_ref, W, H, W};

    // What happens when (ms): taps on items by id, Back, page arrows, Monster on/off.
    typedef struct {
        uint32_t at;
        const char *what;
    } step_t;
    static const step_t k_script[] = {
        {500, "moods"},         {900, "mood.peukie"},  {1300, "mood.working"},   {1700, "mood.working"},
        {2000, "back"},         {2400, "activities"},  {2800, "activity.surprise"}, {3100, "monster+"},
        {3400, "back"},         {3800, "food"},        {4200, "food.water"},     {4600, "monster-"},
        {5000, "food.monster"}, {5400, "back"},        {5800, "moods"},          {6200, "mood.idle"},
    };
    menu_status_t st = k_idle;
    unsigned next_step = 0;
    int back = 0, frames = 0, bad = 0, max_area = 0;
    for (uint32_t t = 0, k = 0; t < 7000; k++) {
        while (next_step < sizeof(k_script) / sizeof(k_script[0]) && k_script[next_step].at <= t) {
            const char *what = k_script[next_step++].what;
            if (strcmp(what, "monster+") == 0 || strcmp(what, "monster-") == 0) {
                st.monster = st.busy = what[7] == '+';
            } else if (strcmp(what, "back") == 0) {
                int x, y;
                back_center(&m, &x, &y);
                tap_at(&m, x, y, &st, t);
            } else {
                const int slot = slot_of(&m, &st, what);
                CHECK(slot >= 0);
                int x, y;
                slot_center(&m, slot, &x, &y);
                menu_touch(&m, MENU_TOUCH_DOWN, x, y, &st, t);
                const menu_result_t r = menu_touch(&m, MENU_TOUCH_UP, x, y, &st, t);
                if (r.action == MENU_DO_MOOD) {
                    st.mood = (buddy_mood_t)r.arg;
                }
            }
        }
        uint32_t next = menu_view_apply(&v, &m, &st, t);
        gfx_dirty_clear(&changes);
        gfx_scene_collect(&scene, &changes);
        if (changes.count > 0) {
            gfx_repair_next(&repair, &changes, &redraw);
            const gfx_surface_t dst = {s_buf[back], W, H, W};
            gfx_scene_render(&scene, &redraw, &dst);
            gfx_scene_render(&scene, &all, &ref);
            bad += memcmp(s_buf[back], s_ref, sizeof(s_ref)) != 0;
            if (++frames > 2) { // after the two boot-time full redraws
                for (int i = 0; i < redraw.count; i++) {
                    CHECK(gfx_rect_contains_rect(layout.panel, redraw.rect[i])); // the stage is never touched
                }
                const int area = gfx_dirty_area(&redraw);
                max_area = area > max_area ? area : max_area;
            }
            back ^= 1;
        }
        next = next < 40 ? next : 40;
        t += next + (k % 3 == 0 ? 3 : 0);
    }
    CHECK_EQ(next_step, sizeof(k_script) / sizeof(k_script[0]));
    CHECK(frames > 50);
    CHECK_EQ(bad, 0);
    const int panel = layout.panel.w * layout.panel.h;
    printf("    menu: %d frames, largest redraw %d px (panel %d px)\n", frames, max_area, panel);
    // Page changes flip tile by tile, never the whole panel at once. (Back from a full Activities page is the most:
    // the first rows of Home make the old tiles they overlap give way.)
    CHECK(max_area < panel * 2 / 3);
}
