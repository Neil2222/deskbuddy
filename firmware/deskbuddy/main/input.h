// Touch input: a high-priority task turns GT911 interrupts into core touch events on a queue.
// Consumers never touch the controller; the touch task never touches the display.
#pragma once

#include <stdint.h>

#include "core_event.h"
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    core_event_t ev; // what the shared core sees (time_ms on the esp_timer millisecond clock)
    int64_t irq_us;  // esp_timer time of the interrupt, for touch-latency measurement
} input_event_t;

// board_callbacks_t.on_touch_irq (ISR context).
void input_touch_irq(void *ctx);

// Starts the touch task (core 1, above rendering). Events arrive on *events.
esp_err_t input_start(esp_lcd_touch_handle_t touch, QueueHandle_t *events);
