# Clock Mode (first called Office Mode): stroke font, the faces (Classic desk clock, Mechanical Skeleton) and the
# selection between them, moon art, the dark scene switch, and Buddy's zoom into it and back.
# Hardware independent; host tests build the same sources: firmware/tests/host. The Skeleton's drawing code
# (src/office_skeleton_art.c) runs on the development PC only: the firmware gets its packed result
# (src/office_skeleton_assets.c, made by tools/host-test.ps1 -Assets).
idf_component_register(SRCS "src/office_font.c" "src/office_clock.c" "src/office_skeleton.c"
                            "src/office_skeleton_assets.c" "src/office_faces.c" "src/office_switch.c"
                            "src/office_zoom.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gfx buddy)
