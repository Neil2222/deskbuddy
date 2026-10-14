# esp_lcd_touch comes in publicly through board (board.h exposes the touch handle).
idf_component_register(SRCS "main.c" "input.c" "serial_link.c"
                       PRIV_REQUIRES board buddy display esp_app_format esp_driver_uart esp_timer gfx layout link
                                     office)
