// The PC's load on the device: what the companion last measured on the PC it runs on (sys.set: processor, memory, the
// system disk C:), held as a lease like Claude usage's (usage.h). The device reads nothing itself (docs/protocol.md,
// "PC load").
//
// - The lease: sys.set state=on with at least one stat holds for SYSINFO_LEASE_MS; the companion renews it every 5 to
//   10 s with fresh numbers. When the companion goes away the strip disappears by itself; a companion hello drops it.
//
// Pure logic: no I/O, no clock of its own; host-tested.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYSINFO_LEASE_MS 30000u // the numbers without a sys.set refresh

typedef struct {
    bool on;            // the companion's last word
    uint32_t lease_end; // ...valid until
    link_sys_t sys;
} sysinfo_t;

// Nothing to show (also the start).
void sysinfo_init(sysinfo_t *s);

// The companion said hello (a new connection): forget what the previous one said.
void sysinfo_reset(sysinfo_t *s);

// sys.set from the companion (sys ignored when off).
void sysinfo_set(sysinfo_t *s, bool on, const link_sys_t *sys, uint32_t now);

// At least one stat (sys has one).
bool sysinfo_has_stats(const link_sys_t *sys);

// The lease holds and there is at least one stat: the strip shows.
bool sysinfo_available(const sysinfo_t *s, uint32_t now);

// ms until the lease runs out; UINT32_MAX when there is none.
uint32_t sysinfo_next(const sysinfo_t *s, uint32_t now);

#ifdef __cplusplus
}
#endif
