# esp_lcd_touch comes in publicly through board (board.h exposes the touch handle).
idf_component_register(SRCS "main.c" "input.c"
                       PRIV_REQUIRES board buddy display esp_timer gfx layout)
