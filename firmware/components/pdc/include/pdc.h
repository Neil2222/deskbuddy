// The device side of the PDC Manager integration, "Ze Doen Maar Wat" (ZDMW): whether the companion says it can
// click for us, the one click that may be under way, and the cooldown after it. The companion does all the HTTP and
// holds the login; the device never sees credentials (docs/protocol.md, "PDC Manager").
//
// - Availability is a lease: pdc.set state=on holds for PDC_LEASE_MS, the companion renews it every 10 s. When the
//   companion goes away the PDC menu disappears by itself; a companion hello resets it.
// - A click: the user confirms JA, pdc_send() hands out a fresh id (evt pdc.click id=), and nothing else can be sent
//   until its pdc.result arrives (or PDC_REPLY_MS pass: then it may or may not have been sent) and PDC_COOLDOWN_MS
//   have passed since it was sent. Results for other ids (late, repeated) are ignored, so one tap is one request.
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

#define PDC_LEASE_MS 30000u    // availability without a pdc.set refresh
#define PDC_COOLDOWN_MS 5000u  // after a confirmed click (the companion keeps the same cooldown)
#define PDC_REPLY_MS 20000u    // a click without pdc.result is given up (the companion's request times out at 10 s)
#define PDC_TEXT_MAX 64        // longest LED sign line, including the NUL

typedef struct {
    bool on;            // the companion's last word
    uint32_t lease_end; // ...valid until
    int32_t count;      // the ZDMW counter; -1: unknown
    uint16_t next_id;
    uint16_t pending;   // the click waiting for its result; 0: none
    uint32_t sent_at;   // when the last click was sent
    bool sent;          // a click was sent (sent_at is meaningful)
} pdc_t;

// first_id: where the ids start (random per boot, so a companion that missed the reboot cannot mix them up).
void pdc_init(pdc_t *p, uint16_t first_id);

// The companion said hello (a new connection): forget what the previous one said. A pending click keeps waiting.
void pdc_reset(pdc_t *p);

// pdc.set from the companion. count < 0: keep the one we know.
void pdc_set(pdc_t *p, bool on, int32_t count, uint32_t now);

// The companion says the integration is on, and said so recently: the PDC menu shows.
bool pdc_available(const pdc_t *p, uint32_t now);

// Available, no click waiting for its result, and the cooldown is over: the ZDMW button works.
bool pdc_ready(const pdc_t *p, uint32_t now);

// The user confirmed (JA): returns the click's id to send as evt pdc.click, or 0 if not ready (nothing to send).
uint16_t pdc_send(pdc_t *p, uint32_t now);

// pdc.result from the companion. True when it ends the waiting click (show its outcome); false for any other id.
bool pdc_result(pdc_t *p, uint16_t id, link_pdc_result_t result, int32_t count, uint32_t now);

// The waiting click has had no result for PDC_REPLY_MS: true once, and it is given up (maybe it was sent).
bool pdc_expired(pdc_t *p, uint32_t now);

// ms until something changes on its own (the lease runs out, the cooldown ends, a click is given up); UINT32_MAX
// when nothing is scheduled.
uint32_t pdc_next(const pdc_t *p, uint32_t now);

// The LED sign's line for an outcome (teams: 1, 0, or -1 unknown; count < 0 unknown). Never says "not sent" when it
// may have been sent.
void pdc_text(char *buf, size_t size, link_pdc_result_t result, int32_t count, int teams);

#ifdef __cplusplus
}
#endif
