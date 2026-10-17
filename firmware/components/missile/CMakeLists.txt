# Buddy Missile Defense: the rules (md.c), what Buddy feels about it (md_buddy.c) and the fullscreen view (md_view.c).
# Hardware independent; host tests build the same sources: firmware/tests/host.
idf_component_register(SRCS "src/md.c" "src/md_buddy.c" "src/md_view.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx buddy
                       PRIV_REQUIRES marquee)
