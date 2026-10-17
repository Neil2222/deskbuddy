// Renders DeskBuddy's screens to PNG files, using the firmware's own layout, artwork, menu and compositor. A first
// step towards a desktop simulator: lets the art be reviewed without the device.
//
//   buddy_snapshot [output_dir]
//
// Writes buddy_<pose>.png, monster_<ms>.png at the key moments of the Monster sequence, monster_sheet.png (the
// stage every 200 ms of the sequence, at half size, on one sheet), mood_sheet.png (one row per mood: the stage
// every MOOD_STEP_MS of its loop, as the brain plays it), the touchscreen menu (menu_<screen>.png, and
// menu_sheet.png: the control panel in every state side by side), the message marquee (marquee_intro.png,
// marquee.png, office_marquee.png), the Clock Mode clock: office_clock.png and office_not_set.png, and the
// Mechanical Skeleton face (skeleton_*.png: the face, its tick positions, the minute roll, the hourly flourish,
// a close-up, the menu page that picks it, and the zoom arriving at it), Tic-Tac-Toe (ttt_*.png and ttt_sheet.png:
// the game's page, Buddy watching, thinking, winning, losing, puzzled by a draw, and asking for a rematch), and
// Missile Defense (md_*.png and md_sheet.png: his alert on the stage, then the whole screen: a wave starting, aiming,
// firing, a desk item lost, a missile on his head, overheated eyes, panic, a wave cleared, the game over).
#include <stdio.h>
#include <string.h>

#include "buddy_activity.h"
#include "buddy_brain.h"
#include "buddy_game.h"
#include "buddy_kitt.h"
#include "buddy_monster.h"
#include "buddy_view.h"
#include "layout.h"
#include "marquee.h"
#include "md.h"
#include "md_buddy.h"
#include "md_view.h"
#include "menu.h"
#include "menu_ask.h"
#include "menu_thought.h"
#include "menu_ttt.h"
#include "menu_view.h"
#include "office_faces.h"
#include "office_zoom.h"
#include "png.h"

enum { W = 1024, H = 600, ALIGN = 32, STAGE_PERCENT = 65 }; // firmware defaults (Kconfig)

// Sheet tiles: this part of the stage (screen pixels), halved.
enum { CROP_X = 64, CROP_Y = 32, CROP_W = 576, CROP_H = 480, COLS = 6, SHEET_STEP_MS = 200 };
enum { TILE_W = CROP_W / 2, TILE_H = CROP_H / 2 };
enum { TILES = (BUDDY_MONSTER_MS + SHEET_STEP_MS - 1) / SHEET_STEP_MS, ROWS = (TILES + COLS - 1) / COLS };
enum { MOOD_COLS = 12, MOOD_STEP_MS = 700, MOOD_ROWS = BUDDY_MOOD_COUNT - 1 }; // every mood but idle
enum { PANELS = 18, PANEL_COLS = 6, PANEL_GAP = 8 }; // the menu sheet
enum { ACT_COLS = 16, ACT_ROWS = 4 };               // the activity sheet: each activity in ACT_COLS even steps

static gfx_color_t s_px[W * H];
static gfx_color_t s_sheet[COLS * TILE_W * ROWS * TILE_H];
static gfx_color_t s_mood_sheet[MOOD_COLS * TILE_W * MOOD_ROWS * TILE_H];
static gfx_color_t s_act_sheet[ACT_COLS * TILE_W * ACT_ROWS * TILE_H];
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
    menu_status_t st = {.mood = BUDDY_MOOD_IDLE, .office = true};
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
    if (!menu_shot("food", &st, t += 1000) || !tap(&st, "food.coffee", t += 1000) ||
        !menu_shot("food_soon", &st, t + 60)) {
        return false;
    }
    st.monster = st.busy = true;
    st.doing = "charging!";
    buddy_pose_t pose;
    buddy_monster_pose(3300, &pose);
    buddy_view_apply(&s_view, &pose, 3300);
    if (!menu_shot("food_monster", &st, t += 1000)) {
        return false;
    }
    st.monster = st.busy = false;
    st.doing = NULL;
    stage_mood(BUDDY_MOOD_RELAXED, 2000);

    // MY DAY: what you are doing (it reports to the companion; he only cheers).
    menu_open(&s_menu, MENU_MYDAY, t += 1000);
    if (!menu_shot("myday", &st, t += 1000)) {
        return false;
    }

    // PDC Manager, offered by the companion: a row on Home, its page with the counter, dimmed while a click is under
    // way or cooling down, and without a known counter.
    st.pdc = st.pdc_ready = true;
    st.pdc_count = 123;
    menu_open(&s_menu, MENU_HOME, t += 1000);
    if (!menu_shot("home_pdc", &st, t += 1000)) {
        return false;
    }
    menu_open(&s_menu, MENU_PDC, t += 1000);
    if (!menu_shot("pdc", &st, t += 1000)) {
        return false;
    }
    st.pdc_ready = false;
    if (!menu_shot("pdc_wait", &st, t += 1000)) {
        return false;
    }
    st.pdc_ready = true;
    st.pdc_count = -1;
    if (!menu_shot("pdc_nocount", &st, t += 1000)) {
        return false;
    }
    st.pdc = st.pdc_ready = false;
    st.pdc_count = -1;
    menu_open(&s_menu, MENU_HOME, t += 1000);

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

// --- The zoom into Clock Mode and back ---------------------------------------------------------------------------

enum { ZOOM_STEP_MS = 40, ZOOM_COLS = 6, ZOOM_TILE_W = W / 2, ZOOM_TILE_H = H / 2 };
enum { ZOOM_ROWS = (OFFICE_LEAVE_MS / ZOOM_STEP_MS + 2 + ZOOM_COLS - 1) / ZOOM_COLS };
static gfx_color_t s_zoom_sheet[ZOOM_COLS * ZOOM_TILE_W * ZOOM_ROWS * ZOOM_TILE_H];

// Adds the last full render, halved, as tile i of the zoom sheet (white top/left border).
static void zoom_tile(int i)
{
    const int sheet_w = ZOOM_COLS * ZOOM_TILE_W, ox = (i % ZOOM_COLS) * ZOOM_TILE_W, oy = (i / ZOOM_COLS) * ZOOM_TILE_H;
    for (int y = 0; y < ZOOM_TILE_H; y++) {
        for (int x = 0; x < ZOOM_TILE_W; x++) {
            s_zoom_sheet[(size_t)(oy + y) * sheet_w + ox + x] =
                x == 0 || y == 0 ? GFX_RGB(255, 255, 255) : s_px[(size_t)(2 * y) * W + 2 * x];
        }
    }
}

static bool zoom_sheet(const char *name, int tiles)
{
    const int sheet_w = ZOOM_COLS * ZOOM_TILE_W;
    const gfx_surface_t sheet = {s_zoom_sheet, sheet_w, (tiles + ZOOM_COLS - 1) / ZOOM_COLS * ZOOM_TILE_H, sheet_w};
    return write(name, &sheet);
}

// Draws Buddy and the menu t ms into a zoom, as the firmware does, and renders the screen.
static void zoom_render(const office_zoom_t *z, const menu_status_t *st, uint32_t t)
{
    const gfx_surface_t dst = {s_px, W, H, W};
    office_camera_t cam;
    buddy_pose_t pose;
    office_zoom_frame(z, t, (office_camera_t){s_view.home_scale, s_view.home_x0, s_view.home_y0}, s_layout.screen,
                      &cam, &pose);
    buddy_view_camera(&s_view, cam.scale, cam.x0, cam.y0);
    buddy_view_apply(&s_view, &pose, t);
    menu_view_apply(&s_menu_view, &s_menu, st, t);
    gfx_scene_render(&s_scene, &s_all, &dst);
}

// Clock from the menu's Activities page with Buddy relaxed, then a tap on the clock: every ZOOM_STEP_MS of both ways
// on a sheet (zoom_in_sheet.png, zoom_out_sheet.png, the clock in between), and full-size key moments.
static bool zoom_screens(office_faces_t *faces)
{
    const menu_status_t st = {.mood = BUDDY_MOOD_RELAXED, .office = true};
    office_faces_show(faces, false);
    s_scene.background = layout_background;
    s_scene.background_ctx = &s_layout;
    menu_view_show(&s_menu_view, true);
    menu_open(&s_menu, MENU_ACTIVITIES, 0);
    menu_view_apply(&s_menu_view, &s_menu, &st, 5000);

    const uint32_t t0 = 10000;
    office_zoom_t z = {.state = OFFICE_ZOOM_ENTERING, .want_office = true, .start = t0};
    menu_view_close(&s_menu_view, t0);
    static const uint32_t k_in_keys[] = {60, 500, 640, 780, 940, 1050, 1250};
    int n = 0;
    for (uint32_t t = 0; t < OFFICE_ENTER_MS; t += ZOOM_STEP_MS) {
        zoom_render(&z, &st, t0 + t);
        zoom_tile(n++);
    }
    for (unsigned i = 0; i < sizeof(k_in_keys) / sizeof(k_in_keys[0]); i++) {
        zoom_render(&z, &st, t0 + k_in_keys[i]);
        char name[32];
        snprintf(name, sizeof(name), "zoom_in_%04u", (unsigned)k_in_keys[i]);
        if (!shot(name)) {
            return false;
        }
    }
    // The clock takes his dark face's place.
    buddy_view_hide(&s_view);
    menu_view_show(&s_menu_view, false);
    office_faces_show(faces, true);
    s_scene.background = office_faces_background(faces, &s_scene.background_ctx);
    if (!shot("zoom_clock")) {
        return false;
    }
    zoom_tile(n++);
    if (!zoom_sheet("zoom_in_sheet", n)) {
        return false;
    }

    // A tap: his dark face, then he wakes up and steps back; the menu comes back on its page.
    office_faces_show(faces, false);
    s_scene.background = layout_background;
    s_scene.background_ctx = &s_layout;
    zoom_tile(0);
    z = (office_zoom_t){.state = OFFICE_ZOOM_LEAVING, .running = true, .start = t0};
    static const uint32_t k_out_keys[] = {200, 300, 600, 800, 1000, 1200, 1420};
    n = 1;
    bool menu_back = false;
    for (uint32_t t = 0; t < OFFICE_LEAVE_MS + ZOOM_STEP_MS; t += ZOOM_STEP_MS) {
        if (!menu_back && t >= OFFICE_MENU_BACK_AT) {
            menu_view_show(&s_menu_view, true);
            menu_reveal(&s_menu, t0 + t);
            menu_back = true;
        }
        zoom_render(&z, &st, t0 + t);
        zoom_tile(n++);
    }
    if (!zoom_sheet("zoom_out_sheet", n)) {
        return false;
    }
    menu_view_show(&s_menu_view, false); // the key moments: the menu as it is at each of them
    for (unsigned i = 0; i < sizeof(k_out_keys) / sizeof(k_out_keys[0]); i++) {
        if (k_out_keys[i] >= OFFICE_MENU_BACK_AT && !s_menu_view.visible) {
            menu_view_show(&s_menu_view, true);
            menu_reveal(&s_menu, t0 + OFFICE_MENU_BACK_AT);
        }
        zoom_render(&z, &st, t0 + k_out_keys[i]);
        char name[32];
        snprintf(name, sizeof(name), "zoom_out_%04u", (unsigned)k_out_keys[i]);
        if (!shot(name)) {
            return false;
        }
    }
    buddy_view_camera_home(&s_view);
    return true;
}

// The Mechanical Skeleton face at a time of day (ms into its second), rendered.
static void skeleton_at(office_faces_t *faces, int h, int m, int s, uint32_t ms)
{
    const struct tm tm = {.tm_year = 126, .tm_mon = 9, .tm_mday = 17, .tm_wday = 6, .tm_hour = h, .tm_min = m,
                          .tm_sec = s};
    office_faces_apply(faces, &tm, ms);
    const gfx_surface_t dst = {s_px, W, H, W};
    gfx_scene_render(&s_scene, &s_all, &dst);
}

enum { SK_COLS = 4, SK_TILE_W = W / 2, SK_TILE_H = H / 2 };
static gfx_color_t s_sk_sheet[SK_COLS * SK_TILE_W * 3 * SK_TILE_H];

// The last render, halved, as tile i of a SK_COLS-wide sheet.
static void sk_tile(int i)
{
    const int sheet_w = SK_COLS * SK_TILE_W, ox = (i % SK_COLS) * SK_TILE_W, oy = (i / SK_COLS) * SK_TILE_H;
    for (int y = 0; y < SK_TILE_H; y++) {
        for (int x = 0; x < SK_TILE_W; x++) {
            s_sk_sheet[(size_t)(oy + y) * sheet_w + ox + x] =
                x == 0 || y == 0 ? GFX_RGB(255, 255, 255) : s_px[(size_t)(2 * y) * W + 2 * x];
        }
    }
}

static bool sk_sheet(const char *name, int tiles)
{
    const int sheet_w = SK_COLS * SK_TILE_W;
    const gfx_surface_t sheet = {s_sk_sheet, sheet_w, (tiles + SK_COLS - 1) / SK_COLS * SK_TILE_H, sheet_w};
    return write(name, &sheet);
}

// A part of the last render, enlarged 2x (nearest neighbour): the pixel art up close.
static bool sk_closeup(const char *name, int x0, int y0)
{
    static gfx_color_t px[W * H];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            px[(size_t)y * W + x] = s_px[(size_t)(y0 + y / 2) * W + x0 + x / 2];
        }
    }
    const gfx_surface_t s = {px, W, H, W};
    return write(name, &s);
}

static bool skeleton_screens(office_faces_t *faces, marquee_t *marquee)
{
    buddy_view_hide(&s_view);
    menu_view_show(&s_menu_view, false);
    if (!office_faces_select(faces, OFFICE_FACE_SKELETON)) {
        fprintf(stderr, "no Mechanical Skeleton face\n");
        return false;
    }
    office_faces_show(faces, true);
    s_scene.background = office_faces_background(faces, &s_scene.background_ctx);
    skeleton_at(faces, 14, 37, 21, 300);
    if (!shot("skeleton_clock") || !sk_closeup("skeleton_closeup_left", 0, 40) ||
        !sk_closeup("skeleton_closeup_escapement", 256, 300)) {
        return false;
    }
    // Eight seconds: the escapement every second, the left train at 14:37:25, the right one at 14:37:30.
    for (int i = 0; i < 8; i++) {
        skeleton_at(faces, 14, 37, 23 + i, 300);
        sk_tile(i);
    }
    if (!sk_sheet("skeleton_ticks_sheet", 8)) {
        return false;
    }
    // 14:37:59 -> 14:38: the minute digit rolls up in its window.
    static const uint32_t k_roll[] = {30, 90};
    for (int i = 0; i < 2; i++) {
        skeleton_at(faces, 14, 38, 0, k_roll[i]);
        char name[32];
        snprintf(name, sizeof(name), "skeleton_roll_%d", i + 1);
        if (!shot(name)) {
            return false;
        }
    }
    // 19:59 -> 20:00: three digits roll, then the movement whirs for the hour.
    skeleton_at(faces, 20, 0, 0, 90);
    if (!shot("skeleton_roll_hour")) {
        return false;
    }
    int n = 0;
    for (uint32_t t = 0; t < OFFICE_SKELETON_FLOURISH_STEPS * OFFICE_SKELETON_FLOURISH_MS; t += 2 * OFFICE_SKELETON_FLOURISH_MS) {
        skeleton_at(faces, 20, 0, (int)(t / 1000), t % 1000);
        if (n == 3 && !shot("skeleton_flourish")) {
            return false;
        }
        sk_tile(n++);
    }
    if (!sk_sheet("skeleton_flourish_sheet", n)) {
        return false;
    }
    marquee_start(marquee, "Goodnight, Buddy!", 0);
    skeleton_at(faces, 22, 5, 9, 300);
    marquee_apply(marquee, (W - MARQUEE_W) / 2, H - MARQUEE_H - 36, MARQUEE_INTRO_MS + 60 * MARQUEE_COL_MS);
    if (!shot("skeleton_marquee")) {
        return false;
    }
    marquee_apply(marquee, 0, 0, marquee_duration_ms(marquee)); // over: hidden
    office_faces_apply(faces, NULL, 0);
    if (!shot("skeleton_not_set")) {
        return false;
    }
    // The CLOCK FACES page with Skeleton selected.
    office_faces_show(faces, false);
    s_scene.background = layout_background;
    s_scene.background_ctx = &s_layout;
    const buddy_pose_t rest = {.eyes = BUDDY_EYES_OPEN};
    buddy_view_apply(&s_view, &rest, 0);
    menu_view_show(&s_menu_view, true);
    const menu_status_t st = {.mood = BUDDY_MOOD_IDLE, .office = true, .faces = 3, .face = OFFICE_FACE_SKELETON};
    menu_open(&s_menu, MENU_ACTIVITIES, 0); // CLOCK, and CLOCK FACES next to it
    menu_view_apply(&s_menu_view, &s_menu, &st, 5000);
    if (!shot("skeleton_menu_activities")) {
        return false;
    }
    menu_open(&s_menu, MENU_CLOCKFACES, 0);
    menu_view_apply(&s_menu_view, &s_menu, &st, 5000);
    if (!shot("skeleton_menu")) {
        return false;
    }
    // Into the clock: his dark face, and the Skeleton taking its place.
    menu_view_close(&s_menu_view, 6000);
    office_zoom_t z = {.state = OFFICE_ZOOM_ENTERING, .want_office = true, .start = 6000};
    zoom_render(&z, &st, 6000 + OFFICE_ENTER_MS - 10);
    if (!shot("skeleton_zoom_dark_face")) {
        return false;
    }
    buddy_view_hide(&s_view);
    menu_view_show(&s_menu_view, false);
    office_faces_show(faces, true);
    s_scene.background = office_faces_background(faces, &s_scene.background_ctx);
    skeleton_at(faces, 9, 41, 5, 0);
    return shot("skeleton_zoom_arrived");
}

// KITT: the turn in, one round trip of the sweep and the turn back, as stage tiles (kitt_sheet.png) and as his head
// at full size (kitt_face_sheet.png); full screens at key moments, and the Moods page while he is KITT.
enum { KITT_COLS = 16, KITT_ROWS = 3, KITT_HEAD_X = 216, KITT_HEAD_Y = 188, KITT_HEAD_W = 240, KITT_HEAD_H = 152 };
static gfx_color_t s_kitt_face_sheet[KITT_COLS * KITT_HEAD_W * KITT_ROWS * KITT_HEAD_H];

// The phase's frame `col` of KITT_COLS: row 0 the turn in, row 1 a round trip, row 2 the turn back.
static void kitt_pose_at(int row, int col, buddy_pose_t *pose, uint32_t *t)
{
    const uint32_t out = BUDDY_KITT_IN_MS + 10u * BUDDY_KITT_SWEEP_MS;
    *t = row == 0   ? BUDDY_KITT_IN_MS * (uint32_t)col / (KITT_COLS - 1)
         : row == 1 ? BUDDY_KITT_IN_MS + BUDDY_KITT_SWEEP_MS * (uint32_t)col / KITT_COLS
                    : out + BUDDY_KITT_OUT_MS * (uint32_t)col / (KITT_COLS - 1);
    buddy_kitt_pose(*t, row == 2 ? out : 0, pose);
}

static const menu_status_t k_kitt_status = {.mood = BUDDY_MOOD_IDLE, .busy = true, .doing = "scanning",
                                            .office = true, .kitt = true};

static bool kitt_screens(const menu_status_t *idle)
{
    const menu_status_t kitt = k_kitt_status;
    const int act_w = KITT_COLS * TILE_W, face_w = KITT_COLS * KITT_HEAD_W;
    const gfx_surface_t dst = {s_px, W, H, W};
    for (int row = 0; row < KITT_ROWS; row++) {
        for (int col = 0; col < KITT_COLS; col++) {
            buddy_pose_t pose;
            uint32_t t;
            kitt_pose_at(row, col, &pose, &t);
            buddy_view_apply(&s_view, &pose, t);
            gfx_scene_render(&s_scene, &s_all, &dst);
            tile(s_act_sheet, act_w, col, row);
            for (int y = 0; y < KITT_HEAD_H; y++) {
                for (int x = 0; x < KITT_HEAD_W; x++) {
                    const bool border = x == 0 || y == 0;
                    s_kitt_face_sheet[(size_t)(row * KITT_HEAD_H + y) * face_w + col * KITT_HEAD_W + x] =
                        border ? GFX_RGB(255, 255, 255) : s_px[(size_t)(KITT_HEAD_Y + y) * W + KITT_HEAD_X + x];
                }
            }
        }
    }
    const gfx_surface_t sheet = {s_act_sheet, act_w, KITT_ROWS * TILE_H, act_w};
    const gfx_surface_t faces = {s_kitt_face_sheet, face_w, KITT_ROWS * KITT_HEAD_H, face_w};
    if (!write("kitt_sheet", &sheet) || !write("kitt_face_sheet", &faces)) {
        return false;
    }

    static const struct {
        uint32_t t;
        bool out;
        const char *name;
    } k_keys[] = {
        {300, false, "kitt_in_0300"},  {950, false, "kitt_in_0950"},  {1450, false, "kitt_in_1450"},
        {1800, false, "kitt_in_1800"}, {2000, false, "kitt_scan_middle"},
        {2000 + 400, false, "kitt_scan_right"}, {2000 + 1200, false, "kitt_scan_left"},
        {400, true, "kitt_out_0400"},  {1000, true, "kitt_out_1000"}, {1600, true, "kitt_out_1600"},
    };
    const uint32_t out = BUDDY_KITT_IN_MS + 10u * BUDDY_KITT_SWEEP_MS;
    menu_view_apply(&s_menu_view, &s_menu, &kitt, 60000);
    for (unsigned i = 0; i < sizeof(k_keys) / sizeof(k_keys[0]); i++) {
        buddy_pose_t pose;
        const uint32_t t = k_keys[i].out ? out + k_keys[i].t : k_keys[i].t;
        buddy_kitt_pose(t, k_keys[i].out ? out : 0, &pose);
        buddy_view_apply(&s_view, &pose, t);
        if (!shot(k_keys[i].name)) {
            return false;
        }
    }
    menu_view_apply(&s_menu_view, &s_menu, idle, 60000);
    const buddy_pose_t rest = {.eyes = BUDDY_EYES_OPEN};
    buddy_view_apply(&s_view, &rest, 0);
    return true;
}

// The Moods page while he is KITT (its button marked), after everything else: opening the page moves the menu on.
static bool kitt_menu_screen(office_faces_t *faces)
{
    office_faces_show(faces, false);
    s_scene.background = layout_background;
    s_scene.background_ctx = &s_layout;
    buddy_view_camera_home(&s_view);
    menu_view_show(&s_menu_view, true);
    const uint32_t t0 = 10000000, out = BUDDY_KITT_IN_MS + 10u * BUDDY_KITT_SWEEP_MS;
    buddy_pose_t pose;
    buddy_kitt_pose(out - 300, 0, &pose);
    buddy_view_apply(&s_view, &pose, out - 300);
    menu_open(&s_menu, MENU_MOODS, t0);
    menu_view_apply(&s_menu_view, &s_menu, &k_kitt_status, t0 + 2000);
    return shot("kitt_menu_moods");
}

// Tic-Tac-Toe: moments of rounds set up on the board (the game's own state, so each is exactly that moment), with
// his face, his line and the panel as the firmware draws them; full size, and halved on ttt_sheet.png (3 x 3).
enum { TTT_COLS = 3, TTT_ROWS = 3, TTT_TILE_W = W / 2, TTT_TILE_H = H / 2 };
static gfx_color_t s_ttt_sheet[TTT_COLS * TTT_TILE_W * TTT_ROWS * TTT_TILE_H];

static void ttt_tile(int i)
{
    const int sheet_w = TTT_COLS * TTT_TILE_W, ox = (i % TTT_COLS) * TTT_TILE_W, oy = (i / TTT_COLS) * TTT_TILE_H;
    for (int y = 0; y < TTT_TILE_H; y++) {
        for (int x = 0; x < TTT_TILE_W; x++) {
            s_ttt_sheet[(size_t)(oy + y) * sheet_w + ox + x] =
                x == 0 || y == 0 ? GFX_RGB(255, 255, 255) : s_px[(size_t)(2 * y) * W + 2 * x];
        }
    }
}

// A round at a moment: the board ("X.O" row by row), the phase and since when, the result and its line.
static void ttt_set(ttt_t *g, const char *board, ttt_phase_t phase, uint32_t phase_at, ttt_result_t result)
{
    g->moves = 0;
    for (int i = 0; i < TTT_CELLS; i++) {
        g->board[i] = board[i] == 'X' ? TTT_X : board[i] == 'O' ? TTT_O : TTT_EMPTY;
        g->moves += g->board[i] != TTT_EMPTY;
    }
    g->phase = (uint8_t)phase;
    g->phase_at = phase_at;
    g->round_at = phase_at - 20000; // well into the round: no opening line
    g->touched_at = phase_at;
    g->last = -1;
    g->hey = false;
    g->result = (uint8_t)result;
    ttt_judge(g->board, &g->line);
}

static bool ttt_moment(const char *name, int tile, ttt_t *g, uint8_t face, uint8_t attitude, uint32_t since,
                       uint32_t now, menu_ask_t *ask)
{
    const buddy_game_t f = {face, attitude, 1, since, 0, 0};
    buddy_pose_t pose = {.state = BUDDY_STATE_GAME};
    buddy_game_pose(&f, now - since, &pose);
    buddy_view_apply(&s_view, &pose, now);
    menu_game_t look;
    menu_ttt_look(&look, g, NULL, now);
    menu_status_t st = {.mood = BUDDY_MOOD_IDLE, .office = true, .busy = g->phase != TTT_OFF};
    st.game = g->phase != TTT_OFF ? &look : NULL;
    menu_view_apply(&s_menu_view, &s_menu, &st, now);
    menu_ask_say(ask, g->phase != TTT_OFF ? ttt_line(g, now) : NULL);
    if (!shot(name)) {
        return false;
    }
    ttt_tile(tile);
    return true;
}

static bool ttt_screens(menu_ask_t *ask)
{
    const uint32_t t0 = 20000000;
    const menu_status_t idle = {.mood = BUDDY_MOOD_IDLE, .office = true};
    menu_open(&s_menu, MENU_GAMES, t0);
    menu_view_apply(&s_menu_view, &s_menu, &idle, t0 + 1000);
    ttt_t g;
    ttt_init(&g, 3);
    if (!ttt_moment("ttt_menu_games", 0, &g, BUDDY_GAME_WATCH, BUDDY_GAME_EVEN, t0, t0 + 1000, ask)) {
        return false;
    }
    menu_open(&s_menu, MENU_TTT, t0 + 2000);
    uint32_t t = t0 + 3000;
    ttt_start(&g, t); // you start: "YOU START!"
    bool ok = ttt_moment("ttt_your_turn", 1, &g, BUDDY_GAME_WATCH, BUDDY_GAME_EVEN, t, t + 900, ask);

    t += 10000; // a few moves in: he thinks about blocking you
    ttt_set(&g, "X...X...O", TTT_THINKING, t, TTT_OPEN);
    ok = ok && ttt_moment("ttt_thinking", 2, &g, BUDDY_GAME_THINK, BUDDY_GAME_EVEN, t, t + 700, ask);

    t += 10000; // he wins: the line blinks, he hops with sparkles; then smug, asking for another one
    g.buddy = 1;
    g.streak = 1;
    g.say = 0;
    ttt_set(&g, "XXO.O.OX.", TTT_REACTION, t, TTT_O_WINS);
    ok = ok && ttt_moment("ttt_buddy_wins", 3, &g, BUDDY_GAME_WON, BUDDY_GAME_EVEN, t, t + 400, ask);
    ttt_set(&g, "XXO.O.OX.", TTT_REMATCH, t + TTT_REACTION_MS, TTT_O_WINS);
    ok = ok && ttt_moment("ttt_buddy_smug", 4, &g, BUDDY_GAME_WON, BUDDY_GAME_EVEN, t, t + 4000, ask);

    t += 10000; // you win: frustrated, then drooping; then sulking, asking for a rematch
    g.you = 1;
    g.streak = -1;
    ttt_set(&g, "XOO.X.O.X", TTT_REACTION, t, TTT_X_WINS);
    ok = ok && ttt_moment("ttt_buddy_loses", 5, &g, BUDDY_GAME_LOST, BUDDY_GAME_EVEN, t, t + 1300, ask);
    ttt_set(&g, "XOO.X.O.X", TTT_REMATCH, t + TTT_REACTION_MS, TTT_X_WINS);
    ok = ok && ttt_moment("ttt_rematch", 6, &g, BUDDY_GAME_LOST, BUDDY_GAME_EVEN, t, t + 4000, ask);

    t += 10000; // a draw: confused, then puzzled, asking to play again
    g.draws = 1;
    g.streak = 0;
    ttt_set(&g, "XOXXOOOXX", TTT_REACTION, t, TTT_DRAW);
    ok = ok && ttt_moment("ttt_draw", 7, &g, BUDDY_GAME_DREW, BUDDY_GAME_EVEN, t, t + 700, ask);
    ttt_set(&g, "XOXXOOOXX", TTT_REMATCH, t + TTT_REACTION_MS, TTT_DRAW);
    ok = ok && ttt_moment("ttt_again", 8, &g, BUDDY_GAME_DREW, BUDDY_GAME_EVEN, t, t + 4000, ask);
    if (!ok) {
        return false;
    }
    const int sheet_w = TTT_COLS * TTT_TILE_W;
    const gfx_surface_t sheet = {s_ttt_sheet, sheet_w, TTT_ROWS * TTT_TILE_H, sheet_w};
    return write("ttt_sheet", &sheet);
}

// Missile Defense: real game states (md.h, played forward or set up), Buddy's face for them and the game view, as the
// firmware draws them; full size, and halved on md_sheet.png (4 x 4).
enum { MD_COLS = 4, MD_ROWS = 4, MD_TILE_W = W / 2, MD_TILE_H = H / 2 };
static gfx_color_t s_md_sheet[MD_COLS * MD_TILE_W * MD_ROWS * MD_TILE_H];
static md_view_t s_mdv;

static void md_tile(int i)
{
    const int sheet_w = MD_COLS * MD_TILE_W, ox = (i % MD_COLS) * MD_TILE_W, oy = (i / MD_COLS) * MD_TILE_H;
    for (int y = 0; y < MD_TILE_H; y++) {
        for (int x = 0; x < MD_TILE_W; x++) {
            s_md_sheet[(size_t)(oy + y) * sheet_w + ox + x] =
                x == 0 || y == 0 ? GFX_RGB(255, 255, 255) : s_px[(size_t)(2 * y) * W + 2 * x];
        }
    }
}

static void md_run(md_t *g, uint32_t *t, uint32_t to)
{
    while ((int32_t)(to - *t) > 0) {
        *t += 16;
        md_tick(g, *t);
    }
}

static bool md_moment(const char *name, int tile, const md_t *g, uint32_t now)
{
    const buddy_game_t f = md_buddy_face(g, now);
    buddy_pose_t pose = {.state = BUDDY_STATE_GAME};
    buddy_game_pose(&f, now - f.since, &pose);
    buddy_view_apply(&s_view, &pose, now);
    int ex[2], ey[2];
    for (int i = 0; i < 2; i++) {
        const gfx_rect_t e = gfx_sprite_bounds(&s_view.eye[i]);
        ex[i] = e.x + e.w / 2;
        ey[i] = e.y + e.h / 2;
    }
    md_view_apply(&s_mdv, g, ex, ey, now);
    if (!shot(name)) {
        return false;
    }
    md_tile(tile);
    return true;
}

// A missile by hand: launched at `born` from x0 towards target, falling for fall ms.
static void md_place(md_t *g, int slot, int x0, int target, uint16_t fall, uint32_t born)
{
    g->missile[slot] = (md_missile_t){MD_MISSILE_FLYING, (uint8_t)target, false, (int16_t)x0, MD_SKY_TOP,
                                      target == MD_TARGET_BUDDY ? MD_BUDDY_X : md_item_x[target],
                                      target == MD_TARGET_BUDDY ? MD_BUDDY_Y : MD_ITEM_HIT_Y, fall, born, 0, 0, 0};
}

static bool md_screens(menu_ask_t *ask)
{
    if (!md_view_init(&s_mdv, &s_scene)) {
        return false;
    }
    uint32_t t = 30000000;
    // His alert on the stage, the GAMES page behind it.
    menu_open(&s_menu, MENU_GAMES, t);
    const menu_status_t busy = {.mood = BUDDY_MOOD_IDLE, .office = true, .busy = true};
    menu_view_apply(&s_menu_view, &s_menu, &busy, t + 1000);
    const buddy_game_t alert = {BUDDY_GAME_MD_ALERT, BUDDY_GAME_EVEN, 0, t, 0, 0};
    buddy_pose_t pose = {.state = BUDDY_STATE_GAME};
    buddy_game_pose(&alert, 500, &pose);
    buddy_view_apply(&s_view, &pose, t + 500);
    menu_ask_say(ask, "INCOMING!");
    if (!shot("md_alert")) {
        return false;
    }
    md_tile(0);
    menu_ask_say(ask, NULL);

    // The game scene, as the firmware switches to it.
    menu_view_show(&s_menu_view, false);
    buddy_view_camera(&s_view, MD_BUDDY_SCALE, MD_BUDDY_X0, MD_BUDDY_Y0);
    s_scene.background = md_view_background;
    s_scene.background_ctx = &s_mdv;
    s_scene.overlay = md_view_overlay;
    s_scene.overlay_ctx = &s_mdv;
    md_view_show(&s_mdv, true);

    md_t g;
    md_init(&g, 9);
    t += 10000;
    md_start(&g, t);
    g.best = 2480;
    g.best_before = 2480;
    bool ok = md_moment("md_ready", 1, &g, t + 700);

    // Wave 3 under way: he tracks the lowest missile; then a shot: both beams, the blast, the points.
    g.wave = 3;
    md_run(&g, &t, t + MD_READY_MS + 7000);
    g.say = MD_SAY_NONE;
    ok = ok && md_moment("md_aim", 2, &g, t);
    int lowest = -1, ly = -1;
    for (int i = 0; i < MD_MAX_MISSILES; i++) {
        int x, y;
        if (g.missile[i].state == MD_MISSILE_FLYING && (md_missile_head(&g.missile[i], t, &x, &y), y > ly)) {
            lowest = i;
            ly = y;
        }
    }
    if (lowest >= 0) {
        int x, y;
        md_missile_head(&g.missile[lowest], t + 150, &x, &y);
        md_fire(&g, x, y, t);
    }
    md_run(&g, &t, t + 64);
    ok = ok && md_moment("md_fire", 3, &g, t);
    md_run(&g, &t, t + 220);
    ok = ok && md_moment("md_blast", 4, &g, t);

    // A missile gets through to the coffee mug: the flash, then flames over the rubble; he is dismayed.
    md_place(&g, 9, 160, 0, 2500, t - 2400);
    md_run(&g, &t, t + 120);
    ok = ok && md_moment("md_item_hit", 5, &g, t);
    md_run(&g, &t, t + 500);
    ok = ok && md_moment("md_item_lost", 6, &g, t);

    // One on his head: it flies off his neck.
    md_run(&g, &t, t + 2500);
    md_place(&g, 8, 560, MD_TARGET_BUDDY, 1200, t - 1150);
    md_run(&g, &t, t + 330);
    ok = ok && md_moment("md_hit", 7, &g, t);
    md_run(&g, &t, t + 1000);
    ok = ok && md_moment("md_dazed", 8, &g, t);

    // Fired too fast: overheated eyes.
    md_run(&g, &t, t + 1500);
    for (int i = 0; i < 5; i++) {
        md_fire(&g, 150 + i * 180, 120 + (i % 2) * 60, t);
        md_run(&g, &t, t + 40);
    }
    md_run(&g, &t, t + 420);
    ok = ok && md_moment("md_hot", 9, &g, t);

    // Wave 8, many close at once: panic.
    md_run(&g, &t, t + 3000);
    g.wave = 8;
    g.next_spawn = t + 100000000u;
    g.lost_at = 0; // nothing just lost: his panic shows
    g.say = MD_SAY_NONE;
    for (int i = 0; i < MD_MAX_MISSILES; i++) {
        g.missile[i].state = MD_MISSILE_FREE;
    }
    for (int i = 0; i < 5; i++) {
        md_place(&g, i, 120 + i * 190, i + 1, 6000, t - 4200 + (uint32_t)i * 150);
    }
    md_run(&g, &t, t + 120);
    ok = ok && md_moment("md_panic", 10, &g, t);

    // The wave cleared: the bonus, hops and sparkles.
    for (int i = 0; i < MD_MAX_MISSILES; i++) {
        g.missile[i].state = MD_MISSILE_FREE;
    }
    g.spawned = md_wave(g.wave).missiles;
    md_run(&g, &t, t + 1200);
    ok = ok && md_moment("md_clear", 11, &g, t);
    if (!ok) {
        return false;
    }

    // Game over: the last item goes; RETRY and QUIT; a new best.
    md_run(&g, &t, t + MD_CLEAR_MS + MD_READY_MS + 400);
    g.items = 1u << 5;
    g.score = 3120;
    g.next_spawn = t + 100000000u;
    md_place(&g, 0, 900, 5, 2000, t - 1990);
    md_run(&g, &t, t + 50);
    md_run(&g, &t, g.phase_at + MD_OVER_BUTTONS_MS + 300);
    g.best = 3120;
    if (!md_moment("md_over", 12, &g, t)) {
        return false;
    }
    const int sheet_w = MD_COLS * MD_TILE_W;
    const gfx_surface_t sheet = {s_md_sheet, sheet_w, MD_ROWS * MD_TILE_H, sheet_w};
    return write("md_sheet", &sheet);
}

int main(int argc, char **argv)
{
    s_dir = argc > 1 ? argv[1] : ".";
    static gfx_color_t strip[16 * 1024];
    static office_faces_t faces;
    static uint8_t clock_mem[OFFICE_CLOCK_MEMORY];
    static uint8_t skeleton_mem[OFFICE_SKELETON_MEMORY];
    static uint8_t menu_mem[MENU_VIEW_MEMORY];
    static marquee_t marquee;
    static menu_ask_t ask;
    static menu_thought_t thought;
    static uint8_t thought_mem[MENU_THOUGHT_MEMORY];
    s_layout = layout_compute(W, H, STAGE_PERCENT, false, ALIGN);
    if (!gfx_scene_init(&s_scene, W, H, layout_background, &s_layout, strip, 16 * 1024, ALIGN) ||
        !buddy_view_init(&s_view, &s_scene, s_layout.stage, 10, 0) ||
        !menu_init(&s_menu, &menu_default, layout_panel_content(&s_layout), 0) ||
        !menu_view_init(&s_menu_view, &s_scene, &s_menu, menu_mem, sizeof(menu_mem), 10) ||
        !office_faces_init(&faces, &s_scene, s_layout.screen, clock_mem, skeleton_mem, NULL, NULL, 20) ||
        !marquee_init(&marquee, &s_scene, 30) ||
        !menu_ask_init(&ask, &s_scene, s_layout.stage, s_view.home_y0 - BUDDY_MAX_LIFT * s_view.home_scale, 25) ||
        !menu_thought_init(&thought, &s_scene, s_layout.stage, s_view.home_x0 + s_view.art->w * s_view.home_scale / 2,
                           thought_mem, sizeof(thought_mem), 5)) {
        fprintf(stderr, "scene setup failed\n");
        return 1;
    }
    gfx_dirty_init(&s_all, W, H, ALIGN);
    gfx_dirty_add(&s_all, (gfx_rect_t){0, 0, W, H});
    const menu_status_t idle = {.mood = BUDDY_MOOD_IDLE, .office = true};
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
    const menu_status_t charging = {.mood = BUDDY_MOOD_IDLE, .monster = true, .busy = true, .doing = "charging!",
                                    .office = true};
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

    // The activities: a sheet (one row each, in even steps through it) and full-size key moments.
    static const struct {
        buddy_state_t state;
        const char *name;
        uint32_t keys[3];
    } k_acts[ACT_ROWS] = {
        {BUDDY_STATE_WATER, "water", {1500, 3300, 4200}},
        {BUDDY_STATE_SNEEZE, "sneeze", {900, 1300, 1800}},
        {BUDDY_STATE_STARE, "stare", {2000, 3500, 4600}},
        {BUDDY_STATE_MEDITATE, "meditate", {3000, 6000, 6450}},
    };
    const int act_w = ACT_COLS * TILE_W;
    for (int a = 0; a < ACT_ROWS; a++) {
        const uint32_t len = buddy_activity_ms(k_acts[a].state);
        for (int col = 0; col < ACT_COLS; col++) {
            const uint32_t t = len * (uint32_t)col / ACT_COLS;
            buddy_pose_t pose = {.state = k_acts[a].state};
            buddy_activity_pose(k_acts[a].state, t, &pose);
            buddy_view_apply(&s_view, &pose, t);
            gfx_scene_render(&s_scene, &s_all, &dst);
            tile(s_act_sheet, act_w, col, a);
        }
        for (int k = 0; k < 3; k++) {
            buddy_pose_t pose = {.state = k_acts[a].state};
            buddy_activity_pose(k_acts[a].state, k_acts[a].keys[k], &pose);
            buddy_view_apply(&s_view, &pose, k_acts[a].keys[k]);
            char name[64];
            snprintf(name, sizeof(name), "act_%s_%04u", k_acts[a].name, (unsigned)k_acts[a].keys[k]);
            if (!shot(name)) {
                return 1;
            }
        }
        const buddy_pose_t rest = {.eyes = BUDDY_EYES_OPEN};
        buddy_view_apply(&s_view, &rest, 0); // back to his place
    }
    const gfx_surface_t act_sheet = {s_act_sheet, act_w, ACT_ROWS * TILE_H, act_w};
    if (!write("activity_sheet", &act_sheet)) {
        return 1;
    }

    // "PEUKIE ROKEN?": hopeful (glancing at NEE, JA being pressed), impatient with the bar red, and sad after no.
    // "ZE DOEN MAAR WAT?": the confirmation, and his reaction once PDC Manager confirmed the click (with the line on
    // the sign).
    static const struct {
        buddy_state_t state;
        uint32_t t;
        int pressed;
        const char *name;
    } k_ask[] = {
        {BUDDY_STATE_ASK, 200, -1, "act_ask_0200"},       {BUDDY_STATE_ASK, 900, -1, "act_ask_0900"},
        {BUDDY_STATE_ASK, 20500, MENU_ANSWER_YES, "act_ask_ja"}, {BUDDY_STATE_ASK, 54000, -1, "act_ask_54000"},
        {BUDDY_STATE_SAD, 1500, -1, "act_sad_1500"},      {BUDDY_STATE_CONFIRM, 900, -1, "act_zdmw_confirm"},
        {BUDDY_STATE_CONFIRM, 12500, MENU_ANSWER_YES, "act_zdmw_ja"}, {BUDDY_STATE_ZDMW, 120, -1, "act_zdmw_0120"},
        {BUDDY_STATE_ZDMW, 900, -1, "act_zdmw_0900"},     {BUDDY_STATE_ZDMW, 1450, -1, "act_zdmw_1450"},
        {BUDDY_STATE_ZDMW, 2300, -1, "act_zdmw_2300"},
    };
    const menu_status_t asking = {.mood = BUDDY_MOOD_IDLE, .busy = true, .doing = "asking", .office = true};
    const menu_status_t fuming = {.mood = BUDDY_MOOD_IDLE, .busy = true, .doing = "fuming", .office = true};
    const int sign_x = s_layout.stage.x + (s_layout.stage.w - MARQUEE_W) / 2, sign_y = 22;
    for (unsigned i = 0; i < sizeof(k_ask) / sizeof(k_ask[0]); i++) {
        const buddy_state_t state = k_ask[i].state;
        buddy_pose_t pose = {.state = state};
        buddy_activity_pose(state, k_ask[i].t, &pose);
        buddy_view_apply(&s_view, &pose, k_ask[i].t);
        menu_view_apply(&s_menu_view, &s_menu, state == BUDDY_STATE_ZDMW ? &fuming : &asking, 20000 + k_ask[i].t);
        ask.pressed = (int8_t)k_ask[i].pressed;
        const uint32_t total = buddy_activity_ms(state);
        const char *q = state == BUDDY_STATE_ASK       ? MENU_QUESTION_PEUKIE
                        : state == BUDDY_STATE_CONFIRM ? MENU_QUESTION_ZDMW
                                                       : NULL;
        menu_ask_apply(&ask, q, q ? total - k_ask[i].t : 0, total);
        if (state == BUDDY_STATE_ZDMW) { // the line goes up with his reaction
            marquee_start(&marquee, "ZE DOEN MAAR WAT!!! #123", 0);
            marquee_apply(&marquee, sign_x, sign_y, MARQUEE_INTRO_MS + (k_ask[i].t / 40) * MARQUEE_COL_MS);
        }
        if (!shot(k_ask[i].name)) {
            return 1;
        }
    }
    marquee_apply(&marquee, 0, 0, marquee_duration_ms(&marquee)); // over: hidden
    ask.pressed = MENU_ANSWER_NONE;
    menu_ask_apply(&ask, NULL, 0, BUDDY_ASK_MS);
    menu_view_apply(&s_menu_view, &s_menu, &idle, 40000);

    // FOCUS, panel by panel: the plan growing, the interruption, POOF, NEVERMIND, and the void.
    static const uint32_t k_focus[] = {1000, 2200, 3000, 4100, 4700, 5000, 5900, 7200, 8000};
    const menu_status_t focusing = {.mood = BUDDY_MOOD_IDLE, .busy = true, .doing = "focusing", .office = true};
    for (unsigned i = 0; i < sizeof(k_focus) / sizeof(k_focus[0]); i++) {
        buddy_pose_t pose = {.state = BUDDY_STATE_FOCUS};
        buddy_activity_pose(BUDDY_STATE_FOCUS, k_focus[i], &pose);
        buddy_view_apply(&s_view, &pose, k_focus[i]);
        menu_thought_apply(&thought, &pose.thought);
        menu_view_apply(&s_menu_view, &s_menu, &focusing, 50000 + k_focus[i]);
        char name[32];
        snprintf(name, sizeof(name), "act_focus_%04u", (unsigned)k_focus[i]);
        if (!shot(name)) {
            return 1;
        }
    }
    menu_thought_apply(&thought, NULL);
    menu_view_apply(&s_menu_view, &s_menu, &idle, 60000);
    const buddy_pose_t rest = {.eyes = BUDDY_EYES_OPEN};
    buddy_view_apply(&s_view, &rest, 0);

    if (!kitt_screens(&idle)) {
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

    // Clock Mode, as the firmware switches to it.
    buddy_view_hide(&s_view);
    menu_view_show(&s_menu_view, false);
    office_faces_show(&faces, true);
    s_scene.background = office_faces_background(&faces, &s_scene.background_ctx);
    const struct tm tm = {.tm_year = 126, .tm_mon = 9, .tm_mday = 14, .tm_wday = 3, .tm_hour = 9, .tm_min = 41};
    office_faces_apply(&faces, &tm, 100);
    marquee_apply(&marquee, (W - MARQUEE_W) / 2, H - MARQUEE_H - 36, MARQUEE_INTRO_MS + 120 * MARQUEE_COL_MS);
    if (!shot("office_marquee")) {
        return 1;
    }
    marquee_apply(&marquee, 0, 0, marquee_duration_ms(&marquee)); // over: hidden
    if (!shot("office_clock")) {
        return 1;
    }
    office_faces_apply(&faces, NULL, 0);
    if (!shot("office_not_set")) {
        return 1;
    }
    office_faces_apply(&faces, &tm, 100);
    return zoom_screens(&faces) && skeleton_screens(&faces, &marquee) && kitt_menu_screen(&faces) &&
                   ttt_screens(&ask) && md_screens(&ask)
               ? 0
               : 1;
}
