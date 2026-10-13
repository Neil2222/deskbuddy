# Hardware-independent graphics core. Host tests build the same sources: firmware/tests/host.
idf_component_register(SRCS "src/gfx_anim.c" "src/gfx_dirty.c" "src/gfx_image.c" "src/gfx_repair.c" "src/gfx_scene.c"
                       INCLUDE_DIRS "include")
