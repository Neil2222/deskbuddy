// A question Buddy asks on his stage ("PEUKIE ROKEN?", "ZE DOEN MAAR WAT?"), answered with two buttons: a speech
// bubble above his head, NEE at the bottom left of the stage, JA at the bottom right, and a bar between them that
// runs down while he waits (red in the last third, at most the last 10 s).
//
// Pixel art in the menu's style (its palette and buttons, the LED sign's font) at MENU_ASK_SCALE. The bubble fits
// the question (one line, or two split at '\n'). It shows while the caller says there is time left (the brain's
// buddy_brain_asking(): the brain owns the question and its timeout), so it can never be left standing. Touches work
// like the menu's: a button sinks in under the finger and answers when the finger is lifted on it; sliding off
// cancels. Each piece is one sprite, redrawn in place only when it changes.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx_scene.h"
#include "menu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_ASK_SCALE 3
#define MENU_ASK_BUBBLE_W_MIN 84  // pixels at MENU_ASK_SCALE: the bubble, its tail included...
#define MENU_ASK_BUBBLE_W_MAX 160
#define MENU_ASK_BUBBLE_H_MAX 31  // two lines
#define MENU_ASK_BUTTON_W 44 // ...each button, including its ledge...
#define MENU_ASK_BUTTON_H 28
#define MENU_ASK_TIMER_W 64  // ...and the bar
#define MENU_ASK_TIMER_H 5
#define MENU_ASK_LOW_MS 10000 // the bar turns red when this little time is left (a third of a short question)

// The questions he asks (each must pass menu_ask_fits(); host-tested).
#define MENU_QUESTION_PEUKIE "PEUKIE ROKEN?"
#define MENU_QUESTION_ZDMW "ZE DOEN MAAR WAT?\nTEAMS-BERICHT VERSTUREN?"

typedef enum {
    MENU_ANSWER_NONE = -1,
    MENU_ANSWER_NO,  // left
    MENU_ANSWER_YES, // right
} menu_answer_t;

typedef struct {
    gfx_scene_t *scene;
    gfx_rect_t stage;
    int head_top;
    bool visible;
    const char *shown_question; // the text on the bubble (compared by pointer)
    gfx_sprite_t bubble, button[2], timer;
    gfx_image_t bubble_img, button_img[2], timer_img;
    uint8_t bubble_px[MENU_ASK_BUBBLE_W_MAX * MENU_ASK_BUBBLE_H_MAX];
    uint8_t button_px[2][MENU_ASK_BUTTON_W * MENU_ASK_BUTTON_H];
    uint8_t timer_px[MENU_ASK_TIMER_W * MENU_ASK_TIMER_H];
    // A touch in progress.
    bool down;
    int16_t down_x, down_y;
    int8_t target, pressed; // the button the finger went down on and is still on; -1 none
    // What is on screen.
    int8_t shown_pressed;
    int shown_bar; // filled width * 2 + low; -1: not drawn
} menu_ask_t;

// Places the questions on `stage` (screen pixels): the bubble's tail ends just above head_top (the top of Buddy's head
// at his highest hop), the buttons in the bottom corners. Adds its sprites to the scene at z, hidden. Returns false if
// the largest bubble does not fit, the scene is full, or the art is malformed.
bool menu_ask_init(menu_ask_t *a, gfx_scene_t *scene, gfx_rect_t stage, int head_top, int z);

// The question fits a bubble: one or two lines ('\n'), each in the sign's font within MENU_ASK_BUBBLE_W_MAX.
bool menu_ask_fits(const char *question);

// Shows `question` (a string that lives on; a new pointer redraws the bubble) with left_ms of total_ms to go (the
// bar), or hides it when question is NULL or left_ms is 0. Returns ms until it changes next on its own; UINT32_MAX
// while hidden.
uint32_t menu_ask_apply(menu_ask_t *a, const char *question, uint32_t left_ms, uint32_t total_ms);

// Feeds a touch (screen pixels). Returns the answer a completed tap on a button gives; MENU_ANSWER_NONE otherwise.
menu_answer_t menu_ask_touch(menu_ask_t *a, menu_touch_t type, int x, int y);

// A touch that went down on one of the buttons is in progress (it belongs to the question, not to the menu or Buddy).
bool menu_ask_holding(const menu_ask_t *a);

#ifdef __cplusplus
}
#endif
