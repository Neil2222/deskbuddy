# Placeholder character: behavior (brain), artwork (art) and pose-to-sprite mapping (view).
# Hardware independent; host tests build the same sources: firmware/tests/host.
idf_component_register(SRCS "src/buddy_art.c" "src/buddy_brain.c" "src/buddy_view.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx)
