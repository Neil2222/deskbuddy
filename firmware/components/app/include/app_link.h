// The device side of the companion link (protocol: components/link, docs/protocol.md), on the board's UART and the
// simulator's TCP socket alike.
//
// Received bytes are cut into lines. hello and ping are answered right away; other valid commands are handed to
// the application thread as APP_COMMAND events (it owns the character and the scene, and replies once it has
// acted). Lines that are not frames are ignored.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app.h"
#include "link.h"

#ifdef __cplusplus
extern "C" {
#endif

// The commands this firmware offers (hello caps=) and the message styles it draws (hello styles=). track: act iid=,
// evt act, status act=, evt mood src=; did: evt did (MY DAY).
#define APP_LINK_CAPS "poke,monster,mode,time,status,msg,mood,act,pdc,track,did"
#define APP_LINK_STYLES "marquee"

// Passes a command event to the application thread. false: it could not be queued (the command gets "err busy").
typedef bool (*app_link_deliver_fn)(const app_event_t *ev, void *ctx);

// One receive direction: the line being assembled.
typedef struct {
    char line[LINK_LINE_MAX];
    size_t len;
    bool overflow; // the rest of an over-long line is discarded
} app_link_rx_t;

// Starts a new line (a new connection).
void app_link_rx_reset(app_link_rx_t *rx);

// Takes received bytes; every complete line is handled (see above).
void app_link_feed(app_link_rx_t *rx, const uint8_t *data, size_t n, app_link_deliver_fn deliver, void *ctx);

// Sends one frame: "<DB1 <seq> <body> #crc". seq 0 for events. Safe from any thread.
void app_link_send(uint32_t seq, const char *body);

// "evt boot": the device has started.
void app_link_boot(void);

#ifdef __cplusplus
}
#endif
