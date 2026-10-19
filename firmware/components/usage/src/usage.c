#include "usage.h"

#include <stdio.h>
#include <string.h>

static bool reached(uint32_t now, uint32_t at)
{
    return (int32_t)(now - at) >= 0;
}

void usage_init(usage_t *u)
{
    memset(u, 0, sizeof(*u));
}

void usage_reset(usage_t *u)
{
    u->on = false;
}

void usage_set(usage_t *u, bool on, const link_usage_t *win, uint32_t now)
{
    u->on = on;
    u->lease_end = now + USAGE_LEASE_MS;
    for (int w = 0; w < LINK_USAGE_WINDOWS; w++) {
        u->win[w] = on ? win[w] : (link_usage_t){0};
    }
}

bool usage_available(const usage_t *u, uint32_t now)
{
    return u->on && !reached(now, u->lease_end) && (u->win[LINK_USAGE_FIVE].has || u->win[LINK_USAGE_WEEK].has);
}

uint32_t usage_next(const usage_t *u, uint32_t now)
{
    return u->on && !reached(now, u->lease_end) ? u->lease_end - now : UINT32_MAX;
}

#define MINUTE_MS 60000

uint32_t usage_countdown(char *buf, size_t size, int64_t now_utc_ms, int64_t end_utc_s)
{
    const int64_t left = end_utc_s * 1000 - now_utc_ms;
    if (left <= 0) {
        snprintf(buf, size, "nu");
        return UINT32_MAX;
    }
    const int64_t m = (left + MINUTE_MS - 1) / MINUTE_MS; // rounded up: "1m" until the moment itself
    if (m >= 24 * 60) {
        snprintf(buf, size, "%dd %du", (int)(m / (24 * 60)), (int)(m % (24 * 60) / 60));
    } else if (m >= 60) {
        snprintf(buf, size, "%d:%02d", (int)(m / 60), (int)(m % 60));
    } else {
        snprintf(buf, size, "%dm", (int)m);
    }
    return (uint32_t)(left - (m - 1) * MINUTE_MS); // until one minute less is left
}

usage_level_t usage_level(int pct)
{
    return pct < 50 ? USAGE_GREEN : pct < 75 ? USAGE_YELLOW : pct < 90 ? USAGE_ORANGE : USAGE_RED;
}
