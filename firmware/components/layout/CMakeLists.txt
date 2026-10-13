# Screen layout (character stage + reserved control-panel area). Hardware independent.
idf_component_register(SRCS "src/layout.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx)
