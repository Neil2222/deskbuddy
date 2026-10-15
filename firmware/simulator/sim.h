// The DeskBuddy desktop simulator (Windows): the board's application (components/app) in a Win32 window, with the
// mouse as the touchscreen and a local TCP port in place of the USB serial link. sim_port.c implements app_port.h.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app.h"

#define SIM_DEFAULT_PORT 47801 // the companion's DeviceLink.SimulatorPort

// Events for the application thread (the window's thread), from the mouse and the link thread. Like the board's
// queue: moves are dropped when it is full, touch-down and -up evict the oldest. false: a command did not fit.
bool sim_queue_push(const app_event_t *ev);
bool sim_queue_pop(app_event_t *ev);
// Signalled while events are waiting (a Win32 event handle, for MsgWaitForMultipleObjects).
void *sim_queue_signal(void);

// Sets up the clock, the queue and the frame buffers. Call first.
void sim_port_init(int width, int height);

// The buffer last presented (what the panel shows), and whether the backlight is on.
const uint16_t *sim_front_buffer(void);
bool sim_lit(void);

// Called on the application thread after each present (the window repaints).
extern void (*sim_on_present)(void);

// Starts listening on 127.0.0.1:port for the companion (one connection at a time). false: the port is taken.
bool sim_link_start(uint16_t port);

// Called from the link thread when the companion connects (true) or goes (false).
extern void (*sim_on_link)(bool connected);
