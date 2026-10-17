# esp_lcd_touch comes in publicly through board (board.h exposes the touch handle).
# The application itself is components/app; port.c is the board under it (app_port.h). WHOLE_ARCHIVE: the app
# library, linked after main, needs port.c's functions.
idf_component_register(SRCS "main.c" "port.c" "input.c" "serial_link.c"
                       PRIV_REQUIRES app board bootloader_support display esp_app_format esp_driver_uart esp_mm esp_timer gfx
                       WHOLE_ARCHIVE)
