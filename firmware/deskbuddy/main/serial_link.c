#include "serial_link.h"

#include "app_link.h"
#include "driver/uart.h"
#include "freertos/task.h"

#define UART_PORT UART_NUM_0 // the console, wired to the CH343
#define RX_BUF 1024
#define TASK_PRIO 4 // below rendering: commands are rare and never urgent
#define TASK_CORE 0
#define QUEUE_WAIT_MS 100

static QueueHandle_t s_events;

static bool deliver(const app_event_t *ev, void *ctx)
{
    (void)ctx;
    return xQueueSend(s_events, ev, pdMS_TO_TICKS(QUEUE_WAIT_MS)) == pdTRUE;
}

static void link_task(void *arg)
{
    (void)arg;
    app_link_boot();

    static app_link_rx_t rx;
    app_link_rx_reset(&rx);
    for (;;) {
        // uart_read_bytes() waits until it has the full length, so wait for one byte, then take what is buffered.
        uint8_t buf[64];
        int n = uart_read_bytes(UART_PORT, buf, 1, portMAX_DELAY);
        size_t more = 0;
        if (n == 1 && uart_get_buffered_data_len(UART_PORT, &more) == ESP_OK && more > 0) {
            const int got = uart_read_bytes(UART_PORT, buf + 1, more < sizeof(buf) - 1 ? more : sizeof(buf) - 1, 0);
            n += got > 0 ? got : 0;
        }
        if (n > 0) {
            app_link_feed(&rx, buf, (size_t)n, deliver, NULL);
        }
    }
}

esp_err_t serial_link_start(QueueHandle_t events)
{
    s_events = events;
    // Receive only: the console keeps writing through its own path (the VFS), as before; see app_port_send().
    esp_err_t err = uart_driver_install(UART_PORT, RX_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreatePinnedToCore(link_task, "link", 4096, NULL, TASK_PRIO, NULL, TASK_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
