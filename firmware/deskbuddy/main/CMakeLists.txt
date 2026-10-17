# esp_lcd_touch comes in publicly through board (board.h exposes the touch handle).
# The application itself is components/app; port.c is the board under it (app_port.h). WHOLE_ARCHIVE: the app
# library, linked after main, needs port.c's functions.
idf_component_register(SRCS "main.c" "port.c" "input.c" "serial_link.c"
                       PRIV_REQUIRES app board bootloader_support display esp_app_format esp_driver_uart esp_mm esp_psram
                                     esp_timer gfx spi_flash
                       WHOLE_ARCHIVE)

# info build= (port.c): the Git commit of this build.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(${CMAKE_CURRENT_LIST_DIR}/../../build_commit.cmake)
    deskbuddy_build_commit(DESKBUDDY_BUILD_COMMIT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DESKBUDDY_BUILD_COMMIT="${DESKBUDDY_BUILD_COMMIT}")
endif()
