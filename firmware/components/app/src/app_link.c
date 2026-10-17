#include "app_link.h"

#include <stdio.h>
#include <string.h>

#include "app_events.h"
#include "app_log.h"
#include "app_port.h"
#include "buddy_activity.h"
#include "buddy_mood.h"
#include "office_faces.h"

static const char *TAG = "link";

void app_link_send(uint32_t seq, const char *body)
{
    char line[LINK_REPLY_MAX];
    const size_t n = link_format(line, sizeof(line), seq, body);
    if (n == 0) {
        APP_LOGE(TAG, "frame too long: %s", body);
        return;
    }
    app_port_send(line, n);
}

void app_link_boot(void)
{
    app_events_identity();
    char body[128];
    snprintf(body, sizeof(body), "evt boot proto=%d fw=%s dev=%s boot=%s", LINK_PROTO, app_port_version(),
             app_events_device(), app_events_boot());
    app_link_send(0, body);
}

static void reply_error(uint32_t seq, link_result_t r)
{
    char body[48];
    snprintf(body, sizeof(body), "err %s%s", link_error_code(r), r == LINK_ERR_VERSION ? " proto=1" : "");
    app_link_send(seq, body);
}

static void handle_line(const char *line, size_t len, app_link_deliver_fn deliver, void *ctx)
{
    app_event_t ev = {.type = APP_COMMAND};
    const link_result_t r = link_parse_command(line, len, &ev.command);
    if (r == LINK_NOT_A_FRAME) {
        return;
    }
    if (r != LINK_OK) {
        APP_LOGW(TAG, "rejected frame (%s): %.*s", link_error_code(r), (int)len, line);
        reply_error(ev.command.seq, r);
        return;
    }
    if (ev.command.cmd == LINK_CMD_HELLO) {
        char body[LINK_REPLY_MAX];
        int n = snprintf(body, sizeof(body), "ok proto=%d fw=%s caps=" APP_LINK_CAPS " styles=" APP_LINK_STYLES " moods=",
                         LINK_PROTO, app_port_version());
        for (int m = 0; m < BUDDY_MOOD_COUNT && n > 0 && (size_t)n < sizeof(body); m++) {
            n += snprintf(body + n, sizeof(body) - (size_t)n, "%s%s", m ? "," : "", buddy_mood_name((buddy_mood_t)m));
        }
        for (int i = 0; i < BUDDY_ACTS && n > 0 && (size_t)n < sizeof(body); i++) {
            n += snprintf(body + n, sizeof(body) - (size_t)n, "%s%s", i ? "," : " acts=", buddy_act_name(i));
        }
        for (int i = 0; i < OFFICE_FACE_COUNT && n > 0 && (size_t)n < sizeof(body); i++) {
            n += snprintf(body + n, sizeof(body) - (size_t)n, "%s%s", i ? "," : " faces=",
                          office_face_name((office_face_t)i));
        }
        if (n > 0 && (size_t)n < sizeof(body)) {
            snprintf(body + n, sizeof(body) - (size_t)n, " dev=%s boot=%s", app_events_device(), app_events_boot());
        }
        app_link_send(ev.command.seq, body);
        // The application also hears of it (a new companion connection: forget what the previous one offered); it
        // does not reply again.
        deliver(&ev, ctx);
        return;
    }
    if (ev.command.cmd == LINK_CMD_PING) {
        app_link_send(ev.command.seq, "ok");
        return;
    }
    if (ev.command.cmd == LINK_CMD_INFO) {
        // Fixed since start-up (app_port_info): answered here, the application thread is not involved.
        char body[LINK_REPLY_MAX];
        snprintf(body, sizeof(body), "ok fw=%s %s", app_port_version(), app_port_info());
        app_link_send(ev.command.seq, body);
        return;
    }
    if (!deliver(&ev, ctx)) {
        app_link_send(ev.command.seq, "err busy");
    }
}

void app_link_rx_reset(app_link_rx_t *rx)
{
    rx->len = 0;
    rx->overflow = false;
}

void app_link_feed(app_link_rx_t *rx, const uint8_t *data, size_t n, app_link_deliver_fn deliver, void *ctx)
{
    for (size_t i = 0; i < n; i++) {
        const char c = (char)data[i];
        if (c == '\n') {
            if (!rx->overflow) {
                handle_line(rx->line, rx->len, deliver, ctx);
            }
            app_link_rx_reset(rx);
        } else if (!rx->overflow) {
            if (rx->len < sizeof(rx->line) - 1) {
                rx->line[rx->len++] = c;
            } else {
                rx->overflow = true;
            }
        }
    }
}
