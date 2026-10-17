// Tic-Tac-Toe with Buddy on screen: his faces across the board (buddy_game.h), the brain playing them over his mood,
// the board's touch handling (menu_ttt.h), his lines, and whole games in the render loop's way.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buddy_activity.h"
#include "buddy_brain.h"
#include "buddy_game.h"
#include "buddy_view.h"
#include "gfx_repair.h"
#include "gfx_scene.h"
#include "layout.h"
#include "menu.h"
#include "menu_ask.h"
#include "menu_ttt.h"
#include "menu_view.h"
#include "test.h"
#include "ttt.h"

enum { W = 1024, H = 600, ALIGN = 32 };

static buddy_pose_t face_at(uint8_t face, uint8_t attitude, uint32_t t)
{
    const buddy_game_t g = {face, attitude, 1, 0};
    buddy_pose_t p = {0};
    buddy_game_pose(&g, t, &p);
    return p;
}

// After the round he keeps the face the result gave him until the next round starts: smug after a win, sulking
// after a loss, puzzled after a draw; never his plain self. Every face says when it changes next, and the change is
// really there (no busy waking).
void test_game_faces_hold_until_the_next_round(void)
{
    // Won: hops and a grin first, then smug and self-satisfied for as long as it takes.
    buddy_pose_t p = face_at(BUDDY_GAME_WON, BUDDY_GAME_EVEN, 200);
    CHECK_EQ(p.eyes, BUDDY_EYES_HAPPY);
    CHECK_EQ(p.mouth, BUDDY_MOUTH_GRIN);
    CHECK(p.lift > 0);
    CHECK_EQ(p.props.particle[0].kind, BUDDY_PART_SPARKLE);
    for (uint32_t t = BUDDY_GAME_REACT_MS; t < 120000; t += 997) {
        p = face_at(BUDDY_GAME_WON, BUDDY_GAME_EVEN, t);
        CHECK(p.mouth == BUDDY_MOUTH_SMIRK);
        CHECK(p.eyes == BUDDY_EYES_LIDDED || p.eyes == BUDDY_EYES_CLOSED); // a blink now and then
        CHECK_EQ(p.props.particle[0].kind, BUDDY_PART_NONE);
    }
    // Lost: frustrated, then drooping with a tear, then sulking (looking away, glaring back now and then).
    p = face_at(BUDDY_GAME_LOST, BUDDY_GAME_EVEN, 100);
    CHECK_EQ(p.eyes, BUDDY_EYES_SQUEEZE);
    CHECK(p.shake != 0);
    p = face_at(BUDDY_GAME_LOST, BUDDY_GAME_EVEN, 1000);
    CHECK_EQ(p.nod, 1);
    CHECK_EQ(p.tint, BUDDY_TINT_DROWSY);
    CHECK_EQ(p.mouth, BUDDY_MOUTH_FROWN);
    CHECK_EQ(p.props.particle[0].kind, BUDDY_PART_SWEAT);
    int away = 0, glare = 0;
    for (uint32_t t = BUDDY_GAME_REACT_MS; t < 120000; t += 97) {
        p = face_at(BUDDY_GAME_LOST, BUDDY_GAME_EVEN, t);
        CHECK_EQ(p.mouth, BUDDY_MOUTH_FROWN);
        CHECK_EQ(p.antenna, BUDDY_ANTENNA_DIM);
        away += p.look == -1;
        glare += p.eyes == BUDDY_EYES_ANGRY && p.look == 1;
    }
    CHECK(away > glare && glare > 0);
    // Drew: confused (eyes darting, head wobbling, a "?"), then puzzled with the "?" staying up.
    int darts = 0;
    for (uint32_t t = 0; t < 1500; t += 50) {
        p = face_at(BUDDY_GAME_DREW, BUDDY_GAME_EVEN, t);
        darts += p.look != 0 && p.wobble != 0;
    }
    CHECK(darts > 10);
    CHECK(!face_at(BUDDY_GAME_DREW, BUDDY_GAME_EVEN, 100).props.query);
    for (uint32_t t = 400; t < 120000; t += 997) {
        CHECK(face_at(BUDDY_GAME_DREW, BUDDY_GAME_EVEN, t).props.query);
    }
    // Watching you: his attitude shows; thinking: narrowed eyes on the board.
    CHECK_EQ(face_at(BUDDY_GAME_WATCH, BUDDY_GAME_SMUG, 100).mouth, BUDDY_MOUTH_SMIRK);
    CHECK_EQ(face_at(BUDDY_GAME_WATCH, BUDDY_GAME_KEEN, 100).eyes, BUDDY_EYES_FOCUS);
    CHECK_EQ(face_at(BUDDY_GAME_WATCH, BUDDY_GAME_EVEN, 100).eyes, BUDDY_EYES_OPEN);
    CHECK_EQ(face_at(BUDDY_GAME_THINK, BUDDY_GAME_EVEN, 100).eyes, BUDDY_EYES_FOCUS);
    CHECK(face_at(BUDDY_GAME_READY, BUDDY_GAME_EVEN, BUDDY_GAME_READY_MS / 2).lift > 0);

    // The faces he holds while you take your time (watching, thinking, after the round) say honestly when they
    // change next: nothing differs before it, it is never 0, so he wakes the screen only for real changes. (Hops
    // update at the motion tick, as everywhere.)
    for (uint8_t f = 0; f < BUDDY_GAME_FACE_COUNT; f++) {
        const uint32_t from = f == BUDDY_GAME_READY ? 1000 : f >= BUDDY_GAME_WON ? BUDDY_GAME_REACT_MS : 0;
        for (uint32_t t = from; t < 60000;) {
            const buddy_game_t g = {f, BUDDY_GAME_EVEN, 1, 0};
            buddy_pose_t a = {0}, b = {0};
            const uint32_t next = buddy_game_pose(&g, t, &a);
            CHECK(next >= 1);
            const uint32_t step = next > 1 ? next - 1 : 0;
            buddy_game_pose(&g, t + step, &b);
            CHECK(memcmp(&a, &b, sizeof(a)) == 0);
            t += next;
        }
    }
}

// The brain plays the game over his mood: busy for shows while it lasts, refused while a show plays, a mood chosen
// meanwhile taken but not shown until the game is over, and then his mood carries on.
void test_game_brain_plays_over_his_mood(void)
{
    buddy_brain_t b;
    buddy_brain_init(&b, 0, 7);
    buddy_brain_set_mood(&b, BUDDY_MOOD_HAPPY, 0, 0);
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_SNEEZE, 100));
    const buddy_game_t think = {BUDDY_GAME_THINK, BUDDY_GAME_EVEN, 1, 200};
    CHECK(!buddy_brain_game(&b, &think, 200)); // the sneeze plays to its end
    CHECK_EQ(b.state, BUDDY_STATE_SNEEZE);
    CHECK(buddy_brain_game(&b, &think, 100 + BUDDY_SNEEZE_MS));
    CHECK_EQ(b.state, BUDDY_STATE_GAME);
    CHECK(buddy_brain_busy(&b, 100000));
    CHECK(!buddy_brain_event(&b, BUDDY_EVENT_POKE, 10000));
    CHECK(!buddy_brain_event(&b, BUDDY_EVENT_KITT, 10000));

    buddy_pose_t p;
    buddy_brain_tick(&b, 10000, &p);
    CHECK_EQ(p.state, BUDDY_STATE_GAME);
    CHECK_EQ(p.eyes, BUDDY_EYES_FOCUS);
    buddy_brain_set_mood(&b, BUDDY_MOOD_SLEEPY, 0, 11000); // taken...
    CHECK_EQ(buddy_brain_mood(&b, 11000), BUDDY_MOOD_SLEEPY);
    buddy_brain_tick(&b, 11100, &p);
    CHECK_EQ(p.state, BUDDY_STATE_GAME); // ...but he keeps playing
    const buddy_game_t won = {BUDDY_GAME_WON, BUDDY_GAME_EVEN, 1, 12000};
    CHECK(buddy_brain_game(&b, &won, 12000)); // a new face, same game
    buddy_brain_tick(&b, 12100, &p);
    CHECK_EQ(p.eyes, BUDDY_EYES_HAPPY);

    buddy_brain_game_leave(&b, 20000);
    CHECK_EQ(b.state, BUDDY_STATE_IDLE);
    CHECK(!buddy_brain_busy(&b, 20000));
    CHECK_EQ(buddy_brain_mood(&b, 20000), BUDDY_MOOD_SLEEPY);
    buddy_brain_tick(&b, 20100, &p);
    CHECK_EQ(p.state, BUDDY_STATE_IDLE);
    buddy_brain_game_leave(&b, 21000); // not playing: nothing happens
    CHECK_EQ(b.state, BUDDY_STATE_IDLE);
    CHECK(buddy_brain_event(&b, BUDDY_EVENT_POKE, 22000));
}

static void cell_center(const menu_ttt_t *t, int cell, int *x, int *y)
{
    const gfx_rect_t r = cell < TTT_CELLS ? menu_ttt_cell_rect(cell) : menu_ttt_strip_rect();
    *x = t->area.x + (r.x + r.w / 2) * MENU_SCALE;
    *y = t->area.y + (r.y + r.h / 2) * MENU_SCALE;
}

static int tap_cell(menu_ttt_t *t, const ttt_t *g, int cell)
{
    int x, y;
    cell_center(t, cell, &x, &y);
    menu_ttt_touch(t, g, MENU_TOUCH_DOWN, x, y);
    return menu_ttt_touch(t, g, MENU_TOUCH_UP, x, y);
}

// The board takes touches like the menu's tiles (on release, sliding off cancels), only on empty cells while a round
// is on, and the strip only while it is the REMATCH button; the header (Back, his head) is never the game's.
void test_game_touch_takes_only_the_board(void)
{
    static menu_t m;
    const layout_t l = layout_compute(W, H, 65, false, ALIGN);
    CHECK(menu_init(&m, &menu_default, layout_panel_content(&l), 0));
    menu_ttt_t t;
    menu_ttt_init(&t, &m);
    ttt_t g;
    ttt_init(&g, 5);
    for (int i = 0; i < TTT_CELLS; i++) { // the cells fit the panel, under the header, clear of the footer
        const gfx_rect_t r = menu_ttt_cell_rect(i);
        CHECK(r.x >= 0 && r.x + r.w <= MENU_W && r.y >= 38 && r.y + r.h <= menu_ttt_strip_rect().y);
        CHECK(r.w * MENU_SCALE >= 100 && r.h * MENU_SCALE >= 100); // a fingertip, comfortably
    }
    CHECK(menu_ttt_strip_rect().y + menu_ttt_strip_rect().h <= menu_footer_rect().y);

    CHECK_EQ(tap_cell(&t, &g, 4), MENU_TTT_HIT_NONE); // no game yet
    ttt_start(&g, 1000);
    CHECK_EQ(tap_cell(&t, &g, 4), 4);
    CHECK(!menu_ttt_holding(&t));
    CHECK(ttt_play(&g, 4, 1000));
    CHECK_EQ(tap_cell(&t, &g, 4), MENU_TTT_HIT_NONE); // taken
    CHECK_EQ(tap_cell(&t, &g, 0), 0);                 // his turn: still a tap (he protests)
    CHECK_EQ(tap_cell(&t, &g, TTT_CELLS), MENU_TTT_HIT_NONE); // no rematch button yet

    int x, y;
    cell_center(&t, 1, &x, &y);
    menu_ttt_touch(&t, &g, MENU_TOUCH_DOWN, x, y);
    CHECK(menu_ttt_holding(&t));
    CHECK_EQ(t.pressed, 1);
    menu_ttt_touch(&t, &g, MENU_TOUCH_MOVE, x + MENU_SLOP_PX + 1, y); // slid away
    CHECK_EQ(t.pressed, MENU_TTT_HIT_NONE);
    CHECK_EQ(menu_ttt_touch(&t, &g, MENU_TOUCH_UP, x, y), MENU_TTT_HIT_NONE);

    const gfx_rect_t back = menu_back_rect(), head = menu_head_rect();
    CHECK_EQ(menu_ttt_hit(&t, &g, m.area.x + (back.x + 4) * MENU_SCALE, m.area.y + (back.y + 4) * MENU_SCALE),
             MENU_TTT_HIT_NONE);
    CHECK_EQ(menu_ttt_hit(&t, &g, m.area.x + (head.x + 8) * MENU_SCALE, m.area.y + (head.y + 8) * MENU_SCALE),
             MENU_TTT_HIT_NONE);

    // Play the round out with the AI on both sides of the board; then the strip is the button.
    uint32_t now = 1000;
    while (g.phase == TTT_THINKING || g.phase == TTT_YOUR_TURN) {
        now += 100;
        ttt_tick(&g, now);
        if (g.phase == TTT_YOUR_TURN) {
            const int c = ttt_best_move(g.board, TTT_X);
            CHECK_EQ(tap_cell(&t, &g, c), c);
            ttt_play(&g, c, now);
        }
    }
    CHECK_EQ(tap_cell(&t, &g, TTT_CELLS), MENU_TTT_HIT_NONE); // his reaction first
    ttt_tick(&g, now + TTT_REACTION_MS);
    CHECK_EQ(g.phase, TTT_REMATCH);
    for (int i = 0; i < TTT_CELLS; i++) {
        CHECK_EQ(tap_cell(&t, &g, i), MENU_TTT_HIT_NONE); // the board is done
    }
    CHECK_EQ(tap_cell(&t, &g, TTT_CELLS), MENU_TTT_HIT_STRIP);

    // What the screen shows follows the game.
    menu_game_t look;
    menu_ttt_look(&look, &g, &t, now + TTT_REACTION_MS);
    CHECK(look.strip == MENU_TTT_STRIP_REMATCH || look.strip == MENU_TTT_STRIP_AGAIN);
    CHECK_EQ(look.strip == MENU_TTT_STRIP_AGAIN, g.result == TTT_DRAW);
    CHECK_EQ(look.lit, g.line); // done blinking
}

// Everything he says fits his speech bubble.
void test_game_lines_fit_his_bubble(void)
{
    CHECK(ttt_line_count > 10);
    for (int i = 0; i < ttt_line_count; i++) {
        CHECK(menu_ask_fits(ttt_lines[i]));
    }
    CHECK(menu_ask_fits("TIC-TAC-TOE"));
}

// The render loop's way, two buffers repaired from dirty rectangles, with everything the game moves: into the game
// from the menu, three rounds (moves, his faces and lines, the winning line, rematches), out again. Every frame
// equals a full redraw; the panel and the stage never share a redraw; a move redraws a row, not the panel; while it
// is your turn and nothing happens, frames stay rare.
static gfx_color_t s_strip[16 * 1024];
static gfx_color_t s_buf[2][W * H];
static gfx_color_t s_ref[W * H];
static uint8_t s_mem[MENU_VIEW_MEMORY];

static buddy_game_t face_for(const ttt_t *g, uint32_t now)
{
    buddy_game_t f = {BUDDY_GAME_WATCH, BUDDY_GAME_EVEN, 1, g->phase_at};
    if ((g->phase == TTT_YOUR_TURN || g->phase == TTT_THINKING) && now - g->round_at < BUDDY_GAME_READY_MS) {
        f.face = BUDDY_GAME_READY;
        f.since = g->round_at;
    } else if (g->phase == TTT_THINKING) {
        f.face = BUDDY_GAME_THINK;
    } else if (g->phase == TTT_REACTION || g->phase == TTT_REMATCH) {
        f.face = g->result == TTT_O_WINS ? BUDDY_GAME_WON : g->result == TTT_X_WINS ? BUDDY_GAME_LOST : BUDDY_GAME_DREW;
        f.since = g->phase == TTT_REACTION ? g->phase_at : g->phase_at - TTT_REACTION_MS;
    }
    return f;
}

void test_game_redraws_match_full_redraw(void)
{
    static gfx_scene_t scene;
    static buddy_view_t view;
    static menu_t m;
    static menu_view_t mv;
    static menu_ask_t ask;
    static gfx_dirty_t changes, redraw, all;
    static gfx_repair_t repair;
    static layout_t layout;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(buddy_view_init(&view, &scene, layout.stage, 10, 0));
    CHECK(menu_init(&m, &menu_default, layout_panel_content(&layout), 0));
    CHECK(menu_view_init(&mv, &scene, &m, s_mem, sizeof(s_mem), 10));
    CHECK(menu_ask_init(&ask, &scene, layout.stage, view.home_y0 - BUDDY_MAX_LIFT * view.home_scale, 25));
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
    memset(s_buf, 0, sizeof(s_buf));
    const gfx_surface_t ref = {s_ref, W, H, W};

    buddy_brain_t brain;
    buddy_brain_init(&brain, 0, 11);
    ttt_t g;
    ttt_init(&g, 9);
    menu_ttt_t touch;
    menu_ttt_init(&touch, &m);
    const menu_status_t idle = {.mood = BUDDY_MOOD_IDLE, .office = true};

    // Into GAMES, then TIC-TAC-TOE; you play the first free cell 700 ms into your turn; REMATCH 800 ms after it
    // shows; after three rounds, a quiet spell on your turn, then Back twice.
    const uint32_t games_at = 500, ttt_at = 1200;
    int back = 0, frames = 0, bad = 0, outside = 0, rounds = 0, max_move = 0, quiet_frames = 0;
    uint32_t quiet_from = 0, quiet_to = 0, leave_at = 0, end = 0;
    bool in_games = false, playing = false, left = false;
    uint32_t moved_at = 0;
    for (uint32_t t = 0, k = 0; !end || t < end; k++) {
        CHECK(t < 200000);
        if (!in_games && t >= games_at) {
            const gfx_rect_t r = menu_slot_rect(MENU_LIST, 4);
            const int x = m.area.x + (r.x + r.w / 2) * MENU_SCALE, y = m.area.y + (r.y + r.h / 2) * MENU_SCALE;
            menu_touch(&m, MENU_TOUCH_DOWN, x, y, &idle, t);
            menu_touch(&m, MENU_TOUCH_UP, x, y, &idle, t);
            CHECK_EQ(m.category, MENU_GAMES);
            in_games = true;
        }
        if (!playing && !left && t >= ttt_at) {
            const gfx_rect_t r = menu_slot_rect(MENU_GRID, 0);
            const int x = m.area.x + (r.x + r.w / 2) * MENU_SCALE, y = m.area.y + (r.y + r.h / 2) * MENU_SCALE;
            menu_touch(&m, MENU_TOUCH_DOWN, x, y, &idle, t);
            menu_touch(&m, MENU_TOUCH_UP, x, y, &idle, t);
            CHECK_EQ(m.category, MENU_TTT);
            ttt_start(&g, t);
            playing = true;
        }
        uint32_t next = 40;
        if (playing) {
            const uint32_t game_next = ttt_tick(&g, t);
            next = game_next < next ? game_next : next;
            if (g.phase == TTT_YOUR_TURN && rounds < 3 && t - g.phase_at >= 700) {
                int c = 0;
                while (g.board[c] != TTT_EMPTY) {
                    c++;
                }
                CHECK_EQ(tap_cell(&touch, &g, c), c);
                CHECK(ttt_play(&g, c, t));
                moved_at = t;
            }
            if (g.phase == TTT_REMATCH && t - g.phase_at >= 800) {
                if (++rounds < 3) {
                    CHECK_EQ(tap_cell(&touch, &g, TTT_CELLS), MENU_TTT_HIT_STRIP);
                    CHECK(ttt_rematch(&g, t));
                } else if (!quiet_from) {
                    CHECK(ttt_rematch(&g, t)); // a fourth round, left alone on your turn
                }
            }
            if (rounds == 3 && !quiet_from && g.phase == TTT_YOUR_TURN && t - g.round_at > TTT_OPENING_MS + 500) {
                quiet_from = t;
                quiet_to = t + 10000;
                leave_at = quiet_to + 500;
            }
            const buddy_game_t face = face_for(&g, t);
            CHECK(buddy_brain_game(&brain, &face, t));
            if (leave_at && t >= leave_at) { // Back: to GAMES, his mood carries on
                const gfx_rect_t r = menu_back_rect();
                const int x = m.area.x + (r.x + r.w / 2) * MENU_SCALE, y = m.area.y + (r.y + r.h / 2) * MENU_SCALE;
                menu_touch(&m, MENU_TOUCH_DOWN, x, y, &idle, t);
                menu_touch(&m, MENU_TOUCH_UP, x, y, &idle, t);
                CHECK_EQ(m.category, MENU_GAMES);
                ttt_stop(&g);
                buddy_brain_game_leave(&brain, t);
                playing = false;
                left = true;
                end = t + 2000;
            }
        }
        buddy_pose_t pose;
        uint32_t n = buddy_brain_tick(&brain, t, &pose);
        next = n < next ? n : next;
        n = buddy_view_apply(&view, &pose, t);
        next = n < next ? n : next;
        menu_status_t st = idle;
        st.busy = playing;
        menu_game_t look;
        if (playing) {
            n = menu_ttt_look(&look, &g, &touch, t);
            next = n < next ? n : next;
            st.game = &look;
        }
        n = menu_view_apply(&mv, &m, &st, t);
        next = n < next ? n : next;
        menu_ask_say(&ask, playing ? ttt_line(&g, t) : NULL);

        gfx_dirty_clear(&changes);
        gfx_scene_collect(&scene, &changes);
        if (changes.count > 0) {
            gfx_repair_next(&repair, &changes, &redraw);
            const gfx_surface_t dst = {s_buf[back], W, H, W};
            gfx_scene_render(&scene, &redraw, &dst);
            gfx_scene_render(&scene, &all, &ref);
            bad += memcmp(s_buf[back], s_ref, sizeof(s_ref)) != 0;
            if (++frames > 2) {
                int panel_area = 0;
                for (int i = 0; i < redraw.count; i++) {
                    const bool in_panel = gfx_rect_contains_rect(layout.panel, redraw.rect[i]);
                    outside += !in_panel && !gfx_rect_contains_rect(layout.stage, redraw.rect[i]);
                    panel_area += in_panel ? redraw.rect[i].w * redraw.rect[i].h : 0;
                }
                // A move that does not end the round: its cell's row and the footer (the winning line blinks over
                // the whole board, and a rematch clears it: not counted).
                if (moved_at && t - moved_at < 400 && g.phase == TTT_THINKING) {
                    max_move = panel_area > max_move ? panel_area : max_move;
                }
                quiet_frames += quiet_from && t >= quiet_from && t < quiet_to;
            }
            back ^= 1;
        }
        t += next + (k % 5 == 0 ? 3 : 0);
    }
    const int panel = layout.panel.w * layout.panel.h;
    printf("    game: %d frames, %d rounds, largest panel redraw for a move %d px (panel %d px), %d frames in 10 s "
           "on your turn\n",
           frames, rounds, max_move, panel, quiet_frames);
    CHECK_EQ(bad, 0);
    CHECK_EQ(outside, 0);
    CHECK_EQ(rounds, 3);
    // A move redraws its row and the footer; repairing the older buffer adds at most the row of the move before.
    CHECK(max_move > 0 && max_move <= (2 * MENU_TTT_CELL + MENU_FOOTER_H) * MENU_SCALE * layout.panel.w);
    CHECK(max_move < panel / 2);
    CHECK(quiet_frames <= 50); // his breathing, blinks and glances, the header's blink: about 4 frames a second
    CHECK_EQ(brain.state, BUDDY_STATE_IDLE);
    CHECK(ask.bubble.flags & GFX_SPRITE_HIDDEN);
    for (int i = 1; i < MENU_SLOTS; i++) { // GAMES again: its one tile, nothing of the board left
        CHECK(mv.slot[i].flags & GFX_SPRITE_HIDDEN);
    }
    CHECK(!(mv.slot[0].flags & GFX_SPRITE_HIDDEN));
}
