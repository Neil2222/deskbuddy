# The touchscreen menu in the control panel: definitions, navigation and touch (menu.c, menu_items.c), drawing
# (menu_view.c, menu_art.c), and what Buddy shows on the stage: his questions (menu_ask.c) and his thoughts
# (menu_thought.c). Hardware independent; host tests build the same sources: firmware/tests/host.
idf_component_register(SRCS "src/menu.c" "src/menu_items.c" "src/menu_view.c" "src/menu_art.c" "src/menu_ask.c"
                            "src/menu_thought.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx buddy
                       PRIV_REQUIRES marquee)
