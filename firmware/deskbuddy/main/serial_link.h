// The USB serial link to the Windows companion (protocol: components/link, docs/protocol.md).
//
// Frames share the console UART (the CH343 port) with the log. A task reads lines from it; valid commands go to
// the render task as INPUT_COMMAND events (it owns the character and the scene, and replies once it has acted).
// hello and ping are answered right away. Lines that are not frames are ignored.
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Starts receiving; commands are queued on events. Announces itself with "evt boot".
esp_err_t serial_link_start(QueueHandle_t events);

// Sends one frame: "<DB1 <seq> <body> #crc". seq 0 for events. Safe from any task; never mixes with log lines.
void serial_link_send(uint32_t seq, const char *body);
