#include "input.h"

#include "board.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

static const char *TAG = "input";

#define QUEUE_LEN 16
#define RELEASE_POLL_MS 100 // while touched, re-read even without an interrupt so a missed release cannot stick
#define ERROR_RETRY_MS 10   // after a failed read, re-read until one succeeds (see touch_task)
#define TASK_PRIO 10
#define TASK_CORE 1

static esp_lcd_touch_handle_t s_touch;
static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static volatile int64_t s_irq_us;

IRAM_ATTR void input_touch_irq(void *ctx)
{
    (void)ctx;
    s_irq_us = esp_timer_get_time();
    TaskHandle_t task = s_task;
    if (task) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(task, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

// Moves may be dropped when the consumer lags (newer ones follow); down/up must arrive, so they evict the oldest.
static void push(const app_event_t *ev)
{
    if (xQueueSend(s_queue, ev, 0) == pdTRUE || ev->type == APP_TOUCH_MOVE) {
        return;
    }
    app_event_t oldest;
    xQueueReceive(s_queue, &oldest, 0);
    xQueueSend(s_queue, ev, 0);
}

static void touch_task(void *arg)
{
    (void)arg;
    bool down = false;
    int failures = 0;
    app_event_t last = {0};
    for (;;) {
        const TickType_t wait = failures ? pdMS_TO_TICKS(ERROR_RETRY_MS)
                                : down   ? pdMS_TO_TICKS(RELEASE_POLL_MS)
                                         : portMAX_DELAY;
        const bool irq = ulTaskNotifyTake(pdTRUE, wait) > 0;
        const int64_t t = irq ? s_irq_us : esp_timer_get_time();
        // A failed read must not end touch input. The GT911 raises its interrupt again only after a successful read
        // has cleared its status register, so keep retrying. And the reads can keep failing until the IO expander
        // on the same bus is addressed again (seen after backlight writes), so address it before each retry.
        if (esp_lcd_touch_read_data(s_touch) != ESP_OK) {
            failures++;
            board_i2c_recover();
            continue;
        }
        if (failures) {
            ESP_LOGW(TAG, "touch read recovered after %d failed attempts", failures);
            failures = 0;
        }
        esp_lcd_touch_point_data_t pt[1];
        uint8_t count = 0;
        esp_lcd_touch_get_data(s_touch, pt, &count, 1);

        app_event_t ev = {.at_us = t};
        if (count > 0) {
            ev.x = (int16_t)pt[0].x;
            ev.y = (int16_t)pt[0].y;
            ev.type = down ? APP_TOUCH_MOVE : APP_TOUCH_DOWN;
            if (ev.type == APP_TOUCH_MOVE && ev.x == last.x && ev.y == last.y) {
                continue;
            }
            down = true;
        } else if (down) {
            ev = last;
            ev.type = APP_TOUCH_UP;
            ev.at_us = t;
            down = false;
        } else {
            continue;
        }
        push(&ev);
        last = ev;
    }
}

esp_err_t input_start(esp_lcd_touch_handle_t touch, QueueHandle_t *events)
{
    s_touch = touch;
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(app_event_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(touch_task, "touch", 4096, NULL, TASK_PRIO, &s_task, TASK_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    *events = s_queue;
    ESP_LOGI(TAG, "touch task on core %d, priority %d", TASK_CORE, TASK_PRIO);
    return ESP_OK;
}
