// Switching between the DeskBuddy scene and the Office Mode clock.
//
// Changing scenes repaints the whole screen, in both frame buffers. The backlight is left alone: the IO expander
// that drives it did not apply brightness changes reliably on the device (random levels, sometimes stuck until
// power loss; see docs/rendering-results.md), and a switch redrawn with the light on looked fine to the user,
// although full-screen redraws make the scanout resync (counted in the log).
//
// Pure logic, driven by the render loop: each step says when to swap the scene and what to invalidate; the loop
// reports every presented frame.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OFFICE_SETTLED,
    OFFICE_REDRAW, // both buffers get a full redraw of the new scene
} office_stage_t;

typedef struct {
    bool office; // the clock is the scene being drawn
    bool want_office;
    office_stage_t stage;
    uint8_t presents; // frames presented while redrawing
} office_switch_t;

typedef struct {
    bool swap; // the scene to draw just changed (now office_switch_t.office): swap the sprites now
    // Redraw of both frame buffers, as at boot: the first frame invalidates the whole screen; the second
    // invalidates just one pixel, which is enough to present a frame, and the double-buffer repair redraws the
    // whole other buffer. (Invalidating everything twice would make the repair redraw a third full frame.)
    bool redraw_all;  // invalidate the whole screen for this frame
    bool redraw_kick; // invalidate a single pixel, so this frame gets presented
    bool redrawn;     // both buffers hold the new scene
    uint32_t next_ms; // call again within this many ms (UINT32_MAX: only after a request)
} office_step_t;

// Starts settled on the normal scene.
void office_switch_init(office_switch_t *s);

// Asks for the clock (true) or the normal scene (false). Takes effect on the next step; a request during a
// switch is carried out once that switch has finished.
void office_switch_request(office_switch_t *s, bool office);

// True while redrawing.
bool office_switch_busy(const office_switch_t *s);

// The clock's face changed while it shows: both buffers are redrawn again, as after a switch (a request to switch
// waits until that is done). Does nothing on the normal scene.
void office_switch_redraw(office_switch_t *s);

office_step_t office_switch_step(office_switch_t *s);

// Call after every presented frame.
void office_switch_presented(office_switch_t *s);

#ifdef __cplusplus
}
#endif
