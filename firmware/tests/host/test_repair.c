// Double-buffer repair: two buffers alternate exactly like the device's frame buffers, and every presented buffer
// must equal a full redraw of the scene.
#include <string.h>

#include "gfx_repair.h"
#include "gfx_scene.h"
#include "test.h"
#include "test_util.h"

enum { RW = 96, RH = 64 };

static gfx_color_t s_strip[RW * 2];
static gfx_color_t s_buf[2][RW * RH];
static gfx_color_t s_colors[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
static const gfx_palette_t k_pal = {s_colors, 10};

typedef struct {
    gfx_scene_t scene;
    gfx_repair_t repair;
    gfx_dirty_t changes, redraw;
    gfx_surface_t surf[2];
    int back;
} fixture_t;

static void fixture_init(fixture_t *f, int x_align)
{
    gfx_scene_init(&f->scene, RW, RH, test_background, NULL, s_strip, RW * 2, x_align);
    gfx_repair_init(&f->repair, RW, RH, x_align);
    gfx_dirty_init(&f->changes, RW, RH, x_align);
    gfx_dirty_init(&f->redraw, RW, RH, x_align);
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < RW * RH; i++) {
            s_buf[b][i] = (gfx_color_t)(0x4000 + b * 1000 + i % 977); // power-on garbage, differs per buffer
        }
        f->surf[b] = (gfx_surface_t){s_buf[b], RW, RH, RW};
    }
    f->back = 1; // buffer 0 is on screen first
    gfx_scene_invalidate(&f->scene, (gfx_rect_t){0, 0, RW, RH});
}

// One frame: collect changes, repair the back buffer, "present" it. Returns -1 if nothing changed (no present),
// otherwise the number of pixels that differ from a full redraw.
static int fixture_frame(fixture_t *f)
{
    gfx_dirty_clear(&f->changes);
    gfx_scene_collect(&f->scene, &f->changes);
    if (f->changes.count == 0) {
        return -1;
    }
    gfx_repair_next(&f->repair, &f->changes, &f->redraw);
    const gfx_surface_t *back = &f->surf[f->back];
    gfx_scene_render(&f->scene, &f->redraw, back);
    int bad = 0;
    for (int y = 0; y < RH; y++) {
        for (int x = 0; x < RW; x++) {
            bad += back->px[y * RW + x] != test_reference_pixel(&f->scene, x, y);
        }
    }
    f->back ^= 1;
    return bad;
}

static gfx_sprite_t sprite(const gfx_image_t *img, int x, int y, int scale)
{
    gfx_sprite_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.image = img;
    sp.palette = &k_pal;
    sp.x = x;
    sp.y = y;
    sp.scale = (uint8_t)scale;
    return sp;
}

void test_repair_alternating_buffers_match_full_redraw(void)
{
    static fixture_t f;
    static uint8_t pixels[8][36];
    static gfx_image_t images[8];
    static gfx_sprite_t sprites[6];
    uint32_t rng = 4242;
    fixture_init(&f, 8);
    for (int i = 0; i < 8; i++) {
        images[i].w = (uint16_t)test_rand_range(&rng, 2, 6);
        images[i].h = (uint16_t)test_rand_range(&rng, 2, 6);
        for (int p = 0; p < images[i].w * images[i].h; p++) {
            pixels[i][p] = (uint8_t)test_rand_range(&rng, 0, 9);
        }
        images[i].px = pixels[i];
    }
    for (int i = 0; i < 6; i++) {
        sprites[i] = sprite(&images[i], test_rand_range(&rng, -5, RW), test_rand_range(&rng, -5, RH),
                            test_rand_range(&rng, 1, 3));
        CHECK(gfx_scene_add(&f.scene, &sprites[i], test_rand_range(&rng, 0, 3)));
    }

    int presents = 0;
    for (int frame = 0; frame < 500; frame++) {
        if (frame > 0 && test_rand_range(&rng, 0, 4) != 0) { // some frames change nothing
            for (int i = 0; i < 6; i++) {
                gfx_sprite_t *sp = &sprites[i];
                if (test_rand_range(&rng, 0, 2) == 0) {
                    sp->x += test_rand_range(&rng, -4, 4);
                    sp->y += test_rand_range(&rng, -4, 4);
                }
                if (test_rand_range(&rng, 0, 9) == 0) {
                    sp->flags ^= GFX_SPRITE_HIDDEN;
                }
                if (test_rand_range(&rng, 0, 9) == 0) {
                    sp->flags ^= GFX_SPRITE_FLIP_X;
                }
                if (test_rand_range(&rng, 0, 9) == 0) {
                    sp->image = &images[test_rand_range(&rng, 0, 7)];
                }
                if (test_rand_range(&rng, 0, 19) == 0) {
                    sp->scale = (uint8_t)test_rand_range(&rng, 1, 3);
                }
                // Keep sprites near the screen so most frames change something visible.
                if (sp->x < -20 || sp->x > RW + 5) {
                    sp->x = RW / 2;
                }
                if (sp->y < -20 || sp->y > RH + 5) {
                    sp->y = RH / 2;
                }
            }
        }
        const int bad = fixture_frame(&f);
        if (bad > 0) {
            printf("    frame %d: %d stale pixels in the presented buffer\n", frame, bad);
        }
        CHECK(bad <= 0);
        presents += bad == 0;
    }
    CHECK(presents > 300); // the scenario really exercised alternation
}

void test_repair_moving_sprite_restores_background(void)
{
    static fixture_t f;
    static const uint8_t px[9] = {1, 1, 1, 1, 2, 1, 1, 1, 1};
    static const gfx_image_t img = {3, 3, px};
    fixture_init(&f, 32);
    gfx_sprite_t sp = sprite(&img, 0, 20, 2);
    CHECK(gfx_scene_add(&f.scene, &sp, 0));
    for (int step = 0; step < 17; step++) { // ends at x = 80, still on screen
        CHECK_EQ(fixture_frame(&f), 0);
        sp.x += 5;
    }
    // Both buffers have now been presented many times; the start position shows background in both.
    for (int b = 0; b < 2; b++) {
        CHECK_EQ(s_buf[b][20 * RW + 0], test_background_at(0, 20));
        CHECK_EQ(s_buf[b][25 * RW + 5], test_background_at(5, 25));
    }
}

void test_repair_redraws_only_recent_changes(void)
{
    static fixture_t f;
    static const uint8_t px[16] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    static const gfx_image_t img = {4, 4, px};
    fixture_init(&f, 1);
    gfx_sprite_t sp = sprite(&img, 10, 10, 1);
    CHECK(gfx_scene_add(&f.scene, &sp, 0));
    for (int frame = 0; frame < 30; frame++) {
        CHECK_EQ(fixture_frame(&f), 0);
        if (frame >= 2) {
            // This move (old + new: 5x4) plus the previous move, which the back buffer has not seen: 6x4.
            CHECK_EQ(gfx_dirty_area(&f.redraw), 24);
        }
        sp.x += 1;
    }
}
