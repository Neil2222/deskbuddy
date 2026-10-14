// Input events for the render task: touches from the GT911 (a high-priority task turns its interrupts into
// down/move/up events) and commands from the Windows companion (serial_link.c), on one queue.
// Consumers never touch the controller; the touch task never touches the display.
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "link.h"

typedef enum {
    INPUT_DOWN,
    INPUT_MOVE,
    INPUT_UP,
    INPUT_COMMAND, // from the companion: see command
} input_type_t;

typedef struct {
    input_type_t type;
    int16_t x, y;   // screen pixels (first touch point)
    int64_t irq_us; // esp_timer time of the interrupt that produced this event
    link_command_t command; // INPUT_COMMAND only
} input_event_t;

// board_callbacks_t.on_touch_irq (ISR context).
void input_touch_irq(void *ctx);

// Starts the touch task (core 1, above rendering). Events arrive on *events.
esp_err_t input_start(esp_lcd_touch_handle_t touch, QueueHandle_t *events);
