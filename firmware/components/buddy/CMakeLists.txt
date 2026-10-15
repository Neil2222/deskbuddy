# Placeholder character: behavior (brain, moods, the Monster sequence and other activities), artwork (art) and
# pose-to-sprite mapping (view). Hardware independent; host tests build the same sources: firmware/tests/host.
idf_component_register(SRCS "src/buddy_activity.c" "src/buddy_art.c" "src/buddy_brain.c" "src/buddy_monster.c"
                            "src/buddy_mood.c" "src/buddy_view.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx)
