#include "sysinfo.h"

#include <string.h>

static const link_sys_t k_none = {-1, -1, -1, -1};

static bool reached(uint32_t now, uint32_t at)
{
    return (int32_t)(now - at) >= 0;
}

void sysinfo_init(sysinfo_t *s)
{
    memset(s, 0, sizeof(*s));
    s->sys = k_none;
}

void sysinfo_reset(sysinfo_t *s)
{
    s->on = false;
}

void sysinfo_set(sysinfo_t *s, bool on, const link_sys_t *sys, uint32_t now)
{
    s->on = on;
    s->lease_end = now + SYSINFO_LEASE_MS;
    s->sys = on ? *sys : k_none;
}

bool sysinfo_has_stats(const link_sys_t *sys)
{
    return sys->cpu >= 0 || sys->ram >= 0 || sys->disk >= 0;
}

bool sysinfo_available(const sysinfo_t *s, uint32_t now)
{
    return s->on && !reached(now, s->lease_end) && sysinfo_has_stats(&s->sys);
}

uint32_t sysinfo_next(const sysinfo_t *s, uint32_t now)
{
    return s->on && !reached(now, s->lease_end) ? s->lease_end - now : UINT32_MAX;
}
