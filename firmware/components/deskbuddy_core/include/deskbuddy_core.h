// DeskBuddy application core: everything between "something happened" and "these pixels changed", shared by the
// ESP32 firmware and the Windows simulator.
//
// One frame, on any platform:
//   core_handle_event()  for each queued input
//   core_update(now)     advance behavior and animation; returns ms until the next change
//   core_collect()       regions to redraw into the back buffer, or NULL when nothing changed (then don't present)
//   core_render()        draw them, then present the back buffer and swap
// The core assumes two alternating frame buffers (the device's model; the simulator emulates it), and every frame
// returned by core_collect() must be presented.
//
// The core keeps pointers into itself (the scene uses the layout as background context): do not move a core_t
// after core_init().
#pragma once

#include "buddy_brain.h"
#include "buddy_view.h"
#include "core_event.h"
#include "gfx_repair.h"
#include "gfx_scene.h"
#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CORE_SCREEN_W 1024
#define CORE_SCREEN_H 600
#define CORE_X_ALIGN 32 // dirty rectangles start/end on 64-byte PSRAM cache lines

typedef struct {
    int screen_w, screen_h;
    int stage_percent;     // character stage share of the width; the rest is the control-panel area
    bool panel_left;
    uint32_t seed;         // behavior randomness (blink timing)
    uint32_t demo_poke_ms; // poke the character every N ms (0 = off)
} core_config_t;

typedef struct {
    uint32_t events;    // handled by core_handle_event()
    uint32_t reactions; // events that made the character react
    uint32_t ignored;   // unknown event types or signal ids
    uint32_t frames;    // frames returned by core_collect()
} core_counters_t;

typedef struct {
    core_config_t cfg;
    layout_t layout;
    gfx_scene_t scene;
    buddy_brain_t brain;
    buddy_view_t view;
    buddy_pose_t pose; // last pose applied
    gfx_dirty_t changes, redraw;
    gfx_repair_t repair;
    uint32_t now; // latest time seen (updates and events); the core's clock never runs backwards
    uint32_t next_demo;
    bool second_frame_pending;
    core_counters_t count;
    struct {
        int16_t x, y;
        bool down;
    } touch; // last touch, for debugging
} core_t;

// 1024x600, stage 65 % on the left, panel on the right, seed 1, no demo pokes.
void core_config_defaults(core_config_t *cfg);

// strip: composition work buffer (fast RAM), at least one screen row. Returns false on invalid configuration or
// when the character does not fit the stage.
bool core_init(core_t *c, const core_config_t *cfg, gfx_color_t *strip, int strip_px, uint32_t now_ms);

// Applies one event. Returns true when it made the character react (e.g. a touch on the robot).
bool core_handle_event(core_t *c, const core_event_t *ev);

// Advances behavior and animation to now_ms. Returns ms until the pose may change next; 0 means "collect again
// right away" (only right after the first frame, so both buffers get drawn before the screen is shown).
uint32_t core_update(core_t *c, uint32_t now_ms);

// Regions to redraw into the back buffer: this frame's changes plus the previous frame's, which the back buffer
// has not seen. NULL when nothing changed. Valid until the next call.
const gfx_dirty_t *core_collect(core_t *c);

void core_render(const core_t *c, const gfx_dirty_t *redraw, const gfx_surface_t *back);

// True once both frame buffers hold a complete frame (after two presented frames): safe to light the screen.
static inline bool core_ready(const core_t *c)
{
    return c->count.frames >= 2;
}

// Draws the whole scene into scratch and compares it with frame. Returns the number of differing pixels; if any,
// *first_x/*first_y (optional) receive the first one.
uint32_t core_verify(const core_t *c, const gfx_surface_t *frame, const gfx_surface_t *scratch, int *first_x,
                     int *first_y);

// CRC-32 (IEEE) of a frame's pixels, row by row, each pixel as low byte then high byte. Identical on every
// platform, so device and simulator frames can be compared.
uint32_t core_frame_crc32(const gfx_surface_t *s);

#ifdef __cplusplus
}
#endif
