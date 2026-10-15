// The DeskBuddy application: everything the device does above the hardware. The character, the touchscreen menu,
// Clock Mode, the message sign, PDC Manager and the companion's commands, composed into frames.
//
// The board firmware (firmware/deskbuddy) and the desktop simulator (firmware/simulator) both run this code. Each
// provides the hardware underneath (app_port.h): the screen, the clock, memory and the link to the companion. Each also
// runs the same loop:
//
//     app_init(&config);
//     for (;;) {
//         wait for events, at most app_frame()'s answer in ms;
//         app_event() for each touch and command that arrived;
//         wait = app_frame();
//     }
//
// app_init, app_event and app_frame run on one thread (the board's render task), which owns the scene.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_TOUCH_DOWN,
    APP_TOUCH_MOVE,
    APP_TOUCH_UP,
    APP_COMMAND, // from the companion: see command
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    int16_t x, y;           // screen pixels (first touch point)
    int64_t at_us;          // app_port_time_us() when the touch happened (the touch interrupt on the board)
    link_command_t command; // APP_COMMAND only
} app_event_t;

// What the board takes from menuconfig ("DeskBuddy"); the simulator uses the same defaults.
typedef struct {
    int width, height;     // the screen in pixels
    int stage_percent;     // the character stage's share of the width; the rest is the control panel
    bool panel_left;       // the control panel on the left
    int perf_log_s;        // performance log interval (0: off)
    int demo_poke_s;       // automatic pokes, Monster sequences, Clock Mode toggles and activities every N s (0: off)
    int demo_monster_s;
    int demo_office_s;
    int demo_activity_s;
    bool gfx_verify;       // compare every frame with a full redraw (slow; a screen-sized buffer)
} app_config_t;

// Sets up the scene and everything in it. false: no memory, or the character does not fit (logged).
bool app_init(const app_config_t *config);

// A touch or a companion command. Touches are dropped until the screen is lit and while a scene change is under way.
void app_event(const app_event_t *ev);

// Runs the time-driven part and draws and presents a frame if anything changed. Returns how long to wait for events
// before calling it again (ms; it is also called after events right away).
uint32_t app_frame(void);

#ifdef __cplusplus
}
#endif
