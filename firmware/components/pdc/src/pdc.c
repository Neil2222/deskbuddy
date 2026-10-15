#include "pdc.h"

#include <stdio.h>
#include <string.h>

static bool reached(uint32_t now, uint32_t at)
{
    return (int32_t)(now - at) >= 0;
}

void pdc_init(pdc_t *p, uint16_t first_id)
{
    memset(p, 0, sizeof(*p));
    p->count = -1;
    p->next_id = first_id ? first_id : 1;
}

void pdc_reset(pdc_t *p)
{
    p->on = false;
}

void pdc_set(pdc_t *p, bool on, int32_t count, uint32_t now)
{
    p->on = on;
    p->lease_end = now + PDC_LEASE_MS;
    if (count >= 0) {
        p->count = count;
    }
}

bool pdc_available(const pdc_t *p, uint32_t now)
{
    return p->on && !reached(now, p->lease_end);
}

bool pdc_ready(const pdc_t *p, uint32_t now)
{
    return pdc_available(p, now) && !p->pending && !(p->sent && !reached(now, p->sent_at + PDC_COOLDOWN_MS));
}

uint16_t pdc_send(pdc_t *p, uint32_t now)
{
    if (!pdc_ready(p, now)) {
        return 0;
    }
    p->pending = p->next_id;
    p->next_id = (uint16_t)(p->next_id == 65535 ? 1 : p->next_id + 1);
    p->sent_at = now;
    p->sent = true;
    return p->pending;
}

bool pdc_result(pdc_t *p, uint16_t id, link_pdc_result_t result, int32_t count, uint32_t now)
{
    (void)result;
    (void)now;
    if (!id || id != p->pending) {
        return false; // late, repeated, or never asked for
    }
    p->pending = 0;
    if (count >= 0) {
        p->count = count;
    }
    return true;
}

bool pdc_expired(pdc_t *p, uint32_t now)
{
    if (!p->pending || !reached(now, p->sent_at + PDC_REPLY_MS)) {
        return false;
    }
    p->pending = 0; // a result that still comes is ignored
    return true;
}

static uint32_t until(uint32_t now, uint32_t at, uint32_t next)
{
    if (reached(now, at)) {
        return next;
    }
    const uint32_t ms = at - now;
    return ms < next ? ms : next;
}

uint32_t pdc_next(const pdc_t *p, uint32_t now)
{
    uint32_t next = UINT32_MAX;
    if (p->on) {
        next = until(now, p->lease_end, next);
    }
    if (p->sent) {
        next = until(now, p->sent_at + PDC_COOLDOWN_MS, next);
    }
    if (p->pending) {
        next = until(now, p->sent_at + PDC_REPLY_MS, next);
    }
    return next;
}

void pdc_text(char *buf, size_t size, link_pdc_result_t result, int32_t count, int teams)
{
    switch (result) {
    case LINK_PDC_OK:
        if (count >= 0) {
            snprintf(buf, size, "ZE DOEN MAAR WAT!!! #%ld%s", (long)count, teams == 0 ? " (TEAMS MISLUKT)" : "");
        } else {
            snprintf(buf, size, "ZE DOEN MAAR WAT!!!%s", teams == 0 ? " (TEAMS MISLUKT)" : "");
        }
        return;
    case LINK_PDC_AUTH:
        snprintf(buf, size, "PDC: OPNIEUW INLOGGEN IN DE COMPANION");
        return;
    case LINK_PDC_DENIED:
        snprintf(buf, size, "PDC: GEEN RECHTEN VOOR ZDMW");
        return;
    case LINK_PDC_OFFLINE:
        snprintf(buf, size, "PDC: NIET BEREIKBAAR - NIET VERSTUURD");
        return;
    case LINK_PDC_TLS:
        snprintf(buf, size, "PDC: CERTIFICAAT AFGEWEZEN - NIET VERSTUURD");
        return;
    case LINK_PDC_COOLDOWN:
    case LINK_PDC_BUSY:
        snprintf(buf, size, "PDC: EVEN WACHTEN - NIET VERSTUURD");
        return;
    case LINK_PDC_UNAVAILABLE:
        snprintf(buf, size, "PDC: NIET BESCHIKBAAR - NIET VERSTUURD");
        return;
    case LINK_PDC_TIMEOUT:
        snprintf(buf, size, "PDC: GEEN ANTWOORD - MISSCHIEN TOCH VERSTUURD");
        return;
    case LINK_PDC_UNKNOWN:
    default:
        snprintf(buf, size, "PDC: ONDUIDELIJK ANTWOORD - MISSCHIEN VERSTUURD");
        return;
    }
}
