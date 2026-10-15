# Office Mode: stroke font, desk-clock face, moon art, the dark scene switch, and Buddy's zoom into it and back.
# Hardware independent; host tests build the same sources: firmware/tests/host.
idf_component_register(SRCS "src/office_font.c" "src/office_clock.c" "src/office_switch.c" "src/office_zoom.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx buddy)
