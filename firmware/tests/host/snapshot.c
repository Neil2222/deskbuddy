// Renders the placeholder character in its key poses to PNG files, using the firmware's own layout, artwork and
// compositor. A first step towards a desktop simulator: lets the art be reviewed without the device.
//
//   buddy_snapshot [output_dir]
//
// Writes buddy_<pose>.png, monster_<ms>.png at the key moments of the Monster sequence, monster_sheet.png (the
// stage every 200 ms of the sequence, at half size, on one sheet), mood_sheet.png (one row per mood: the stage
// every MOOD_STEP_MS of its loop, as the brain plays it), the message marquee (marquee_intro.png, marquee.png,
// office_marquee.png) and the Office Mode clock: office_clock.png and office_not_set.png.
#include <stdio.h>
#include <string.h>

#include "buddy_brain.h"
#include "buddy_monster.h"
#include "buddy_view.h"
#include "layout.h"
#include "marquee.h"
#include "office_clock.h"
#include "png.h"

enum { W = 1024, H = 600, ALIGN = 32, STAGE_PERCENT = 65 }; // firmware defaults (Kconfig)

// Sheet tiles: this part of the stage (screen pixels), halved.
enum { CROP_X = 64, CROP_Y = 32, CROP_W = 576, CROP_H = 480, COLS = 6, SHEET_STEP_MS = 200 };
enum { TILE_W = CROP_W / 2, TILE_H = CROP_H / 2 };
enum { TILES = (BUDDY_MONSTER_MS + SHEET_STEP_MS - 1) / SHEET_STEP_MS, ROWS = (TILES + COLS - 1) / COLS };
enum { MOOD_COLS = 12, MOOD_STEP_MS = 700, MOOD_ROWS = BUDDY_MOOD_COUNT - 1 }; // every mood but idle

static gfx_color_t s_px[W * H];
static gfx_color_t s_sheet[COLS * TILE_W * ROWS * TILE_H];
static gfx_color_t s_mood_sheet[MOOD_COLS * TILE_W * MOOD_ROWS * TILE_H];

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

static bool write(const char *dir, const char *name, const gfx_surface_t *s)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    if (!png_write_rgb565(path, s)) {
        fprintf(stderr, "cannot write %s\n", path);
        return false;
    }
    printf("%s\n", path);
    return true;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    static gfx_color_t strip[16 * 1024];
    static gfx_scene_t scene;
    static buddy_view_t view;
    static buddy_button_t button;
    static office_button_t moon;
    static office_clock_t clock;
    static uint8_t clock_mem[OFFICE_CLOCK_MEMORY];
    static marquee_t marquee;
    layout_t layout = layout_compute(W, H, STAGE_PERCENT, false, ALIGN);
    const int moon_top = layout.panel.h * 78 / 100; // as the firmware places it
    const gfx_rect_t moon_area = {layout.panel.x, layout.panel.y + moon_top, layout.panel.w,
                                  layout.panel.h - moon_top};
    if (!gfx_scene_init(&scene, W, H, layout_background, &layout, strip, 16 * 1024, ALIGN) ||
        !buddy_view_init(&view, &scene, layout.stage, 10, 0) ||
        !buddy_button_init(&button, &scene, layout.panel, view.scale, 10) ||
        !office_button_init(&moon, &scene, moon_area, 5, 10) ||
        !office_clock_init(&clock, &scene, layout.screen, clock_mem, sizeof(clock_mem), 20) ||
        !marquee_init(&marquee, &scene, 30)) {
        fprintf(stderr, "scene setup failed\n");
        return 1;
    }
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
    const gfx_surface_t dst = {s_px, W, H, W};
    gfx_dirty_t all;
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    for (unsigned i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        buddy_view_apply(&view, &shots[i].pose, 0);
        gfx_scene_render(&scene, &all, &dst);
        char name[64];
        snprintf(name, sizeof(name), "buddy_%s", shots[i].name);
        if (!write(dir, name, &dst)) {
            return 1;
        }
    }

    // Key moments of the Monster sequence, full screen (the panel shows the button without its can meanwhile).
    static const uint32_t k_moments[] = {150, 700, 1000, 1500, 1750, 2000, 2300, 2560, 2700, 3300, 3900, 4400,
                                         4560, 4630, 5000, 5340, 5700, 6500, 6790};
    buddy_button_apply(&button, true, false);
    for (unsigned i = 0; i < sizeof(k_moments) / sizeof(k_moments[0]); i++) {
        buddy_pose_t pose;
        buddy_monster_pose(k_moments[i], &pose);
        buddy_view_apply(&view, &pose, k_moments[i]);
        gfx_scene_render(&scene, &all, &dst);
        char name[64];
        snprintf(name, sizeof(name), "monster_%04u", (unsigned)k_moments[i]);
        if (!write(dir, name, &dst)) {
            return 1;
        }
    }

    // Contact sheet.
    const int sheet_w = COLS * TILE_W;
    memset(s_sheet, 0, sizeof(s_sheet));
    for (int i = 0; i < TILES; i++) {
        const uint32_t t = (uint32_t)i * SHEET_STEP_MS;
        buddy_pose_t pose;
        buddy_monster_pose(t, &pose);
        buddy_view_apply(&view, &pose, t);
        gfx_scene_render(&scene, &all, &dst);
        tile(s_sheet, sheet_w, i % COLS, i / COLS);
    }
    const gfx_surface_t sheet = {s_sheet, sheet_w, ROWS * TILE_H, sheet_w};
    if (!write(dir, "monster_sheet", &sheet)) {
        return 1;
    }

    // The moods, each played by the brain from its start (ticked like the render loop, captured every step).
    buddy_button_apply(&button, true, true);
    const int mood_w = MOOD_COLS * TILE_W;
    for (int m = 1; m < BUDDY_MOOD_COUNT; m++) {
        buddy_brain_t brain;
        buddy_brain_init(&brain, 0, 7);
        buddy_brain_set_mood(&brain, (buddy_mood_t)m, 0, 0);
        buddy_pose_t pose;
        for (uint32_t t = 0, col = 0; col < MOOD_COLS; t += 10) {
            buddy_brain_tick(&brain, t, &pose);
            if (t == col * MOOD_STEP_MS) {
                buddy_view_apply(&view, &pose, t);
                gfx_scene_render(&scene, &all, &dst);
                tile(s_mood_sheet, mood_w, (int)col, m - 1);
                col++;
            }
        }
    }
    const gfx_surface_t mood_sheet = {s_mood_sheet, mood_w, MOOD_ROWS * TILE_H, mood_w};
    if (!write(dir, "mood_sheet", &mood_sheet)) {
        return 1;
    }
    // The message marquee over the stage, as the firmware places it: its scanner sweep, then mid-scroll over the
    // surprised robot (a message makes him jump).
    const int mx = layout.stage.x + (layout.stage.w - MARQUEE_W) / 2, my = 22;
    buddy_pose_t surprised = shots[2].pose;
    buddy_view_apply(&view, &surprised, 0);
    buddy_button_apply(&button, true, true);
    marquee_start(&marquee, "Koffie om 15:00? Taart: \xE2\x82\xAC" "5,- \xE2\x99\xA5 caf\xC3\xA9 \xF0\x9F\x98\x80!", 0);
    marquee_apply(&marquee, mx, my, MARQUEE_INTRO_MS / 2);
    gfx_scene_render(&scene, &all, &dst);
    if (!write(dir, "marquee_intro", &dst)) {
        return 1;
    }
    marquee_apply(&marquee, mx, my, MARQUEE_INTRO_MS + 150 * MARQUEE_COL_MS);
    gfx_scene_render(&scene, &all, &dst);
    if (!write(dir, "marquee", &dst)) {
        return 1;
    }

    // Office Mode, as the firmware switches to it.
    buddy_view_hide(&view);
    buddy_button_apply(&button, false, false);
    office_button_show(&moon, false);
    office_clock_show(&clock, true);
    scene.background = office_background;
    scene.background_ctx = NULL;
    const struct tm tm = {.tm_year = 126, .tm_mon = 9, .tm_mday = 14, .tm_wday = 3, .tm_hour = 9, .tm_min = 41};
    office_clock_apply(&clock, &tm, 100);
    marquee_apply(&marquee, (W - MARQUEE_W) / 2, H - MARQUEE_H - 36, MARQUEE_INTRO_MS + 120 * MARQUEE_COL_MS);
    gfx_scene_render(&scene, &all, &dst);
    if (!write(dir, "office_marquee", &dst)) {
        return 1;
    }
    marquee_apply(&marquee, 0, 0, marquee_duration_ms(&marquee)); // over: hidden
    gfx_scene_render(&scene, &all, &dst);
    if (!write(dir, "office_clock", &dst)) {
        return 1;
    }
    office_clock_apply(&clock, NULL, 0);
    gfx_scene_render(&scene, &all, &dst);
    return write(dir, "office_not_set", &dst) ? 0 : 1;
}
