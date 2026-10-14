# DeskBuddy application core: events, frame loop, verification. Shared by the firmware and the Windows simulator;
# the host build (firmware/host) compiles the same sources.
idf_component_register(SRCS "src/deskbuddy_core.c"
                       INCLUDE_DIRS "include"
                       REQUIRES buddy gfx layout)
