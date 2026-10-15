// Logging for code that runs on the board and in the simulator: ESP-IDF's log on the board, app_port_log elsewhere.
#pragma once

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define APP_LOGE(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
#define APP_LOGW(tag, ...) ESP_LOGW(tag, __VA_ARGS__)
#define APP_LOGI(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#else
#include "app_port.h"
#define APP_LOGE(tag, ...) app_port_log('E', tag, __VA_ARGS__)
#define APP_LOGW(tag, ...) app_port_log('W', tag, __VA_ARGS__)
#define APP_LOGI(tag, ...) app_port_log('I', tag, __VA_ARGS__)
#endif
