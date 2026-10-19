// Claude usage on the device: what the companion last said about the Claude plan's limits (usage.set), held as a
// lease like PDC Manager's. The companion reads the numbers from Claude's API; the device never goes online and holds
// no credentials (docs/protocol.md, "Claude usage").
//
// - The lease: usage.set state=on with at least one window holds for USAGE_LEASE_MS; the companion renews it every
//   10 s. When the companion goes away the strip disappears by itself; a companion hello drops it at once.
// - The countdown to a window's reset, from the device clock, in whole minutes rounded up (so it never says 0m):
//   at least a day "3d 2u" (days and hours, Dutch u), at least an hour "2:30", less "45m", reached "nu".
//
// Pure logic: no I/O, no clock of its own; host-tested.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USAGE_LEASE_MS 30000u // the numbers without a usage.set refresh
#define USAGE_COUNTDOWN_MAX 12 // longest countdown text, including the NUL (a reset in 2100: "27000d 23u")

typedef struct {
    bool on;            // the companion's last word
    uint32_t lease_end; // ...valid until
    link_usage_t win[LINK_USAGE_WINDOWS];
} usage_t;

// Nothing to show (also the start).
void usage_init(usage_t *u);

// The companion said hello (a new connection): forget what the previous one said.
void usage_reset(usage_t *u);

// usage.set from the companion (win: LINK_USAGE_WINDOWS windows; ignored when off).
void usage_set(usage_t *u, bool on, const link_usage_t *win, uint32_t now);

// The lease holds and there is at least one window: the strip shows.
bool usage_available(const usage_t *u, uint32_t now);

// ms until the lease runs out; UINT32_MAX when there is none.
uint32_t usage_next(const usage_t *u, uint32_t now);

// The countdown from now (UTC ms) to end (UTC s) into buf (USAGE_COUNTDOWN_MAX bytes is enough). Returns ms until the
// text may change (the next whole minute); UINT32_MAX once it says "nu".
uint32_t usage_countdown(char *buf, size_t size, int64_t now_utc_ms, int64_t end_utc_s);

// How full a window is, by its pct: below 50, 75, 90, and from 90 on.
typedef enum { USAGE_GREEN, USAGE_YELLOW, USAGE_ORANGE, USAGE_RED } usage_level_t;
usage_level_t usage_level(int pct);

#ifdef __cplusplus
}
#endif
