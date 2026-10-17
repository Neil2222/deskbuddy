#include <stdlib.h>
#include <string.h>

#include "buddy_art.h"
#include "buddy_brain.h"
#include "buddy_view.h"
#include "gfx_repair.h"
#include "layout.h"
#include "marquee.h"
#include "menu.h"
#include "menu_view.h"
#include "office_faces.h"
#include "office_switch.h"
#include "office_zoom.h"
#include "test.h"

enum { W = 1024, H = 600, ALIGN = 32 };
static const gfx_rect_t k_screen = {0, 0, W, H};

static office_camera_t home_of(const buddy_view_t *v)
{
    return (office_camera_t){v->home_scale, v->home_x0, v->home_y0};
}

void test_zoom_face_fills_the_screen_in_the_clock_colour(void)
{
    const office_camera_t face = office_zoom_face_camera(k_screen);
    CHECK_EQ(face.scale, 75); // the 16 x 8 face screen: 600 / 8
    // The face screen (art columns 6..21, rows 10..17) covers every screen pixel.
    CHECK(face.x0 + 6 * face.scale <= 0 && face.x0 + 22 * face.scale >= W);
    CHECK(face.y0 + 10 * face.scale <= 0 && face.y0 + 18 * face.scale >= H);
    // Switched off, it is exactly the clock's background: the clock replaces his face without a seam.
    gfx_color_t bg;
    office_background(NULL, &bg, 0, 0, 1);
    CHECK_EQ(buddy_art_get()->tint[BUDDY_TINT_DARK].colors[BUDDY_PX_SCREEN], bg);
}

// Runs a zoom's camera over its timeline every ms.
void test_zoom_camera_grows_from_his_head_and_back(void)
{
    static gfx_scene_t scene;
    static gfx_color_t strip[W];
    static buddy_view_t view;
    static layout_t layout;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, strip, W, ALIGN));
    CHECK(buddy_view_init(&view, &scene, layout.stage, 10, 0));
    const office_camera_t home = home_of(&view), face = office_zoom_face_camera(k_screen);
    const float hx = home.x0 + 14.0f * home.scale; // his face's centre on the stage

    office_zoom_t z;
    office_zoom_init(&z);
    office_switch_t sw;
    office_switch_init(&sw);
    office_zoom_request(&z, true);
    office_zoom_step(&z, &sw, 0);
    office_camera_t cam, prev = home;
    buddy_pose_t pose;
    int scales = 0;
    for (uint32_t t = 0; t < OFFICE_ENTER_MS; t++) {
        office_zoom_frame(&z, t, home, k_screen, &cam, &pose);
        if (t < OFFICE_ZOOM_IN_AT) {
            CHECK(cam.scale == home.scale && cam.x0 == home.x0 && cam.y0 == home.y0); // he notices first
        }
        CHECK(cam.scale >= prev.scale); // only ever closer
        scales += cam.scale != prev.scale;
        // The face's centre glides from where it is on the stage to the middle: the zoom starts at his head.
        const float cx = cam.x0 + 14.0f * cam.scale;
        CHECK(cx >= hx - cam.scale && cx <= W / 2 + cam.scale);
        prev = cam;
    }
    CHECK(cam.scale == face.scale && cam.x0 == face.x0 && cam.y0 == face.y0);
    CHECK(scales >= face.scale - home.scale - 1); // nearly every integer scale on the way: no big jumps
    CHECK(pose.eyes == BUDDY_EYES_CLOSED && pose.tint == BUDDY_TINT_DARK); // asleep, screen off

    // Leaving: from the face (eyes closed, dark) back home.
    z.state = OFFICE_ZOOM_LEAVING;
    z.running = true;
    z.start = 10000;
    prev = face;
    bool opened = false, blinked_after_open = false, stretched = false;
    for (uint32_t t = 0; t < OFFICE_LEAVE_MS; t++) {
        office_zoom_frame(&z, z.start + t, home, k_screen, &cam, &pose);
        if (t == 0) {
            CHECK(cam.scale == face.scale && pose.tint == BUDDY_TINT_DARK && pose.eyes == BUDDY_EYES_CLOSED);
        }
        CHECK(cam.scale <= prev.scale);
        opened |= pose.eyes == BUDDY_EYES_OPEN;
        blinked_after_open |= opened && pose.eyes == BUDDY_EYES_CLOSED && cam.scale == face.scale;
        stretched |= cam.scale == home.scale && pose.lift >= 2;
        prev = cam;
    }
    CHECK(cam.scale == home.scale && cam.x0 == home.x0 && cam.y0 == home.y0);
    CHECK(blinked_after_open); // waking up: the eyes open, then blink
    CHECK(stretched);          // and a stretch once he is back
    CHECK(pose.eyes == BUDDY_EYES_OPEN && pose.tint == BUDDY_TINT_NORMAL);
}

// Steps the zoom and the scene switch as the render loop does, presenting a frame each time.
typedef struct {
    office_zoom_t z;
    office_switch_t sw;
    int menu_hides, menu_shows, backs, swaps;
    bool animated;
} rig_t;

static void rig_frame(rig_t *r, uint32_t t)
{
    const office_zoom_step_t zs = office_zoom_step(&r->z, &r->sw, t);
    r->menu_hides += zs.menu_hide;
    r->menu_shows += zs.menu_show;
    r->backs += zs.back;
    r->animated = zs.animate;
    r->swaps += office_switch_step(&r->sw).swap;
    office_switch_presented(&r->sw);
}

static void rig_run(rig_t *r, uint32_t from, uint32_t to)
{
    for (uint32_t t = from; t < to; t += 10) {
        rig_frame(r, t);
    }
}

void test_zoom_enters_and_leaves_through_the_scene_switch(void)
{
    rig_t r = {0};
    office_zoom_init(&r.z);
    office_switch_init(&r.sw);
    rig_run(&r, 0, 500);
    CHECK_EQ(r.menu_hides, 0); // nothing asked, nothing happens
    CHECK(!office_zoom_busy(&r.z));

    office_zoom_request(&r.z, true);
    rig_run(&r, 500, 500 + OFFICE_ENTER_MS - 10);
    CHECK_EQ(r.menu_hides, 1);
    CHECK(office_zoom_busy(&r.z) && r.animated);
    CHECK(!r.sw.office && !r.sw.want_office); // the clock only once he is ready for it
    rig_run(&r, 500 + OFFICE_ENTER_MS - 10, 500 + OFFICE_ENTER_MS + 100);
    CHECK_EQ(r.z.state, OFFICE_ZOOM_CLOCK);
    CHECK(r.sw.office && !office_zoom_busy(&r.z) && !r.animated);
    CHECK_EQ(r.swaps, 1);

    office_zoom_request(&r.z, false); // a tap on the clock
    rig_frame(&r, 3000);
    CHECK(office_zoom_busy(&r.z) && r.animated); // drawn as his dark face from the first frame of Buddy's scene
    rig_run(&r, 3010, 3000 + 40 + OFFICE_MENU_BACK_AT);
    CHECK_EQ(r.menu_shows, 1);
    CHECK_EQ(r.backs, 0);
    rig_run(&r, 3000 + 40 + OFFICE_MENU_BACK_AT, 3000 + 60 + OFFICE_LEAVE_MS);
    CHECK_EQ(r.backs, 1);
    CHECK_EQ(r.menu_shows, 1); // once
    CHECK_EQ(r.z.state, OFFICE_ZOOM_NORMAL);
    CHECK(!r.sw.office && r.swaps == 2 && !office_zoom_busy(&r.z));
}

void test_zoom_survives_rapid_and_contrary_requests(void)
{
    rig_t r = {0};
    office_zoom_init(&r.z);
    office_switch_init(&r.sw);
    // Many requests at once are one.
    for (int i = 0; i < 10; i++) {
        office_zoom_request(&r.z, true);
    }
    rig_run(&r, 0, 300);
    CHECK_EQ(r.menu_hides, 1);
    // Changed minds while he zooms in: he finishes, shows the clock for its two frames, and comes straight back.
    office_zoom_request(&r.z, false);
    office_zoom_request(&r.z, true);
    office_zoom_request(&r.z, false);
    rig_run(&r, 300, 300 + OFFICE_ENTER_MS + OFFICE_LEAVE_MS + 500);
    CHECK_EQ(r.swaps, 2);
    CHECK_EQ(r.backs, 1);
    CHECK_EQ(r.z.state, OFFICE_ZOOM_NORMAL);
    CHECK(!r.sw.office);
    // Asked again while waking up: once he is back, he goes in again.
    office_zoom_request(&r.z, true);
    rig_run(&r, 5000, 5300);
    CHECK_EQ(r.menu_hides, 2);
    office_zoom_request(&r.z, false);
    rig_run(&r, 5300, 5300 + OFFICE_ENTER_MS + OFFICE_LEAVE_MS + 500);
    CHECK_EQ(r.backs, 2);
    CHECK_EQ(r.swaps, 4);
    CHECK(!r.sw.office && !office_zoom_busy(&r.z));
    // Asked for what is already there: nothing happens.
    office_zoom_request(&r.z, false);
    rig_run(&r, 9000, 9500);
    CHECK_EQ(r.swaps, 4);
}

// --- The whole loop -------------------------------------------------------------------------------------------------

static gfx_color_t s_strip[16 * 1024];
static gfx_color_t s_buf[2][W * H];
static gfx_color_t s_ref[W * H];
static gfx_color_t s_last_face[W * H];
static uint8_t s_clock_mem[OFFICE_CLOCK_MEMORY];
static uint8_t s_skeleton_mem[OFFICE_SKELETON_MEMORY];
static uint8_t s_menu_mem[MENU_VIEW_MEMORY];

// The render loop as the firmware runs it (scene, Buddy and his brain, the menu, the clock, the sign, the zoom and the
// scene switch, double-buffer repair): Clock from a tap on the header's Buddy head over the Activities page, with
// Buddy relaxed, a mood change and a message while the clock shows, a burst of taps to come back. Every presented
// buffer must equal a full redraw, and the zoom (Buddy's frames, the timeline, his mood afterwards) is the same
// whichever face is selected.
typedef struct {
    int frames, max_area, seam_px, clock_frames, full_in_clock;
    uint32_t asked_at, back_at; // when the switch was asked for the clock, when he was back on his stage
} loop_result_t;

static void whole_loop(office_face_t face, loop_result_t *out)
{
    static gfx_scene_t scene;
    static buddy_view_t view;
    static menu_t menu;
    static menu_view_t mview;
    static office_faces_t faces;
    static marquee_t sign;
    static layout_t layout;
    static gfx_dirty_t changes, redraw, all;
    static gfx_repair_t repair;
    memset(out, 0, sizeof(*out));
    out->seam_px = -1;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(buddy_view_init(&view, &scene, layout.stage, 10, 0));
    CHECK(menu_init(&menu, &menu_default, layout_panel_content(&layout), 0));
    CHECK(menu_view_init(&mview, &scene, &menu, s_menu_mem, sizeof(s_menu_mem), 10));
    CHECK(office_faces_init(&faces, &scene, layout.screen, s_clock_mem, s_skeleton_mem, NULL, NULL, 20));
    CHECK(office_faces_select(&faces, face));
    CHECK(marquee_init(&sign, &scene, 30));
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    const gfx_surface_t ref = {s_ref, W, H, W};
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});

    buddy_brain_t brain;
    buddy_brain_init(&brain, 0, 5);
    buddy_brain_set_mood(&brain, BUDDY_MOOD_RELAXED, 0, 0);
    menu_open(&menu, MENU_ACTIVITIES, 0);
    office_zoom_t z;
    office_zoom_init(&z);
    office_switch_t sw;
    office_switch_init(&sw);
    const struct tm tm = {.tm_year = 126, .tm_mon = 9, .tm_mday = 15, .tm_wday = 4, .tm_hour = 2, .tm_min = 7};

    int back = 0, bad = 0;
    bool tapped = false, mood_changed = false, message = false, taps_done = false, last_was_face = false;
    for (uint32_t t = 0; t < 9000; t += 5) {
        const menu_status_t st = {.mood = buddy_brain_mood(&brain, t), .monster = brain.state == BUDDY_STATE_MONSTER,
                                  .busy = buddy_brain_busy(&brain, t), .office = true};
        const bool settled = !office_zoom_busy(&z) && !office_switch_busy(&sw) && sw.want_office == sw.office &&
                             z.want_office == sw.office;
        if (!tapped && t >= 1000) { // Clock: a tap on the header's Buddy head
            const gfx_rect_t head = menu_head_rect();
            const int x = menu.area.x + (head.x + head.w / 2) * MENU_SCALE;
            const int y = menu.area.y + (head.y + head.h / 2) * MENU_SCALE;
            CHECK_EQ(menu_hit(&menu, &st, x, y, t), MENU_HIT_HEAD);
            menu_touch(&menu, MENU_TOUCH_DOWN, x, y, &st, t);
            const menu_result_t r = menu_touch(&menu, MENU_TOUCH_UP, x, y, &st, t);
            CHECK_EQ(r.action, MENU_DO_OFFICE);
            office_zoom_request(&z, true);
            tapped = true;
        }
        if (!mood_changed && sw.office && t >= 3500) { // the companion makes him angry meanwhile
            buddy_brain_set_mood(&brain, BUDDY_MOOD_ANGRY, 0, t);
            mood_changed = true;
        }
        if (!message && sw.office && t >= 3600) { // and sends a message: under the clock, as before
            marquee_start(&sign, "Goodnight", t);
            message = true;
        }
        if (!taps_done && t >= 5000 && t < 5600 && t % 40 == 0) { // a burst of taps on the clock
            if (settled && sw.office) {
                office_zoom_request(&z, false);
            }
            taps_done = t >= 5560;
        }

        const office_zoom_step_t zs = office_zoom_step(&z, &sw, t);
        if (zs.menu_hide) {
            menu_cancel(&menu);
            menu_view_close(&mview, t);
        }
        if (zs.menu_show) {
            menu_view_show(&mview, true);
            menu_reveal(&menu, t);
        }
        if (zs.back) {
            buddy_view_camera_home(&view);
            buddy_brain_rest(&brain, t);
        }
        const office_step_t step = office_switch_step(&sw);
        if (step.swap) {
            if (sw.office) {
                buddy_view_hide(&view);
                menu_cancel(&menu);
                menu_view_show(&mview, false);
                office_faces_show(&faces, true);
                scene.background = office_faces_background(&faces, &scene.background_ctx);
            } else {
                office_faces_show(&faces, false);
                scene.background = layout_background;
                scene.background_ctx = &layout;
            }
        }
        if (step.redraw_all) {
            gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
        } else if (step.redraw_kick) {
            gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, 1, 1});
        }
        if (z.asked && !out->asked_at) {
            out->asked_at = t;
        }
        if (zs.back) {
            out->back_at = t;
        }
        if (sw.office) {
            office_faces_apply(&faces, &tm, t % 1000);
        } else {
            buddy_pose_t pose;
            buddy_brain_tick(&brain, t, &pose);
            if (zs.animate) {
                office_camera_t cam;
                office_zoom_frame(&z, t, home_of(&view), layout.screen, &cam, &pose);
                buddy_view_camera(&view, cam.scale, cam.x0, cam.y0);
            }
            buddy_view_apply(&view, &pose, t);
            menu_view_apply(&mview, &menu, &st, t);
        }
        const gfx_rect_t area = sw.office ? layout.screen : layout.stage;
        marquee_apply(&sign, area.x + (area.w - MARQUEE_W) / 2,
                      sw.office ? area.y + area.h - MARQUEE_H - 36 : area.y + 22, t);

        gfx_dirty_clear(&changes);
        gfx_scene_collect(&scene, &changes);
        if (changes.count == 0) {
            continue;
        }
        gfx_repair_next(&repair, &changes, &redraw);
        const gfx_surface_t dst = {s_buf[back], W, H, W};
        gfx_scene_render(&scene, &redraw, &dst);
        gfx_scene_render(&scene, &all, &ref);
        bad += memcmp(s_buf[back], s_ref, sizeof(s_ref)) != 0;
        for (int i = 0; i < redraw.count; i++) {
            CHECK(gfx_rect_contains_rect(k_screen, redraw.rect[i]));
        }
        const int a = gfx_dirty_area(&redraw);
        out->max_area = a > out->max_area ? a : out->max_area;
        // The seam: how much changes between his last dark face and the first clock frame.
        if (sw.office && last_was_face && out->seam_px < 0) {
            out->seam_px = 0;
            for (int i = 0; i < W * H; i++) {
                out->seam_px += s_ref[i] != s_last_face[i];
            }
        }
        last_was_face = !sw.office && zs.animate && z.state == OFFICE_ZOOM_ENTERING;
        if (last_was_face) {
            memcpy(s_last_face, s_ref, sizeof(s_ref));
        }
        out->clock_frames += sw.office;
        out->full_in_clock += sw.office && !office_switch_busy(&sw) && a == W * H;
        office_switch_presented(&sw);
        out->frames++;
        back ^= 1;
    }
    CHECK_EQ(bad, 0);
    CHECK(out->clock_frames > 0);
    CHECK_EQ(out->full_in_clock, 0); // in the clock only the switch redraws everything
    // Back: Buddy on his stage, the Activities page, his mood as changed meanwhile.
    CHECK(!sw.office && z.state == OFFICE_ZOOM_NORMAL);
    CHECK(view.scale == view.home_scale && view.x0 == view.home_x0 && view.y0 == view.home_y0);
    CHECK_EQ(menu.category, MENU_ACTIVITIES);
    CHECK(mview.visible && !gfx_rect_is_empty(gfx_sprite_bounds(&mview.slot[0])));
    CHECK_EQ(buddy_brain_mood(&brain, 9000), BUDDY_MOOD_ANGRY);
    CHECK(!faces.visible);
}

void test_zoom_whole_loop_into_the_clock_and_back(void)
{
    loop_result_t r;
    whole_loop(OFFICE_FACE_CLASSIC, &r);
    CHECK(r.frames > 0);
    CHECK(r.seam_px >= 0 && r.seam_px < W * H / 20); // only the digits (and the sign) appear: no visible cut
    printf("    zoom loop: %d frames, largest redraw %d px, clock replaces the dark face with %d px changed\n", r.frames,
           r.max_area, r.seam_px);
}

// The same way in and out with the Mechanical Skeleton selected: the zoom's timeline and frames are untouched (the
// switch is asked for and Buddy is back at the same moments), only the face that appears differs.
void test_zoom_whole_loop_into_the_skeleton_and_back(void)
{
    loop_result_t classic, skeleton;
    whole_loop(OFFICE_FACE_CLASSIC, &classic);
    whole_loop(OFFICE_FACE_SKELETON, &skeleton);
    CHECK(classic.frames > 0 && skeleton.frames > 0);
    CHECK_EQ(skeleton.asked_at, classic.asked_at);
    CHECK_EQ(skeleton.back_at, classic.back_at);
    CHECK(skeleton.seam_px > classic.seam_px); // the movement and the timeplate appear with the digits
    printf("    skeleton zoom loop: %d frames (classic %d), largest redraw %d px\n", skeleton.frames, classic.frames,
           skeleton.max_area);
}
