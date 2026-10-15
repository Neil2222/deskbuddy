// What goes on in Buddy's head, drawn on the stage behind him (FOCUS, after MonkeyUser's "Focus"): the plan he builds
// in the air (nodes A..E with arrows, a flowchart), the "HEY! DO YOU HAVE 1 SEC?" from off-stage, the POOF that ends
// it, "NEVERMIND", and the pieces drifting about while he wonders "WHAT WAS I DOING?". The brain's pose says what to
// show (buddy_thought_t); this draws it.
//
// One sprite over the top of the stage at MENU_THOUGHT_SCALE, in the menu's palette and the LED sign's font, light
// lines on the dark stage like chalk. Redrawn in place only when the thought changes; hidden when there is none.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "buddy_pose.h"
#include "gfx_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MENU_THOUGHT_SCALE 3
#define MENU_THOUGHT_W 224 // pixels at MENU_THOUGHT_SCALE: the stage's width...
#define MENU_THOUGHT_H 62  // ...and its top, down to his shoulders
#define MENU_THOUGHT_MEMORY (MENU_THOUGHT_W * MENU_THOUGHT_H)

typedef struct {
    gfx_scene_t *scene;
    gfx_sprite_t sprite;
    gfx_image_t image;
    uint8_t *px;
    int cx;                // his head's centre, in canvas pixels
    bool visible;
    buddy_thought_t shown; // what the canvas holds
} menu_thought_t;

// Places the canvas over the top of `stage` (screen pixels), the plan centred on head_cx (the screen x of his head's
// centre), and adds it to the scene at z (below Buddy, so he stands in front of his thoughts), hidden. mem: at least
// MENU_THOUGHT_MEMORY bytes, read by the compositor whenever it redraws (internal RAM if there is room). Returns false
// if the stage is too small, the memory missing, the scene full, or the art malformed.
bool menu_thought_init(menu_thought_t *t, gfx_scene_t *scene, gfx_rect_t stage, int head_cx, uint8_t *mem,
                       size_t mem_size, int z);

// Shows the thought (NULL, or all zero: hidden).
void menu_thought_apply(menu_thought_t *t, const buddy_thought_t *thought);

#ifdef __cplusplus
}
#endif
