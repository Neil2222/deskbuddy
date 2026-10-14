# Retro LED marquee for messages from the companion. Hardware independent; host tests build the same sources:
# firmware/tests/host.
idf_component_register(SRCS "src/marquee.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx)
