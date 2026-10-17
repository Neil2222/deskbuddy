# The DeskBuddy application above the hardware (app.h), the device side of the companion link (app_link.h) and the
# recorded events kept for replay (src/app_events.h). The board firmware (firmware/deskbuddy) and the desktop
# simulator (firmware/simulator) run the same sources; each implements app_port.h. Host tests compile them too:
# firmware/tests/host.
idf_component_register(SRCS "src/app.c" "src/app_link.c" "src/app_events.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx link
                       PRIV_REQUIRES buddy layout log marquee menu office pdc ttt)
