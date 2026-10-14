# Placeholder character: behavior (brain), artwork (art) and pose-to-sprite mapping (view).
# Hardware independent; the host build (firmware/host) compiles the same sources.
idf_component_register(SRCS "src/buddy_art.c" "src/buddy_brain.c" "src/buddy_pose.c" "src/buddy_view.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx)
