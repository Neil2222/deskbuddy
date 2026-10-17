// DeskBuddy on the board: the application (components/app, the same code the simulator runs) on the ESP32-S3.
//
// Tasks:  touch  (core 1, prio 10)  GT911 interrupt -> input events
//         render (core 1, prio 5)   events + time -> app_event / app_frame: character pose or clock -> dirty
//                                   rectangles -> back buffer -> present; runs companion commands and replies
//         link   (core 0, prio 4)   console UART -> companion commands -> input events
// The LCD scanout interrupts run on core 0 (board_init is called from app_main there).
// The hardware under the application: port.c (app_port.h).
#include <stdlib.h>

#include "app.h"
#include "board.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input.h"
#include "sdkconfig.h"
#include "serial_link.h"

static const char *TAG = "deskbuddy";

#define RENDER_PRIO 5
#define RENDER_CORE 1
#define RENDER_STACK 6144
#define STACK_CHECK_US (10 * 1000000LL)

#ifdef CONFIG_DESKBUDDY_PANEL_LEFT
#define PANEL_LEFT true
#else
#define PANEL_LEFT false
#endif

#ifdef CONFIG_DESKBUDDY_GFX_VERIFY
#define GFX_VERIFY true
#else
#define GFX_VERIFY false
#endif

static board_t s_board;

static void render_task(void *arg)
{
    QueueHandle_t events = arg;
    const app_config_t config = {
        .width = BOARD_LCD_H_RES,
        .height = BOARD_LCD_V_RES,
        .stage_percent = CONFIG_DESKBUDDY_STAGE_PERCENT,
        .panel_left = PANEL_LEFT,
        .perf_log_s = CONFIG_DESKBUDDY_PERF_LOG_S,
        .demo_poke_s = CONFIG_DESKBUDDY_DEMO_POKE_S,
        .demo_monster_s = CONFIG_DESKBUDDY_DEMO_MONSTER_S,
        .demo_office_s = CONFIG_DESKBUDDY_DEMO_OFFICE_S,
        .demo_activity_s = CONFIG_DESKBUDDY_DEMO_ACTIVITY_S,
        .gfx_verify = GFX_VERIFY,
    };
    if (!app_init(&config)) {
        abort();
    }
    // The render task's stack: its deepest use so far, logged whenever it reaches a new depth (checked every 10 s).
    UBaseType_t stack_low = UINT32_MAX;
    int64_t next_stack_check = 0;
    uint32_t wait_ms = 0;
    for (;;) {
        app_event_t ev;
        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
            do {
                app_event(&ev);
            } while (xQueueReceive(events, &ev, 0) == pdTRUE);
        }
        wait_ms = app_frame();
        const int64_t now = esp_timer_get_time();
        if (now >= next_stack_check) {
            next_stack_check = now + STACK_CHECK_US;
            const UBaseType_t unused = uxTaskGetStackHighWaterMark(NULL);
            if (unused < stack_low) {
                stack_low = unused;
                ESP_LOGI(TAG, "render task stack: %u of %d B never used; free internal %u B, PSRAM %u B",
                         (unsigned)unused, RENDER_STACK, heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                         heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            }
        }
    }
}

void app_main(void)
{
    const int64_t t_boot = esp_timer_get_time();
    ESP_ERROR_CHECK(display_init(BOARD_LCD_H_RES, BOARD_LCD_V_RES)); // before board_init: the scanout reads the
                                                                      // buffers from its first frame
    const board_callbacks_t cbs = {.on_touch_irq = input_touch_irq, .on_bounce_fill = display_bounce_fill};
    ESP_ERROR_CHECK(board_init(&cbs, &s_board));

    QueueHandle_t events;
    ESP_ERROR_CHECK(input_start(s_board.touch, &events));
    ESP_ERROR_CHECK(serial_link_start(events));
    if (xTaskCreatePinnedToCore(render_task, "render", RENDER_STACK, events, RENDER_PRIO, NULL, RENDER_CORE) !=
        pdPASS) {
        ESP_LOGE(TAG, "cannot start the render task");
        abort();
    }
    ESP_LOGI(TAG, "started in %.0f ms; free internal %u B, PSRAM %u B", (esp_timer_get_time() - t_boot) / 1000.0,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
