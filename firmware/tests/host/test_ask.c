#include <string.h>

#include "buddy_activity.h"
#include "buddy_brain.h"
#include "buddy_view.h"
#include "gfx_repair.h"
#include "layout.h"
#include "menu_ask.h"
#include "test.h"

enum { W = 1024, H = 600, ALIGN = 32 };

static void asking(buddy_brain_t *b, buddy_mood_t mood)
{
    buddy_brain_init(b, 0, 5);
    buddy_brain_set_mood(b, mood, 0, 0);
    CHECK_EQ(buddy_brain_asking(b, 1000), 0);
    CHECK(!buddy_brain_answer(b, true, 1000)); // nothing was asked
    CHECK(buddy_brain_event(b, BUDDY_EVENT_ASK_PEUKIE, 1000));
}

void test_ask_yes_is_happy_hops_a_smoke_break_then_his_mood(void)
{
    buddy_brain_t b;
    buddy_pose_t pose;
    asking(&b, BUDDY_MOOD_WORKING);
    CHECK_EQ(buddy_brain_asking(&b, 1000), BUDDY_ASK_MS);
    // He waits, offering a cigarette, and nothing else starts meanwhile.
    for (uint32_t t = 1000; t < 21000; t += 97) {
        buddy_brain_tick(&b, t, &pose);
        CHECK_EQ(pose.state, BUDDY_STATE_ASK);
        CHECK(buddy_brain_busy(&b, t));
        CHECK(!buddy_brain_event(&b, BUDDY_EVENT_POKE, t));
        CHECK(!buddy_brain_event(&b, BUDDY_EVENT_ASK_PEUKIE, t));
        CHECK(pose.props.cig && pose.props.ember == 0);
    }
    CHECK(strcmp(buddy_activity_doing(BUDDY_STATE_ASK), "asking") == 0);
    CHECK_EQ(buddy_brain_asking(&b, 21000), BUDDY_ASK_MS - 20000);
    CHECK(buddy_brain_answer(&b, true, 21000));
    CHECK(!buddy_brain_answer(&b, false, 21001)); // answered already
    CHECK_EQ(buddy_brain_asking(&b, 21001), 0);
    buddy_brain_tick(&b, 21100, &pose);
    CHECK_EQ(pose.state, BUDDY_STATE_HAPPY);
    CHECK(!buddy_brain_busy(&b, 21100));
    // The smoke break, then back to work.
    CHECK_EQ(buddy_brain_mood(&b, 21100), BUDDY_MOOD_PEUKIE);
    buddy_brain_tick(&b, 30000, &pose);
    CHECK_EQ(pose.state, BUDDY_STATE_IDLE);
    CHECK_EQ(buddy_brain_mood(&b, 21000 + BUDDY_SMOKE_BREAK_MS - 1), BUDDY_MOOD_PEUKIE);
    CHECK_EQ(buddy_brain_mood(&b, 21000 + BUDDY_SMOKE_BREAK_MS), BUDDY_MOOD_WORKING);
    // A mood chosen during the break replaces it (and its way back) as usual.
    asking(&b, BUDDY_MOOD_WORKING);
    CHECK(buddy_brain_answer(&b, true, 2000));
    buddy_brain_set_mood(&b, BUDDY_MOOD_RELAXED, 0, 3000);
    CHECK_EQ(buddy_brain_mood(&b, 2000 + BUDDY_SMOKE_BREAK_MS + 1), BUDDY_MOOD_RELAXED);
    // Smoking already: he just carries on (no break that ends).
    asking(&b, BUDDY_MOOD_PEUKIE);
    CHECK(buddy_brain_answer(&b, true, 2000));
    CHECK_EQ(buddy_brain_mood(&b, 2000 + 10 * BUDDY_SMOKE_BREAK_MS), BUDDY_MOOD_PEUKIE);
}

void test_ask_no_or_no_answer_makes_him_briefly_sad(void)
{
    buddy_brain_t b;
    buddy_pose_t pose;
    for (int silent = 0; silent < 2; silent++) {
        asking(&b, BUDDY_MOOD_RELAXED);
        const uint32_t sad = silent ? 1000 + BUDDY_ASK_MS : 4000;
        if (!silent) {
            CHECK(buddy_brain_answer(&b, false, sad));
        } else {
            buddy_brain_tick(&b, sad - 1, &pose);
            CHECK_EQ(pose.state, BUDDY_STATE_ASK);
            CHECK_EQ(buddy_brain_asking(&b, sad - 1), 1);
            CHECK_EQ(buddy_brain_asking(&b, sad), 0);
            CHECK(!buddy_brain_answer(&b, true, sad)); // too late
        }
        int tears = 0;
        for (uint32_t t = sad; t < sad + BUDDY_SAD_MS; t += 50) {
            buddy_brain_tick(&b, t, &pose);
            CHECK(pose.mouth == BUDDY_MOUTH_FROWN || pose.mouth == BUDDY_MOUTH_PUFF); // and a sigh
            tears += pose.props.particle[0].kind == BUDDY_PART_SWEAT;
        }
        CHECK(tears > 0);
        buddy_brain_tick(&b, sad + BUDDY_SAD_MS, &pose);
        CHECK_EQ(pose.state, BUDDY_STATE_IDLE);
        CHECK(!pose.props.cig); // the cigarette went away
        CHECK_EQ(buddy_brain_mood(&b, sad + BUDDY_SAD_MS), BUDDY_MOOD_RELAXED); // no smoke break
        CHECK_EQ(buddy_brain_mood(&b, sad + 10 * BUDDY_SMOKE_BREAK_MS), BUDDY_MOOD_RELAXED);
    }
}

static gfx_color_t s_strip[16 * 1024];
static gfx_color_t s_buf[2][W * H];
static gfx_color_t s_ref[W * H];

static gfx_rect_t centre(const gfx_sprite_t *sp, int *x, int *y)
{
    const gfx_rect_t r = gfx_sprite_bounds(sp);
    *x = r.x + r.w / 2;
    *y = r.y + r.h / 2;
    return r;
}

// NEE bottom left and JA bottom right of the stage, the bubble above his head; they never cover him, whatever he
// does while he asks. Taps answer on release; sliding off or touching elsewhere gives no answer.
void test_ask_buttons_beside_him_answer_on_release(void)
{
    static gfx_scene_t scene;
    static buddy_view_t view;
    static menu_ask_t ask;
    static layout_t layout;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(buddy_view_init(&view, &scene, layout.stage, 10, 0));
    const int head_top = view.home_y0 - BUDDY_MAX_LIFT * view.home_scale;
    CHECK(!menu_ask_init(&ask, &scene, layout.stage, head_top, "A QUESTION FAR TOO LONG FOR IT", 25));
    CHECK(menu_ask_init(&ask, &scene, layout.stage, head_top, "PEUKIE ROKEN?", 25));
    CHECK_EQ(menu_ask_apply(&ask, 0, BUDDY_ASK_MS), UINT32_MAX);
    CHECK(gfx_rect_is_empty(gfx_sprite_bounds(&ask.bubble)));
    CHECK_EQ(menu_ask_touch(&ask, MENU_TOUCH_DOWN, 100, 550), MENU_ANSWER_NONE); // hidden: not there
    CHECK(!menu_ask_holding(&ask));

    CHECK(menu_ask_apply(&ask, BUDDY_ASK_MS, BUDDY_ASK_MS) > 0);
    int nx, ny, yx, yy, bx, by;
    const gfx_rect_t no = centre(&ask.button[MENU_ANSWER_NO], &nx, &ny);
    const gfx_rect_t yes = centre(&ask.button[MENU_ANSWER_YES], &yx, &yy);
    const gfx_rect_t bubble = centre(&ask.bubble, &bx, &by);
    const gfx_rect_t bar = gfx_sprite_bounds(&ask.timer);
    CHECK(gfx_rect_contains_rect(layout.stage, no) && gfx_rect_contains_rect(layout.stage, yes));
    CHECK(gfx_rect_contains_rect(layout.stage, bubble) && gfx_rect_contains_rect(layout.stage, bar));
    CHECK(nx < view.home_x0 && yx > view.home_x0 + view.art->w * view.home_scale);
    CHECK(bubble.y + bubble.h <= head_top);
    CHECK(!gfx_rect_overlaps(bar, no) && !gfx_rect_overlaps(bar, yes));
    buddy_brain_t brain;
    asking(&brain, BUDDY_MOOD_IDLE);
    gfx_sprite_t *him[] = {&view.shadow, &view.torso, &view.head, &view.antenna, &view.eye[0], &view.eye[1],
                           &view.mouth, &view.rig[0], &view.rig[1], &view.part[0], &view.part[1], &view.part[2],
                           &view.part[3], &view.part[4]};
    for (uint32_t t = 1000; t < 1000 + BUDDY_ASK_MS; t += 23) {
        buddy_pose_t pose;
        buddy_brain_tick(&brain, t, &pose);
        buddy_view_apply(&view, &pose, t);
        for (int i = 0; i < (int)(sizeof(him) / sizeof(him[0])); i++) {
            const gfx_rect_t r = gfx_sprite_bounds(him[i]);
            CHECK(!gfx_rect_overlaps(r, no) && !gfx_rect_overlaps(r, yes) && !gfx_rect_overlaps(r, bubble));
        }
    }

    // A tap on JA; a tap on NEE.
    CHECK_EQ(menu_ask_touch(&ask, MENU_TOUCH_DOWN, yx, yy), MENU_ANSWER_NONE);
    CHECK(menu_ask_holding(&ask));
    CHECK_EQ(ask.pressed, MENU_ANSWER_YES);
    CHECK_EQ(menu_ask_touch(&ask, MENU_TOUCH_MOVE, yx + 5, yy), MENU_ANSWER_NONE);
    CHECK_EQ(menu_ask_touch(&ask, MENU_TOUCH_UP, yx + 5, yy), MENU_ANSWER_YES);
    CHECK(!menu_ask_holding(&ask));
    menu_ask_touch(&ask, MENU_TOUCH_DOWN, nx, ny);
    CHECK_EQ(menu_ask_touch(&ask, MENU_TOUCH_UP, nx, ny), MENU_ANSWER_NO);
    // Slid off: nothing, and the button comes back up.
    menu_ask_touch(&ask, MENU_TOUCH_DOWN, nx, ny);
    CHECK_EQ(menu_ask_touch(&ask, MENU_TOUCH_MOVE, nx + 200, ny), MENU_ANSWER_NONE);
    CHECK_EQ(ask.pressed, MENU_ANSWER_NONE);
    CHECK_EQ(menu_ask_touch(&ask, MENU_TOUCH_UP, nx, ny), MENU_ANSWER_NONE);
    // Elsewhere (the bubble, Buddy): not the question's touch.
    CHECK_EQ(menu_ask_touch(&ask, MENU_TOUCH_DOWN, bx, by), MENU_ANSWER_NONE);
    CHECK(!menu_ask_holding(&ask));
    CHECK_EQ(menu_ask_touch(&ask, MENU_TOUCH_UP, bx, by), MENU_ANSWER_NONE);
    // Hidden halfway through a touch: the touch is gone with it.
    menu_ask_touch(&ask, MENU_TOUCH_DOWN, yx, yy);
    menu_ask_apply(&ask, 0, BUDDY_ASK_MS);
    CHECK(!menu_ask_holding(&ask));
    CHECK_EQ(menu_ask_touch(&ask, MENU_TOUCH_UP, yx, yy), MENU_ANSWER_NONE);
}

// The render loop's way: he asks, the bar runs down (amber, then red), buttons sink in and come up, and the question
// goes when he is answered. Every buffer equals a full redraw; the bar redraws when it loses a pixel, not every frame.
void test_ask_redraws_match_full_redraw(void)
{
    static gfx_scene_t scene;
    static buddy_view_t view;
    static menu_ask_t ask;
    static gfx_dirty_t changes, redraw, all;
    static gfx_repair_t repair;
    static layout_t layout;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(buddy_view_init(&view, &scene, layout.stage, 10, 0));
    CHECK(menu_ask_init(&ask, &scene, layout.stage, view.home_y0 - BUDDY_MAX_LIFT * view.home_scale, "PEUKIE ROKEN?",
                        25));
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});
    memset(s_buf, 0, sizeof(s_buf));
    const gfx_surface_t ref = {s_ref, W, H, W};

    buddy_brain_t brain;
    asking(&brain, BUDDY_MOOD_HAPPY);
    int nx = 0, ny = 0, yx = 0, yy = 0; // the buttons' centres, once they show (the first frame)
    int back = 0, frames = 0, bad = 0, wakes = 0, red = 0;
    bool answered = false, gone = false;
    const uint32_t end = 1000 + BUDDY_ASK_MS + 2000;
    for (uint32_t t = 1000; t < end;) {
        // The question shows up; NEE is pressed and slid off at 20 s, JA tapped in the last 5 s.
        if (t >= 21000 && t < 21400) {
            menu_ask_touch(&ask, t < 21200 ? MENU_TOUCH_DOWN : MENU_TOUCH_MOVE, t < 21200 ? nx : nx + 300, ny);
        }
        if (!answered && t >= 1000 + BUDDY_ASK_MS - 5000) {
            if (!menu_ask_holding(&ask)) {
                menu_ask_touch(&ask, MENU_TOUCH_DOWN, yx, yy);
            } else if (menu_ask_touch(&ask, MENU_TOUCH_UP, yx, yy) == MENU_ANSWER_YES) {
                CHECK(buddy_brain_answer(&brain, true, t));
                answered = true;
            }
        }
        buddy_pose_t pose;
        uint32_t next = buddy_brain_tick(&brain, t, &pose);
        const uint32_t view_next = buddy_view_apply(&view, &pose, t);
        next = view_next < next ? view_next : next;
        const uint32_t ask_next = menu_ask_apply(&ask, buddy_brain_asking(&brain, t), BUDDY_ASK_MS);
        next = ask_next < next ? ask_next : next;
        if (frames == 0) {
            centre(&ask.button[MENU_ANSWER_NO], &nx, &ny);
            centre(&ask.button[MENU_ANSWER_YES], &yx, &yy);
        }
        red |= ask.visible && (ask.shown_bar & 1);
        gone |= answered && !ask.visible;
        gfx_dirty_clear(&changes);
        gfx_scene_collect(&scene, &changes);
        if (changes.count > 0) {
            gfx_repair_next(&repair, &changes, &redraw);
            const gfx_surface_t dst = {s_buf[back], W, H, W};
            gfx_scene_render(&scene, &redraw, &dst);
            gfx_scene_render(&scene, &all, &ref);
            bad += memcmp(s_buf[back], s_ref, sizeof(s_ref)) != 0;
            back ^= 1;
            frames++;
        }
        wakes++;
        t += next < 1500 ? next : 1500;
    }
    CHECK(answered && gone && red);
    CHECK_EQ(bad, 0);
    CHECK(frames > 100);
    printf("    question: %d frames, %d wakes\n", frames, wakes);
}
