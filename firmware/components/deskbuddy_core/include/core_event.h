// Events into the DeskBuddy behavior system. Platform and transport independent.
//
// Every input source produces these: the device's touch controller, the simulator's mouse, scripts, and later the
// Windows companion bridge over USB. A source only translates its own input into a core_event_t; what the event
// means is decided by the core, the same way on every platform.
//
// Threading: the core is single-threaded. Producers on other tasks/threads hand events to the thread that owns the
// core through a queue the platform provides (a FreeRTOS queue on the device, the message loop on Windows).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CORE_EVENT_TOUCH_DOWN = 1,
    CORE_EVENT_TOUCH_MOVE = 2,
    CORE_EVENT_TOUCH_UP = 3,
    CORE_EVENT_SIGNAL = 4, // a named happening from outside the screen (see core_signal_t)
} core_event_type_t;

// Signal identifiers. The numeric values are part of the external interface (scripts, and later the USB protocol):
// never renumber or reuse a value; add new ones at the end. Unknown ids are counted and ignored, so older firmware
// tolerates newer senders.
typedef enum {
    CORE_SIGNAL_POKE = 1, // poke the character as if it had been tapped
} core_signal_t;

typedef struct {
    core_event_type_t type;
    uint32_t time_ms; // when it happened, on the clock the core is driven with
    struct {
        int16_t x, y; // screen pixels; TOUCH_* only
    } touch;
    struct {
        uint16_t id;   // core_signal_t; SIGNAL only
        int32_t value; // signal-specific argument (unused so far)
    } signal;
} core_event_t;

#ifdef __cplusplus
}
#endif
