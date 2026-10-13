# Double-buffered display output: application-owned PSRAM frame buffers fed to the RGB panel's bounce buffers.
idf_component_register(SRCS "display.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_timer)
