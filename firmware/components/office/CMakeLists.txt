# Office Mode: stroke font, desk-clock face, moon button, and the dark scene switch.
# Hardware independent; host tests build the same sources: firmware/tests/host.
idf_component_register(SRCS "src/office_font.c" "src/office_clock.c" "src/office_switch.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx)
