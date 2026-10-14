// Renders DeskBuddy's screens to PNG files, using the firmware's own layout, artwork, menu and compositor. A first
// step towards a desktop simulator: lets the art be reviewed without the device.
//
//   buddy_snapshot [output_dir]
//
// Writes buddy_<pose>.png, monster_<ms>.png at the key moments of the Monster sequence, monster_sheet.png (the
// stage every 200 ms of the sequence, at half size, on one sheet), mood_sheet.png (one row per mood: the stage
// every MOOD_STEP_MS of its loop, as the brain plays it), the touchscreen menu (menu_<screen>.png, and
// menu_sheet.png: the control panel in every state side by side), the message marquee (marquee_intro.png,
// marquee.png, office_marquee.png) and the Office Mode clock: office_clock.png and office_not_set.png.
#include <stdio.h>
#include <string.h>

#include "buddy_brain.h"
#include "buddy_monster.h"
#include "buddy_view.h"
#include "layout.h"
#include "marquee.h"
#include "menu.h"
#include "menu_view.h"
#include "office_clock.h"
#include "png.h"

enum { W = 1024, H = 600, ALIGN = 32, STAGE_PERCENT = 65 }; // firmware defaults (Kconfig)

// Sheet tiles: this part of the stage (screen pixels), halved.
enum { CROP_X = 64, CROP_Y = 32, CROP_W = 576, CROP_H = 480, COLS = 6, SHEET_STEP_MS = 200 };
enum { TILE_W = CROP_W / 2, TILE_H = CROP_H / 2 };
enum { TILES = (BUDDY_MONSTER_MS + SHEET_STEP_MS - 1) / SHEET_STEP_MS, ROWS = (TILES + COLS - 1) / COLS };
enum { MOOD_COLS = 12, MOOD_STEP_MS = 700, MOOD_ROWS = BUDDY_MOOD_COUNT - 1 }; // every mood but idle
enum { PANELS = 18, PANEL_COLS = 6, PANEL_GAP = 8 }; // the menu sheet

static gfx_color_t s_px[W * H];
static gfx_color_t s_sheet[COLS * TILE_W * ROWS * TILE_H];
static gfx_color_t s_mood_sheet[MOOD_COLS * TILE_W * MOOD_ROWS * TILE_H];
static gfx_color_t s_panels[(PANEL_COLS * (352 + PANEL_GAP)) * ((PANELS / PANEL_COLS) * (H + PANEL_GAP))];

static gfx_scene_t s_scene;
static layout_t s_layout;
static buddy_view_t s_view;
static menu_t s_menu;
static menu_view_t s_menu_view;
static gfx_dirty_t s_all;
static const char *s_dir;
static int s_panel_count;

// Copies the stage crop of the last render, halved, into a sheet tile with a white top/left border.
static void tile(gfx_color_t *sheet, int sheet_w, int col, int row)
{
    const int ox = col * TILE_W, oy = row * TILE_H;
    for (int y = 0; y < TILE_H; y++) {
        for (int x = 0; x < TILE_W; x++) {
            const bool border = x == 0 || y == 0;
            sheet[(size_t)(oy + y) * sheet_w + ox + x] =
                border ? GFX_RGB(255, 255, 255) : s_px[(size_t)(CROP_Y + 2 * y) * W + CROP_X + 2 * x];
        }
    }
}

static bool write(const char *name, const gfx_surface_t *s)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", s_dir, name);
    if (!png_write_rgb565(path, s)) {
        fprintf(stderr, "cannot write %s\n", path);
        return false;
    }
    printf("%s\n", path);
    return true;
}

// Renders the whole screen and writes it.
static bool shot(const char *name)
{
    const gfx_surface_t dst = {s_px, W, H, W};
    gfx_scene_render(&s_scene, &s_all, &dst);
    return write(name, &dst);
}

// Buddy in a mood, as the brain plays it at t ms into it.
static void stage_mood(buddy_mood_t mood, uint32_t t)
{
    buddy_brain_t brain;
    buddy_brain_init(&brain, 0, 7);
    buddy_brain_set_mood(&brain, mood, 0, 0);
    buddy_pose_t pose;
    for (uint32_t ms = 0; ms <= t; ms += 10) {
        buddy_brain_tick(&brain, ms, &pose);
    }
    buddy_view_apply(&s_view, &pose, t);
}

// A screen of the menu: renders it, writes it, and adds the control panel to the menu sheet.
static bool menu_shot(const char *name, const menu_status_t *st, uint32_t t)
{
    menu_view_apply(&s_menu_view, &s_menu, st, t);
    char file[64];
    snprintf(file, sizeof(file), "menu_%s", name);
    if (!shot(file)) {
        return false;
    }
    if (s_panel_count < PANELS) {
        const int sheet_w = PANEL_COLS * (s_layout.panel.w + PANEL_GAP);
        const int ox = (s_panel_count % PANEL_COLS) * (s_layout.panel.w + PANEL_GAP);
        const int oy = (s_panel_count / PANEL_COLS) * (H + PANEL_GAP);
        for (int y = 0; y < H; y++) {
            memcpy(&s_panels[(size_t)(oy + y) * sheet_w + ox], &s_px[(size_t)y * W + s_layout.panel.x],
                   (size_t)s_layout.panel.w * sizeof(gfx_color_t));
        }
        s_panel_count++;
    }
    return true;
}

// Screen point in the middle of the menu item with this id on the current page.
static bool item_point(const menu_status_t *st, const char *id, int *x, int *y)
{
    const menu_item_t *items[MENU_SLOTS];
    const int n = menu_page_items(&s_menu, st, items, NULL);
    for (int i = 0; i < n; i++) {
        if (strcmp(items[i]->id, id) == 0) {
            const gfx_rect_t r =
                menu_slot_rect((menu_layout_t)s_menu.def->categories[s_menu.category].layout, i);
            *x = s_menu.area.x + (r.x + r.w / 2) * MENU_SCALE;
            *y = s_menu.area.y + (r.y + r.h / 2) * MENU_SCALE;
            return true;
        }
    }
    fprintf(stderr, "no menu item %s here\n", id);
    return false;
}

static bool tap(const menu_status_t *st, const char *id, uint32_t t)
{
    int x, y;
    if (!item_point(st, id, &x, &y)) {
        return false;
    }
    menu_touch(&s_menu, MENU_TOUCH_DOWN, x, y, st, t);
    menu_touch(&s_menu, MENU_TOUCH_UP, x, y, st, t + 40);
    return true;
}

// The menu's screens, driven by touches as on the device.
static bool menu_screens(void)
{
    menu_status_t st = {BUDDY_MOOD_IDLE, false, true};
    uint32_t t = 10000;
    stage_mood(BUDDY_MOOD_IDLE, 0);
    if (!menu_shot("home", &st, t)) {
        return false;
    }
    // Opening Moods: the tiles flip in one by one.
    if (!tap(&st, "moods", t += 1000)) {
        return false;
    }
    if (!menu_shot("flip", &st, t + 40 + 3 * MENU_FLIP_MS + 10) || !menu_shot("moods_idle", &st, t += 1000)) {
        return false;
    }
    // Relaxed, picked on the screen: Buddy relaxes, the tile lights up, then stays active.
    if (!tap(&st, "mood.relaxed", t += 1000)) {
        return false;
    }
    st.mood = BUDDY_MOOD_RELAXED;
    stage_mood(BUDDY_MOOD_RELAXED, 2000);
    if (!menu_shot("moods_tapped", &st, t + 100) || !menu_shot("moods", &st, t += 1000)) {
        return false;
    }
    // A finger on Peukie, not lifted yet.
    int x, y;
    if (!item_point(&st, "mood.peukie", &x, &y)) {
        return false;
    }
    menu_touch(&s_menu, MENU_TOUCH_DOWN, x, y, &st, t += 1000);
    if (!menu_shot("moods_press", &st, t + 20)) {
        return false;
    }
    menu_cancel(&s_menu);
    // Every mood as the active one, Buddy in it (the companion may have set it).
    static const buddy_mood_t k_moods[] = {BUDDY_MOOD_PEUKIE, BUDDY_MOOD_ANGRY, BUDDY_MOOD_SLEEPY,
                                           BUDDY_MOOD_WORKING, BUDDY_MOOD_HAPPY};
    for (unsigned i = 0; i < sizeof(k_moods) / sizeof(k_moods[0]); i++) {
        st.mood = k_moods[i];
        stage_mood(k_moods[i], 2100);
        char name[32];
        snprintf(name, sizeof(name), "moods_%s", buddy_mood_name(k_moods[i]));
        if (!menu_shot(name, &st, t += 1000)) {
            return false;
        }
    }
    st.mood = BUDDY_MOOD_RELAXED;
    stage_mood(BUDDY_MOOD_RELAXED, 2000);

    // Activities, then Food & Drink (with a tap on a planned item), then the same during a Monster sequence.
    menu_open(&s_menu, MENU_ACTIVITIES, t += 1000);
    if (!menu_shot("activities", &st, t += 1000)) {
        return false;
    }
    menu_open(&s_menu, MENU_FOOD, t += 1000);
    if (!menu_shot("food", &st, t += 1000) || !tap(&st, "food.water", t += 1000) ||
        !menu_shot("food_soon", &st, t + 60)) {
        return false;
    }
    st.monster = true;
    buddy_pose_t pose;
    buddy_monster_pose(3300, &pose);
    buddy_view_apply(&s_view, &pose, 3300);
    if (!menu_shot("food_monster", &st, t += 1000)) {
        return false;
    }
    st.monster = false;
    stage_mood(BUDDY_MOOD_RELAXED, 2000);

    // More items than fit: pages (a made-up category; the real ones fit on one page so far).
    static const menu_category_def_t k_cats[MENU_CATEGORY_COUNT] = {
        [MENU_HOME] = {"DESKBUDDY", MENU_LIST, MENU_HOME},
        [MENU_MOODS] = {"MOODS +", MENU_GRID, MENU_HOME},
    };
#define MORE(n, m) {"more." #n, #n, MENU_MOODS, MENU_ICON_MOOD, MENU_DO_MOOD, m, false}
    static const menu_item_t k_items[] = {
        MORE(IDLE, 0),    MORE(RELAXED, 1), MORE(PEUKIE, 2), MORE(ANGRY, 3), MORE(SLEEPY, 4),  MORE(WORKING, 5),
        MORE(HAPPY, 6),   MORE(IDLE 2, 0),  MORE(CALM, 1),   MORE(SMOKE, 2), MORE(GRUMPY, 3),
    };
#undef MORE
    static const menu_def_t k_def = {k_cats, k_items, (int)(sizeof(k_items) / sizeof(k_items[0]))};
    const menu_t real = s_menu;
    s_menu.def = &k_def;
    menu_open(&s_menu, MENU_MOODS, t += 1000);
    if (!menu_shot("pages_1", &st, t += 1000)) {
        return false;
    }
    menu_touch(&s_menu, MENU_TOUCH_DOWN, s_menu.area.x + s_menu.area.w - 30, s_menu.area.y + s_menu.area.h - 26,
               &st, t += 1000);
    menu_touch(&s_menu, MENU_TOUCH_UP, s_menu.area.x + s_menu.area.w - 30, s_menu.area.y + s_menu.area.h - 26, &st,
               t + 40);
    if (!menu_shot("pages_2", &st, t += 1000)) {
        return false;
    }
    s_menu = real;
    menu_open(&s_menu, MENU_HOME, t += 1000);
    menu_view_apply(&s_menu_view, &s_menu, &st, t += 1000);

    const int sheet_w = PANEL_COLS * (s_layout.panel.w + PANEL_GAP);
    const gfx_surface_t sheet = {s_panels, sheet_w, (PANELS / PANEL_COLS) * (H + PANEL_GAP), sheet_w};
    return write("menu_sheet", &sheet);
}

int main(int argc, char **argv)
{
    s_dir = argc > 1 ? argv[1] : ".";
    static gfx_color_t strip[16 * 1024];
    static office_clock_t clock;
    static uint8_t clock_mem[OFFICE_CLOCK_MEMORY];
    static uint8_t menu_mem[MENU_VIEW_MEMORY];
    static marquee_t marquee;
    s_layout = layout_compute(W, H, STAGE_PERCENT, false, ALIGN);
    if (!gfx_scene_init(&s_scene, W, H, layout_background, &s_layout, strip, 16 * 1024, ALIGN) ||
        !buddy_view_init(&s_view, &s_scene, s_layout.stage, 10, 0) ||
        !menu_init(&s_menu, &menu_default, layout_panel_content(&s_layout), 0) ||
        !menu_view_init(&s_menu_view, &s_scene, &s_menu, menu_mem, sizeof(menu_mem), 10) ||
        !office_clock_init(&clock, &s_scene, s_layout.screen, clock_mem, sizeof(clock_mem), 20) ||
        !marquee_init(&marquee, &s_scene, 30)) {
        fprintf(stderr, "scene setup failed\n");
        return 1;
    }
    gfx_dirty_init(&s_all, W, H, ALIGN);
    gfx_dirty_add(&s_all, (gfx_rect_t){0, 0, W, H});
    const menu_status_t idle = {BUDDY_MOOD_IDLE, false, true};
    menu_view_apply(&s_menu_view, &s_menu, &idle, 10000); // Home, settled
    const struct {
        const char *name;
        buddy_pose_t pose;
    } shots[] = {
#define POSE(st, e, m, a, l) {.state = (st), .eyes = (e), .mouth = (m), .antenna = (a), .lift = (l)}
        {"idle", POSE(BUDDY_STATE_IDLE, BUDDY_EYES_OPEN, BUDDY_MOUTH_NEUTRAL, BUDDY_ANTENNA_CALM, 0)},
        {"blink", POSE(BUDDY_STATE_IDLE, BUDDY_EYES_CLOSED, BUDDY_MOUTH_NEUTRAL, BUDDY_ANTENNA_CALM, 1)},
        {"surprised", POSE(BUDDY_STATE_SURPRISED, BUDDY_EYES_WIDE, BUDDY_MOUTH_OPEN, BUDDY_ANTENNA_EXCITED, 3)},
        {"happy", POSE(BUDDY_STATE_HAPPY, BUDDY_EYES_HAPPY, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_EXCITED, 4)},
#undef POSE
    };
    for (unsigned i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        buddy_view_apply(&s_view, &shots[i].pose, 0);
        char name[64];
        snprintf(name, sizeof(name), "buddy_%s", shots[i].name);
        if (!shot(name)) {
            return 1;
        }
    }

    // Key moments of the Monster sequence, full screen (the menu says he is charging meanwhile).
    static const uint32_t k_moments[] = {150, 700, 1000, 1500, 1750, 2000, 2300, 2560, 2700, 3300, 3900, 4400,
                                         4560, 4630, 5000, 5340, 5700, 6500, 6790};
    const menu_status_t charging = {BUDDY_MOOD_IDLE, true, true};
    menu_view_apply(&s_menu_view, &s_menu, &charging, 10000);
    for (unsigned i = 0; i < sizeof(k_moments) / sizeof(k_moments[0]); i++) {
        buddy_pose_t pose;
        buddy_monster_pose(k_moments[i], &pose);
        buddy_view_apply(&s_view, &pose, k_moments[i]);
        char name[64];
        snprintf(name, sizeof(name), "monster_%04u", (unsigned)k_moments[i]);
        if (!shot(name)) {
            return 1;
        }
    }

    // Contact sheet.
    const gfx_surface_t dst = {s_px, W, H, W};
    const int sheet_w = COLS * TILE_W;
    memset(s_sheet, 0, sizeof(s_sheet));
    for (int i = 0; i < TILES; i++) {
        const uint32_t t = (uint32_t)i * SHEET_STEP_MS;
        buddy_pose_t pose;
        buddy_monster_pose(t, &pose);
        buddy_view_apply(&s_view, &pose, t);
        gfx_scene_render(&s_scene, &s_all, &dst);
        tile(s_sheet, sheet_w, i % COLS, i / COLS);
    }
    const gfx_surface_t sheet = {s_sheet, sheet_w, ROWS * TILE_H, sheet_w};
    if (!write("monster_sheet", &sheet)) {
        return 1;
    }

    // The moods, each played by the brain from its start (ticked like the render loop, captured every step).
    menu_view_apply(&s_menu_view, &s_menu, &idle, 10000);
    const int mood_w = MOOD_COLS * TILE_W;
    for (int m = 1; m < BUDDY_MOOD_COUNT; m++) {
        buddy_brain_t brain;
        buddy_brain_init(&brain, 0, 7);
        buddy_brain_set_mood(&brain, (buddy_mood_t)m, 0, 0);
        buddy_pose_t pose;
        for (uint32_t t = 0, col = 0; col < MOOD_COLS; t += 10) {
            buddy_brain_tick(&brain, t, &pose);
            if (t == col * MOOD_STEP_MS) {
                buddy_view_apply(&s_view, &pose, t);
                gfx_scene_render(&s_scene, &s_all, &dst);
                tile(s_mood_sheet, mood_w, (int)col, m - 1);
                col++;
            }
        }
    }
    const gfx_surface_t mood_sheet = {s_mood_sheet, mood_w, MOOD_ROWS * TILE_H, mood_w};
    if (!write("mood_sheet", &mood_sheet)) {
        return 1;
    }

    if (!menu_screens()) {
        return 1;
    }

    // The message marquee over the stage, as the firmware places it: its scanner sweep, then mid-scroll over the
    // surprised robot (a message makes him jump).
    const int mx = s_layout.stage.x + (s_layout.stage.w - MARQUEE_W) / 2, my = 22;
    buddy_view_apply(&s_view, &shots[2].pose, 0);
    marquee_start(&marquee, "Koffie om 15:00? Taart: \xE2\x82\xAC" "5,- \xE2\x99\xA5 caf\xC3\xA9 \xF0\x9F\x98\x80!", 0);
    marquee_apply(&marquee, mx, my, MARQUEE_INTRO_MS / 2);
    if (!shot("marquee_intro")) {
        return 1;
    }
    marquee_apply(&marquee, mx, my, MARQUEE_INTRO_MS + 150 * MARQUEE_COL_MS);
    if (!shot("marquee")) {
        return 1;
    }

    // Office Mode, as the firmware switches to it.
    buddy_view_hide(&s_view);
    menu_view_show(&s_menu_view, false);
    office_clock_show(&clock, true);
    s_scene.background = office_background;
    s_scene.background_ctx = NULL;
    const struct tm tm = {.tm_year = 126, .tm_mon = 9, .tm_mday = 14, .tm_wday = 3, .tm_hour = 9, .tm_min = 41};
    office_clock_apply(&clock, &tm, 100);
    marquee_apply(&marquee, (W - MARQUEE_W) / 2, H - MARQUEE_H - 36, MARQUEE_INTRO_MS + 120 * MARQUEE_COL_MS);
    if (!shot("office_marquee")) {
        return 1;
    }
    marquee_apply(&marquee, 0, 0, marquee_duration_ms(&marquee)); // over: hidden
    if (!shot("office_clock")) {
        return 1;
    }
    office_clock_apply(&clock, NULL, 0);
    return shot("office_not_set") ? 0 : 1;
}
