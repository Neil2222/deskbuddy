// Renders the placeholder character in its key poses to PNG files, using the firmware's own layout, artwork and
// compositor. A first step towards a desktop simulator: lets the art be reviewed without the device.
//
//   buddy_snapshot [output_dir]
#include <stdio.h>

#include "buddy_view.h"
#include "layout.h"
#include "png.h"

enum { W = 1024, H = 600, ALIGN = 32, STAGE_PERCENT = 65 }; // firmware defaults (Kconfig)

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    static gfx_color_t px[W * H];
    static gfx_color_t strip[16 * 1024];
    static gfx_scene_t scene;
    static buddy_view_t view;
    layout_t layout = layout_compute(W, H, STAGE_PERCENT, false, ALIGN);
    if (!gfx_scene_init(&scene, W, H, layout_background, &layout, strip, 16 * 1024, ALIGN) ||
        !buddy_view_init(&view, &scene, layout.stage, 10, 0)) {
        fprintf(stderr, "scene setup failed\n");
        return 1;
    }
    const struct {
        const char *name;
        buddy_pose_t pose;
    } shots[] = {
        {"idle", {BUDDY_STATE_IDLE, BUDDY_EYES_OPEN, BUDDY_MOUTH_NEUTRAL, BUDDY_ANTENNA_CALM, 0}},
        {"blink", {BUDDY_STATE_IDLE, BUDDY_EYES_CLOSED, BUDDY_MOUTH_NEUTRAL, BUDDY_ANTENNA_CALM, 1}},
        {"surprised", {BUDDY_STATE_SURPRISED, BUDDY_EYES_WIDE, BUDDY_MOUTH_OPEN, BUDDY_ANTENNA_EXCITED, 3}},
        {"happy", {BUDDY_STATE_HAPPY, BUDDY_EYES_HAPPY, BUDDY_MOUTH_SMILE, BUDDY_ANTENNA_EXCITED, 4}},
    };
    const gfx_surface_t dst = {px, W, H, W};
    gfx_dirty_t all;
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    for (unsigned i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        buddy_view_apply(&view, &shots[i].pose, 0);
        gfx_scene_render(&scene, &all, &dst);
        char path[512];
        snprintf(path, sizeof(path), "%s/buddy_%s.png", dir, shots[i].name);
        if (!png_write_rgb565(path, &dst)) {
            fprintf(stderr, "cannot write %s\n", path);
            return 1;
        }
        printf("%s\n", path);
    }
    return 0;
}
