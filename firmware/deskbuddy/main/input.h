// Touch input for the render task: a high-priority task turns the GT911's interrupts into APP_TOUCH_DOWN/MOVE/UP
// events (app.h) on the queue that also carries the companion's commands (serial_link.c).
// Consumers never touch the controller; the touch task never touches the display.
#pragma once

#include "app.h"
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// board_callbacks_t.on_touch_irq (ISR context).
void input_touch_irq(void *ctx);

// Starts the touch task (core 1, above rendering). app_event_t events arrive on *events.
esp_err_t input_start(esp_lcd_touch_handle_t touch, QueueHandle_t *events);
