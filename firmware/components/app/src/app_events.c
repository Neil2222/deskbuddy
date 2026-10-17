#include "app_events.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "app_link.h"
#include "app_log.h"
#include "app_port.h"

static const char *TAG = "events";

#define BODY_MAX 120

typedef struct {
    uint32_t seq; // 0: empty
    char body[BODY_MAX];
} kept_t;

static char s_device[24];
static char s_boot[17];
static uint64_t s_boot_id;
static kept_t *s_kept; // APP_EVENTS_KEPT entries, by seq % APP_EVENTS_KEPT
static uint32_t s_last; // seq of the last recorded event (0: none yet)

static struct {
    bool active;
    uint32_t reply_seq;
    uint32_t next; // seq to send next
    uint32_t last; // the last one to send (s_last when it started)
    uint32_t lost;
} s_replay;

void app_events_identity(void)
{
    snprintf(s_device, sizeof(s_device), "%s", app_port_device_id());
    uint8_t b[8];
    app_port_random_fill(b, sizeof(b));
    s_boot_id = 0;
    for (size_t i = 0; i < sizeof(b); i++) {
        s_boot_id = s_boot_id << 8 | b[i];
    }
    snprintf(s_boot, sizeof(s_boot), "%016" PRIx64, s_boot_id);
}

const char *app_events_device(void)
{
    return s_device;
}

const char *app_events_boot(void)
{
    return s_boot;
}

void app_events_init(void)
{
    s_kept = app_port_alloc(sizeof(kept_t) * APP_EVENTS_KEPT, APP_MEM_LARGE);
    if (s_kept) {
        memset(s_kept, 0, sizeof(kept_t) * APP_EVENTS_KEPT);
    } else {
        APP_LOGW(TAG, "no memory to keep events: a companion that was away cannot get them back");
    }
}

void app_events_send(const char *body, bool clock_set, int64_t utc_ms, uint32_t uptime_ms)
{
    const uint32_t seq = ++s_last;
    char line[BODY_MAX];
    if (clock_set) {
        snprintf(line, sizeof(line), "%s seq=%" PRIu32 " t=%" PRId64, body, seq, utc_ms);
    } else {
        snprintf(line, sizeof(line), "%s seq=%" PRIu32 " up=%" PRIu32, body, seq, uptime_ms);
    }
    if (s_kept) {
        kept_t *k = &s_kept[seq % APP_EVENTS_KEPT];
        k->seq = seq;
        memcpy(k->body, line, sizeof(line));
    }
    app_link_send(0, line);
}

// The oldest seq still kept (s_last + 1 when nothing is).
static uint32_t oldest_kept(void)
{
    if (!s_kept || s_last == 0) {
        return s_last + 1;
    }
    return s_last > APP_EVENTS_KEPT ? s_last - APP_EVENTS_KEPT + 1 : 1;
}

bool app_events_replay_start(uint32_t reply_seq, bool has_boot, uint64_t boot, uint32_t since)
{
    if (s_replay.active) {
        return false;
    }
    if (!has_boot || boot != s_boot_id || since > s_last) {
        since = 0; // another boot (or one we never had): all of this boot
    }
    const uint32_t oldest = oldest_kept();
    s_replay.active = true;
    s_replay.reply_seq = reply_seq;
    s_replay.next = since + 1 > oldest ? since + 1 : oldest;
    s_replay.last = s_last;
    s_replay.lost = oldest > since + 1 ? oldest - since - 1 : 0;
    return true;
}

bool app_events_replay_step(void)
{
    if (!s_replay.active) {
        return false;
    }
    for (int i = 0; i < APP_EVENTS_REPLAY_PER_FRAME && s_replay.next <= s_replay.last; i++, s_replay.next++) {
        const kept_t *k = &s_kept[s_replay.next % APP_EVENTS_KEPT];
        if (k->seq != s_replay.next) {
            s_replay.lost++; // overwritten meanwhile by newer events (which went out live)
            continue;
        }
        char line[BODY_MAX + 10];
        snprintf(line, sizeof(line), "%s replay=1", k->body);
        app_link_send(0, line);
    }
    if (s_replay.next <= s_replay.last) {
        return true;
    }
    char body[96];
    snprintf(body, sizeof(body), "ok boot=%s last=%" PRIu32 " lost=%" PRIu32, s_boot, s_replay.last, s_replay.lost);
    app_link_send(s_replay.reply_seq, body);
    s_replay.active = false;
    return false;
}
