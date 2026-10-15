// The USB serial link to the Windows companion: the console UART (the CH343 port) under the shared link handling
// (components/app, app_link.h; protocol: components/link, docs/protocol.md).
//
// Frames share the UART with the log. A task reads from it; valid commands go to the render task as APP_COMMAND
// events (it owns the character and the scene, and replies once it has acted). Frames go out with app_link_send().
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Starts receiving; commands are queued on events. Announces itself with "evt boot".
esp_err_t serial_link_start(QueueHandle_t events);
