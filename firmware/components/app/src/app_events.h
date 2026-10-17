// Recorded events: the ones the companion keeps as history (evt act, evt did), made recoverable after it was away.
//
// Identity: every event is (device id, boot id, seq). The device id is stable (the board's MAC, the simulator's
// machine), the boot id a random 64-bit number chosen at every start, seq counts up from 1 within a boot. hello and
// evt boot carry dev= and boot=; each recorded event carries seq= and its time: t=<UTC ms> once the clock has been set
// (time.set), else up=<ms since start>; an uptime is never turned into a date here.
//
// The last APP_EVENTS_KEPT recorded events stay in RAM (PSRAM; nothing is written to flash, so a power cut loses them).
// log.since replays the ones after a seq, marked replay=1, a few per frame so the screen keeps its pace, then
// replies ok boot= last= lost= (lost: events after that seq no longer kept). Replays repeat the same lines, so storing
// them twice is harmless when the companion keys on (dev, boot, seq).
//
// app_events_identity() runs on the link side before anything else (app_link_boot); the rest on the application
// thread.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_EVENTS_KEPT 64
#define APP_EVENTS_REPLAY_PER_FRAME 2 // lines per frame while replaying (the console UART blocks while it sends)

void app_events_identity(void);
const char *app_events_device(void); // "441bf68d1178"
const char *app_events_boot(void);   // 16 lower-case hex digits

// The ring buffer (PSRAM). Without memory events are still sent, only not kept (a replay then reports them lost).
void app_events_init(void);

// Sends a recorded event: body + " seq=<n> t=<ms>" (clock_set) or " up=<ms>", and keeps it.
void app_events_send(const char *body, bool clock_set, int64_t utc_ms, uint32_t uptime_ms);

// Starts a replay for command `reply_seq` (log.since). Asked about another boot (or none): everything kept of this
// boot. false: a replay is already under way.
bool app_events_replay_start(uint32_t reply_seq, bool has_boot, uint64_t boot, uint32_t since);

// Sends the next few replayed lines, and the reply after the last. Returns true while a replay is under way.
bool app_events_replay_step(void);
