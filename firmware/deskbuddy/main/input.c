#include "input.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

static const char *TAG = "input";

#define QUEUE_LEN 16
#define RELEASE_POLL_MS 100 // while touched, re-read even without an interrupt so a missed release cannot stick
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
static void push(const input_event_t *ev)
{
    if (xQueueSend(s_queue, ev, 0) == pdTRUE || ev->ev.type == CORE_EVENT_TOUCH_MOVE) {
        return;
    }
    input_event_t oldest;
    xQueueReceive(s_queue, &oldest, 0);
    xQueueSend(s_queue, ev, 0);
}

static void touch_task(void *arg)
{
    (void)arg;
    bool down = false;
    input_event_t last = {0};
    for (;;) {
        const bool irq = ulTaskNotifyTake(pdTRUE, down ? pdMS_TO_TICKS(RELEASE_POLL_MS) : portMAX_DELAY) > 0;
        const int64_t t = irq ? s_irq_us : esp_timer_get_time();
        if (esp_lcd_touch_read_data(s_touch) != ESP_OK) {
            continue;
        }
        esp_lcd_touch_point_data_t pt[1];
        uint8_t count = 0;
        esp_lcd_touch_get_data(s_touch, pt, &count, 1);

        input_event_t ev = {.irq_us = t};
        ev.ev.time_ms = (uint32_t)(t / 1000);
        if (count > 0) {
            ev.ev.touch.x = (int16_t)pt[0].x;
            ev.ev.touch.y = (int16_t)pt[0].y;
            ev.ev.type = down ? CORE_EVENT_TOUCH_MOVE : CORE_EVENT_TOUCH_DOWN;
            if (ev.ev.type == CORE_EVENT_TOUCH_MOVE && ev.ev.touch.x == last.ev.touch.x &&
                ev.ev.touch.y == last.ev.touch.y) {
                continue;
            }
            down = true;
        } else if (down) {
            ev.ev.type = CORE_EVENT_TOUCH_UP;
            ev.ev.touch = last.ev.touch; // released where it was last seen
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
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(input_event_t));
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
