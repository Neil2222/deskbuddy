#include "serial_link.h"

#include <stdio.h>
#include <string.h>

#include "buddy_activity.h"
#include "buddy_mood.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "input.h"
#include "link.h"

static const char *TAG = "link";

#define UART_PORT UART_NUM_0 // the console, wired to the CH343
#define RX_BUF 1024
#define TASK_PRIO 4 // below rendering: commands are rare and never urgent
#define TASK_CORE 0
#define QUEUE_WAIT_MS 100
#define CAPS "poke,monster,mode,time,status,msg,mood,act,pdc"
#define STYLES "marquee" // msg.show styles this firmware draws

static QueueHandle_t s_events;

void serial_link_send(uint32_t seq, const char *body)
{
    char line[LINK_REPLY_MAX];
    const size_t n = link_format(line, sizeof(line), seq, body);
    if (n == 0) {
        ESP_LOGE(TAG, "frame too long: %s", body);
        return;
    }
    // One write() of the whole line: the console driver holds its lock for the whole buffer, so log lines from
    // other tasks cannot end up in the middle of a frame.
    fwrite(line, 1, n, stdout);
    fflush(stdout);
}

static void reply_error(uint32_t seq, link_result_t r)
{
    char body[48];
    snprintf(body, sizeof(body), "err %s%s", link_error_code(r), r == LINK_ERR_VERSION ? " proto=1" : "");
    serial_link_send(seq, body);
}

static void handle_line(const char *line, size_t len)
{
    link_command_t cmd;
    const link_result_t r = link_parse_command(line, len, &cmd);
    if (r == LINK_NOT_A_FRAME) {
        return;
    }
    if (r != LINK_OK) {
        ESP_LOGW(TAG, "rejected frame (%s): %.*s", link_error_code(r), (int)len, line);
        reply_error(cmd.seq, r);
        return;
    }
    if (cmd.cmd == LINK_CMD_HELLO) {
        char body[LINK_REPLY_MAX];
        int n = snprintf(body, sizeof(body), "ok proto=%d fw=%s caps=" CAPS " styles=" STYLES " moods=", LINK_PROTO,
                         esp_app_get_description()->version);
        for (int m = 0; m < BUDDY_MOOD_COUNT && n > 0 && (size_t)n < sizeof(body); m++) {
            n += snprintf(body + n, sizeof(body) - (size_t)n, "%s%s", m ? "," : "", buddy_mood_name((buddy_mood_t)m));
        }
        for (int i = 0; i < BUDDY_ACTS && n > 0 && (size_t)n < sizeof(body); i++) {
            n += snprintf(body + n, sizeof(body) - (size_t)n, "%s%s", i ? "," : " acts=", buddy_act_name(i));
        }
        serial_link_send(cmd.seq, body);
        // The render task also hears of it (a new companion connection: forget what the previous one offered);
        // it does not reply again.
        const input_event_t ev = {.type = INPUT_COMMAND, .command = cmd};
        xQueueSend(s_events, &ev, pdMS_TO_TICKS(QUEUE_WAIT_MS));
        return;
    }
    if (cmd.cmd == LINK_CMD_PING) {
        serial_link_send(cmd.seq, "ok");
        return;
    }
    const input_event_t ev = {.type = INPUT_COMMAND, .command = cmd};
    if (xQueueSend(s_events, &ev, pdMS_TO_TICKS(QUEUE_WAIT_MS)) != pdTRUE) {
        serial_link_send(cmd.seq, "err busy");
    }
}

static void link_task(void *arg)
{
    (void)arg;
    char body[64];
    snprintf(body, sizeof(body), "evt boot proto=%d fw=%s", LINK_PROTO, esp_app_get_description()->version);
    serial_link_send(0, body);

    static char line[LINK_LINE_MAX];
    size_t len = 0;
    bool overflow = false; // discard the rest of an over-long line
    for (;;) {
        // uart_read_bytes() waits until it has the full length, so wait for one byte, then take what is buffered.
        uint8_t buf[64];
        int n = uart_read_bytes(UART_PORT, buf, 1, portMAX_DELAY);
        size_t more = 0;
        if (n == 1 && uart_get_buffered_data_len(UART_PORT, &more) == ESP_OK && more > 0) {
            const int got = uart_read_bytes(UART_PORT, buf + 1, more < sizeof(buf) - 1 ? more : sizeof(buf) - 1, 0);
            n += got > 0 ? got : 0;
        }
        for (int i = 0; i < n; i++) {
            const char c = (char)buf[i];
            if (c == '\n') {
                if (!overflow) {
                    handle_line(line, len);
                }
                len = 0;
                overflow = false;
            } else if (!overflow) {
                if (len < sizeof(line) - 1) {
                    line[len++] = c;
                } else {
                    overflow = true;
                }
            }
        }
    }
}

esp_err_t serial_link_start(QueueHandle_t events)
{
    s_events = events;
    // Receive only: the console keeps writing through its own path (the VFS), as before.
    esp_err_t err = uart_driver_install(UART_PORT, RX_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreatePinnedToCore(link_task, "link", 4096, NULL, TASK_PRIO, NULL, TASK_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
