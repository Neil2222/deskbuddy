// Buddy Missile Defense on screen (md_view.h) and in Buddy's face (md_buddy.h): touches, his faces and where he looks,
// his bubble, and whole games in the render loop's way (every frame equal to a full redraw), with the workload measured.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buddy_brain.h"
#include "buddy_game.h"
#include "buddy_view.h"
#include "gfx_repair.h"
#include "gfx_scene.h"
#include "layout.h"
#include "marquee.h"
#include "md.h"
#include "md_buddy.h"
#include "md_view.h"
#include "menu.h"
#include "menu_ask.h"
#include "menu_view.h"
#include "test.h"

enum { W = 1024, H = 600, ALIGN = 32 };

static gfx_color_t s_strip[16 * 1024];

// Everything his lines say fits his bubble.
void test_md_view_lines_fit_his_bubble(void)
{
    for (int i = 0; i < md_line_count; i++) {
        char line[40];
        snprintf(line, sizeof(line), "%s", md_lines[i]);
        for (char *part = strtok(line, "\n"); part; part = strtok(NULL, "\n")) {
            uint8_t cols[192];
            CHECK(marquee_text_columns(part, cols, (int)sizeof(cols)) <= MD_VIEW_BUBBLE_COLS);
        }
    }
}

// The face for the moment: the biggest thing going on wins, each face starting when its reason did; his eyes follow
// the lowest missile, or the shot just fired.
void test_md_buddy_faces_follow_the_game(void)
{
    md_t g;
    md_init(&g, 4);
    md_start(&g, 1000);
    CHECK_EQ(md_buddy_face(&g, 1100).face, BUDDY_GAME_MD_READY);
    md_tick(&g, 1000 + MD_READY_MS + 20);
    uint32_t t = 1000 + MD_READY_MS + 20;
    buddy_game_t f = md_buddy_face(&g, t);
    CHECK_EQ(f.face, BUDDY_GAME_MD_AIM);
    // Eyes on the lowest missile: one far left and low, one right and high.
    g.next_spawn = t + 100000000u;
    g.missile[0] = (md_missile_t){MD_MISSILE_FLYING, 0, false, 60, MD_SKY_TOP, md_item_x[0], MD_ITEM_HIT_Y, 10000,
                                  t - 8000, 0, 0, 0};
    g.missile[1] = (md_missile_t){MD_MISSILE_FLYING, 5, false, 900, MD_SKY_TOP, md_item_x[5], MD_ITEM_HIT_Y, 10000,
                                  t - 1000, 0, 0, 0};
    f = md_buddy_face(&g, t);
    CHECK_EQ(f.aim_x, -1);
    CHECK_EQ(f.aim_y, 0); // it is low: level with him
    // A shot: his firing face, eyes on the shot.
    CHECK_EQ(md_fire(&g, 900, 100, t), MD_FIRED);
    f = md_buddy_face(&g, t + 10);
    CHECK_EQ(f.face, BUDDY_GAME_MD_FIRE);
    CHECK_EQ(f.since, g.shot_at);
    CHECK_EQ(f.aim_x, 1);
    CHECK_EQ(f.aim_y, -1);
    CHECK_EQ(md_buddy_face(&g, g.shot_at + MD_FIRE_FACE_MS).face, BUDDY_GAME_MD_AIM);
    // Overheated beats firing; a hit on him beats overheating; losing the game beats everything.
    g.hot = true;
    g.hot_at = t + 20;
    CHECK_EQ(md_buddy_face(&g, t + 30).face, BUDDY_GAME_MD_HOT);
    g.stunned = true;
    g.stun_at = t + 25;
    f = md_buddy_face(&g, t + 30);
    CHECK_EQ(f.face, BUDDY_GAME_MD_HIT);
    CHECK_EQ(f.since, t + 25);
    g.phase = MD_OVER;
    g.phase_at = t + 28;
    CHECK_EQ(md_buddy_face(&g, t + 30).face, BUDDY_GAME_MD_OVER);
    // An item lost: dismay, looking at it.
    g.phase = MD_PLAYING;
    g.hot = g.stunned = false;
    g.lost_item = 4;
    g.lost_at = t + 40;
    f = md_buddy_face(&g, t + 50);
    CHECK_EQ(f.face, BUDDY_GAME_MD_LOST);
    CHECK_EQ(f.aim_x, 1);
    CHECK_EQ(md_buddy_face(&g, t + 40 + MD_LOST_FACE_MS).face == BUDDY_GAME_MD_LOST, 0);
    // Too many too close: panic; a streak: smug.
    g.panic = true;
    g.panic_at = t + 60;
    CHECK_EQ(md_buddy_face(&g, t + 40 + MD_LOST_FACE_MS).face, BUDDY_GAME_MD_PANIC);
    g.panic = false;
    g.streak = MD_SMUG_STREAK;
    g.smug_at = t + 70;
    CHECK_EQ(md_buddy_face(&g, t + 40 + MD_LOST_FACE_MS).face, BUDDY_GAME_MD_SMUG);
    // His poses: eyes up and aside when aiming that way; red face and steam when hot; the head off his neck when hit.
    buddy_pose_t p = {0};
    buddy_game_pose(&(buddy_game_t){BUDDY_GAME_MD_AIM, 0, 0, 0, -1, -1}, 100, &p);
    CHECK(p.look == -1 && p.look_y == -1);
    memset(&p, 0, sizeof(p));
    buddy_game_pose(&(buddy_game_t){BUDDY_GAME_MD_HOT, 0, 0, 0, 0, 0}, 300, &p);
    CHECK_EQ(p.tint, BUDDY_TINT_FURY);
    CHECK_EQ(p.props.particle[0].kind, BUDDY_PART_STEAM);
    memset(&p, 0, sizeof(p));
    buddy_game_pose(&(buddy_game_t){BUDDY_GAME_MD_HIT, 0, 0, 0, 0, 0}, 300, &p);
    CHECK(p.pop >= BUDDY_MAX_POP - 1);
    memset(&p, 0, sizeof(p));
    buddy_game_pose(&(buddy_game_t){BUDDY_GAME_MD_ALERT, 0, 0, 0, 0, 0}, 100, &p);
    CHECK(p.props.alert && p.eyes == BUDDY_EYES_WIDE);
}

// Shots go off as the finger comes down anywhere on the field; the X, RETRY and QUIT act when the finger is lifted on
// them (sliding off cancels); RETRY and QUIT only once the game is over and they show.
void test_md_view_touch_shoots_at_once_buttons_on_release(void)
{
    static gfx_scene_t scene;
    static md_view_t v;
    static layout_t layout;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(md_view_init(&v, &scene));
    md_view_show(&v, true);
    md_t g;
    md_init(&g, 1);
    md_start(&g, 1000);
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_DOWN, 300, 200, 1100), MD_VIEW_HIT_SKY);
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_UP, 300, 200, 1150), MD_VIEW_HIT_NONE);
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_DOWN, 512, 580, 1200), MD_VIEW_HIT_SKY); // on the desk: a low shot
    // The X: on release, only if still on it.
    const int ex = MD_W - 34, ey = 20;
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_DOWN, ex, ey, 1300), MD_VIEW_HIT_NONE);
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_UP, ex, ey, 1350), MD_VIEW_HIT_EXIT);
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_DOWN, ex, ey, 1400), MD_VIEW_HIT_NONE);
    md_view_touch(&v, &g, MD_VIEW_TOUCH_MOVE, ex - 60, ey + 60, 1420);
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_UP, ex - 60, ey + 60, 1450), MD_VIEW_HIT_NONE);
    // RETRY and QUIT: not before the game is over and they show.
    const int rx = MD_W / 2 - 32 - 80, qx = MD_W / 2 + 32 + 80, by = 240;
    CHECK_EQ(md_view_hit(&v, &g, rx, by, 1500), MD_VIEW_HIT_SKY);
    g.phase = MD_OVER;
    g.phase_at = 2000;
    CHECK_EQ(md_view_hit(&v, &g, rx, by, 2000 + MD_OVER_BUTTONS_MS - 1), MD_VIEW_HIT_SKY);
    const uint32_t t = 2000 + MD_OVER_BUTTONS_MS;
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_DOWN, rx, by, t), MD_VIEW_HIT_NONE);
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_UP, rx, by, t + 50), MD_VIEW_HIT_RETRY);
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_DOWN, qx, by, t + 100), MD_VIEW_HIT_NONE);
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_UP, rx, by, t + 150), MD_VIEW_HIT_NONE); // released elsewhere
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_DOWN, qx, by, t + 200), MD_VIEW_HIT_NONE);
    CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_UP, qx, by, t + 250), MD_VIEW_HIT_QUIT);
    md_view_show(&v, false);
    CHECK_EQ(md_view_hit(&v, &g, 300, 200, t), MD_VIEW_HIT_NONE);
}

// ---- Whole games in the render loop's way ---------------------------------------------------------------------------

static gfx_color_t s_buf[2][W * H];
static gfx_color_t s_ref[W * H];
static uint8_t s_menu_mem[MENU_VIEW_MEMORY];

typedef struct {
    int frames, bad, saturated, pending_full, full;
    int px_max, px_sum, pieces_max;
    int fire_px_max;  // frames with a beam on screen
    int over_frames, over_pieces; // the game-over screen once its buttons are up and the last flames are out: frames
                                  // looked at, and anything of the game layer that changed in them (frozen: nothing)
    int quiet_px_max; // play frames without a beam, a new blast or an item burning
    int changes_max;  // dirty rectangles in a frame's changes
    int p50, p95, p99; // play frames' redraws (pixels)
} md_stats_t;

static int s_areas[30000];

static int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

// One game, start to finish, as app.c plays it: into the game scene from the stage (at wave start_wave), a player who
// shoots the lowest missile every gap_ms (every fifth shot 30 px off; once, in the next wave, a frantic burst that
// overheats his eyes), a missile on Buddy's head in that wave, then nobody playing until the desk is gone, the
// game-over screen, RETRY, a little more play, the X: back to the stage. Every presented frame is compared with a full
// redraw. seed: the game's.
static void play_game(uint32_t seed, int start_wave, uint32_t gap_ms, md_stats_t *st)
{
    static gfx_scene_t scene;
    static buddy_view_t view;
    static menu_t m;
    static menu_view_t mv;
    static menu_ask_t ask;
    static md_view_t v;
    static gfx_dirty_t changes, redraw, all;
    static gfx_repair_t repair;
    static layout_t layout;
    memset(st, 0, sizeof(*st));
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(buddy_view_init(&view, &scene, layout.stage, 10, 0));
    CHECK(menu_init(&m, &menu_default, layout_panel_content(&layout), 0));
    CHECK(menu_view_init(&mv, &scene, &m, s_menu_mem, sizeof(s_menu_mem), 10));
    CHECK(menu_ask_init(&ask, &scene, layout.stage, view.home_y0 - BUDDY_MAX_LIFT * view.home_scale, 25));
    CHECK(md_view_init(&v, &scene));
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
    memset(s_buf, 0, sizeof(s_buf));
    const gfx_surface_t ref = {s_ref, W, H, W};
    const menu_status_t idle = {.mood = BUDDY_MOOD_IDLE, .office = true};

    buddy_brain_t brain;
    buddy_brain_init(&brain, 0, 11);
    md_t g;
    md_init(&g, seed);
    int back = 0;
    bool in_game = false, left = false, retried = false, burst = false, hit = false;
    uint32_t last_shot = 0, end = 0, enter_at = 1000, idle_from = 0, over_seen = 0;
    int shots = 0, n_areas = 0;
    for (uint32_t t = 0, k = 0; !end || t < end; k++) {
        CHECK(t < 900000);
        if (!in_game && !left && t >= enter_at) { // the switch: the game scene, both buffers redrawn
            menu_view_show(&mv, false);
            buddy_view_camera(&view, MD_BUDDY_SCALE, MD_BUDDY_X0, MD_BUDDY_Y0);
            scene.background = md_view_background;
            scene.background_ctx = &v;
            scene.overlay = md_view_overlay;
            scene.overlay_ctx = &v;
            md_view_show(&v, true);
            md_start(&g, t);
            g.wave = (uint8_t)start_wave;
            gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
            in_game = true;
        }
        uint32_t next = 33;
        bool beam = false, busy_frame = false;
        if (in_game) {
            const uint32_t n = md_tick(&g, t);
            next = n < next ? n : next;
            // The player (see above); from wave start_wave + 2 on, nobody (the desk goes, the game ends).
            const bool playing = !idle_from || t < idle_from;
            if (!idle_from && !retried && g.wave >= start_wave + 2) {
                idle_from = t;
            }
            if (playing && g.wave == start_wave + 1 && !burst && g.phase == MD_PLAYING) {
                for (int i = 0; i < 6; i++) {
                    md_fire(&g, 200 + i * 120, 150, t);
                }
                burst = true;
                last_shot = t;
            }
            if (playing && t - last_shot >= gap_ms) {
                int best = -1, bx = 0, by = 0;
                for (int i = 0; i < MD_MAX_MISSILES; i++) {
                    if (g.missile[i].state != MD_MISSILE_FLYING) {
                        continue;
                    }
                    int x, y;
                    md_missile_head(&g.missile[i], t + 250, &x, &y);
                    if (y > best) {
                        best = y;
                        bx = x;
                        by = y;
                    }
                }
                if (best >= 0 && md_fire(&g, bx + (shots % 5 == 4 ? 30 : 0), by, t) == MD_FIRED) {
                    shots++;
                    last_shot = t;
                }
            }
            if (!hit && g.wave == start_wave + 1 && g.phase == MD_PLAYING) { // one comes down on his head
                for (int i = 0; i < MD_MAX_MISSILES; i++) {
                    if (g.missile[i].state == MD_MISSILE_FREE) {
                        g.missile[i] = (md_missile_t){MD_MISSILE_FLYING, MD_TARGET_BUDDY, false, 700, MD_SKY_TOP,
                                                      MD_BUDDY_X, MD_BUDDY_Y, 3000, t, 0, 0, 0};
                        hit = true;
                        break;
                    }
                }
            }
            if (g.phase == MD_OVER && !over_seen) {
                over_seen = t;
            }
            if (over_seen && !retried && md_buttons(&g, t) && t - over_seen > 6000) { // RETRY
                CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_DOWN, MD_W / 2 - 112, 240, t), MD_VIEW_HIT_NONE);
                CHECK_EQ(md_view_touch(&v, &g, MD_VIEW_TOUCH_UP, MD_W / 2 - 112, 240, t), MD_VIEW_HIT_RETRY);
                md_start(&g, t);
                retried = true;
                idle_from = 0;
                last_shot = t;
            }
            if (retried && g.phase == MD_PLAYING && t - g.phase_at > 8000) { // the X: back to his stage
                md_view_show(&v, false);
                md_stop(&g);
                scene.background = layout_background;
                scene.background_ctx = &layout;
                scene.overlay = NULL;
                buddy_view_camera_home(&view);
                menu_view_show(&mv, true);
                buddy_brain_game_leave(&brain, t);
                gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
                in_game = false;
                left = true;
                end = t + 1500;
            } else {
                const buddy_game_t face = md_buddy_face(&g, t);
                CHECK(buddy_brain_game(&brain, &face, t));
            }
            beam = g.shots > 0 && t - g.shot_at < MD_BEAM_MS;
            for (int i = 0; i < MD_ITEMS; i++) {
                busy_frame = busy_frame || (!((g.items >> i) & 1u) && t - g.item_lost_at[i] < 2700);
            }
            for (int i = 0; i < MD_MAX_BLASTS; i++) {
                busy_frame = busy_frame || (g.blast[i].on && t - g.blast[i].born < 60);
            }
        }
        buddy_pose_t pose;
        uint32_t n = buddy_brain_tick(&brain, t, &pose);
        next = n < next ? n : next;
        n = buddy_view_apply(&view, &pose, t);
        next = n < next ? n : next;
        if (v.shown) {
            int ex[2], ey[2];
            for (int i = 0; i < 2; i++) {
                const gfx_rect_t e = gfx_sprite_bounds(&view.eye[i]);
                ex[i] = e.x + e.w / 2;
                ey[i] = e.y + e.h / 2;
            }
            n = md_view_apply(&v, &g, ex, ey, t);
            next = n < next ? n : next;
            st->pieces_max = (int)v.pieces > st->pieces_max ? (int)v.pieces : st->pieces_max;
            st->pending_full += scene.pending.count >= GFX_DIRTY_MAX;
            if (g.phase == MD_OVER && t - g.phase_at > 3000) {
                st->over_frames++;
                st->over_pieces += (int)v.pieces;
            }
        }
        n = menu_view_apply(&mv, &m, &idle, t);
        next = n < next ? n : next;
        menu_ask_apply(&ask, NULL, 0, 0);

        gfx_dirty_clear(&changes);
        gfx_scene_collect(&scene, &changes);
        if (changes.count > 0) {
            st->changes_max = changes.count > st->changes_max ? changes.count : st->changes_max;
            st->saturated += in_game && changes.count >= GFX_DIRTY_MAX;
            gfx_repair_next(&repair, &changes, &redraw);
            const gfx_surface_t dst = {s_buf[back], W, H, W};
            gfx_scene_render(&scene, &redraw, &dst);
            gfx_scene_render(&scene, &all, &ref);
            st->bad += memcmp(s_buf[back], s_ref, sizeof(s_ref)) != 0;
            const int area = gfx_dirty_area(&redraw);
            st->frames++;
            if (area == W * H) {
                st->full++;
            } else if (in_game) {
                st->px_max = area > st->px_max ? area : st->px_max;
                st->px_sum += area;
                if (n_areas < (int)(sizeof(s_areas) / sizeof(s_areas[0]))) {
                    s_areas[n_areas++] = area;
                }
                if (beam) {
                    st->fire_px_max = area > st->fire_px_max ? area : st->fire_px_max;
                } else if (!busy_frame) {
                    st->quiet_px_max = area > st->quiet_px_max ? area : st->quiet_px_max;
                }
            }
            back ^= 1;
        }
        const uint32_t step = next < 16 ? 16 : next > 33 ? 33 : next; // the display's pace
        t += step + (k % 7 == 0 ? 3 : 0);
    }
    CHECK(left && retried && burst && hit);
    qsort(s_areas, (size_t)n_areas, sizeof(int), cmp_int);
    if (n_areas) {
        st->p50 = s_areas[n_areas / 2];
        st->p95 = s_areas[n_areas * 95 / 100];
        st->p99 = s_areas[n_areas * 99 / 100];
    }
}

static void report(const char *name, const md_stats_t *st)
{
    printf("    %s: %d frames (%d full), redraw p50 %d / p95 %d / p99 %d / max %d px; with a beam max %d px; quiet "
           "max %d px; up to %d dirty rects (%d frames at the limit of %d, %d with invalidations at it); up to %d "
           "pieces\n",
           name, st->frames, st->full, st->p50, st->p95, st->p99, st->px_max, st->fire_px_max, st->quiet_px_max,
           st->changes_max, st->saturated, GFX_DIRTY_MAX, st->pending_full, st->pieces_max);
}

void test_md_view_redraws_match_full_redraw(void)
{
    md_stats_t st;
    play_game(31, 1, 700, &st); // the first waves, an unhurried player
    report("missile defense, waves 1-3", &st);
    CHECK_EQ(st.bad, 0);
    CHECK(st.frames > 500);
    CHECK(st.full <= 6); // at boot, and the switch in and out (two frames each): nothing else redraws everything
    CHECK(st.px_max < W * H / 2);
    CHECK(st.over_frames > 10);
    CHECK_EQ(st.over_pieces, 0);
    play_game(77, 8, 350, &st); // late waves (five at once, faster), a busy player: chains, many blasts
    report("missile defense, waves 8-10", &st);
    CHECK_EQ(st.bad, 0);
    CHECK(st.full <= 6);
    CHECK(st.px_max < W * H / 2);
    CHECK_EQ(st.over_pieces, 0);
}
