#include <string.h>

#include "gfx_scene.h"
#include "test.h"
#include "test_util.h"

enum { SW = 64, SH = 48, POISON = 0xDEAD }; // POISON: neither a background nor a palette color

// Three full rows per strip: wide rectangles are composed in several strips.
static gfx_color_t s_strip[SW * 3];
static gfx_color_t s_px[SW * SH];
static gfx_color_t s_colors[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
static const gfx_palette_t k_pal = {s_colors, 10};

static const uint8_t k_px3x2[] = {1, 0, 2, 3, 4, 0};
static const gfx_image_t k_img3x2 = {3, 2, k_px3x2};
static const uint8_t k_px3x2b[] = {5, 5, 5, 0, 0, 0};
static const gfx_image_t k_img3x2b = {3, 2, k_px3x2b};
static const uint8_t k_px2x2[] = {1, 2, 3, 4};
static const gfx_image_t k_img2x2 = {2, 2, k_px2x2};
static const uint8_t k_px3x1[] = {1, 2, 3};
static const gfx_image_t k_img3x1 = {3, 1, k_px3x1};
static const uint8_t k_solid5[16] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
static const uint8_t k_solid6[16] = {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6};
static const uint8_t k_solid7[16] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
static const gfx_image_t k_sq5 = {4, 4, k_solid5};
static const gfx_image_t k_sq6 = {4, 4, k_solid6};
static const gfx_image_t k_sq7 = {4, 4, k_solid7};

static gfx_surface_t surface(void)
{
    for (int i = 0; i < SW * SH; i++) {
        s_px[i] = POISON;
    }
    return (gfx_surface_t){s_px, SW, SH, SW};
}

static void setup(gfx_scene_t *s, int x_align)
{
    const bool ok = gfx_scene_init(s, SW, SH, test_background, NULL, s_strip, SW * 3, x_align);
    (void)ok; // checked by the tests that care
}

static gfx_sprite_t sprite(const gfx_image_t *img, int x, int y, int scale, uint8_t flags)
{
    gfx_sprite_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.image = img;
    sp.palette = &k_pal;
    sp.x = x;
    sp.y = y;
    sp.scale = (uint8_t)scale;
    sp.flags = flags;
    return sp;
}

static void render_full(const gfx_scene_t *s, const gfx_surface_t *dst)
{
    gfx_dirty_t all;
    gfx_dirty_init(&all, SW, SH, 1);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, SW, SH});
    gfx_scene_render(s, &all, dst);
}

static gfx_color_t at(int x, int y)
{
    return s_px[y * SW + x];
}

void test_scene_renders_only_inside_dirty_rects(void)
{
    gfx_scene_t s;
    CHECK(gfx_scene_init(&s, SW, SH, test_background, NULL, s_strip, SW * 3, 1));
    CHECK(!gfx_scene_init(&s, SW, SH, test_background, NULL, s_strip, SW - 1, 1)); // strip shorter than a row
    gfx_sprite_t sp = sprite(&k_sq5, 6, 6, 3, 0);
    CHECK(gfx_scene_add(&s, &sp, 0));
    const gfx_surface_t dst = surface();
    gfx_dirty_t d;
    gfx_dirty_init(&d, SW, SH, 1);
    gfx_dirty_add(&d, (gfx_rect_t){8, 8, 16, 4});
    gfx_dirty_add(&d, (gfx_rect_t){40, 30, 5, 10});
    gfx_scene_render(&s, &d, &dst);
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            if (test_dirty_covers(&d, x, y)) {
                CHECK_EQ(at(x, y), test_reference_pixel(&s, x, y));
            } else {
                CHECK_EQ(at(x, y), POISON);
            }
        }
    }
}

void test_scene_transparent_pixels_show_background(void)
{
    gfx_scene_t s;
    setup(&s, 1);
    gfx_sprite_t sp = sprite(&k_img3x2, 10, 10, 1, 0);
    CHECK(gfx_scene_add(&s, &sp, 0));
    const gfx_surface_t dst = surface();
    render_full(&s, &dst);
    CHECK_EQ(at(10, 10), 1);
    CHECK_EQ(at(11, 10), test_background_at(11, 10)); // index 0
    CHECK_EQ(at(12, 10), 2);
    CHECK_EQ(at(10, 11), 3);
    CHECK_EQ(at(11, 11), 4);
    CHECK_EQ(at(12, 11), test_background_at(12, 11));
    CHECK_EQ(at(9, 10), test_background_at(9, 10));
    CHECK_EQ(at(13, 11), test_background_at(13, 11));
}

void test_scene_layers_follow_z_then_insertion_order(void)
{
    gfx_scene_t s;
    setup(&s, 1);
    gfx_sprite_t top = sprite(&k_sq5, 10, 10, 1, 0);
    gfx_sprite_t bottom = sprite(&k_sq6, 12, 12, 1, 0);
    gfx_sprite_t same_z_later = sprite(&k_sq7, 11, 8, 1, 0);
    CHECK(gfx_scene_add(&s, &top, 5));
    CHECK(gfx_scene_add(&s, &bottom, 1)); // added later but lower z
    CHECK(gfx_scene_add(&s, &same_z_later, 5));
    const gfx_surface_t dst = surface();
    render_full(&s, &dst);
    CHECK_EQ(at(13, 13), 5); // top over bottom
    CHECK_EQ(at(15, 15), 6); // bottom alone
    CHECK_EQ(at(11, 10), 7); // equal z: later insertion on top
    CHECK_EQ(at(10, 12), 5);
}

void test_scene_integer_scaling(void)
{
    gfx_scene_t s;
    setup(&s, 1);
    gfx_sprite_t sp = sprite(&k_img2x2, 5, 7, 3, 0);
    CHECK(gfx_scene_add(&s, &sp, 0));
    const gfx_rect_t b = gfx_sprite_bounds(&sp);
    CHECK(b.x == 5 && b.y == 7 && b.w == 6 && b.h == 6);
    const gfx_surface_t dst = surface();
    render_full(&s, &dst);
    for (int dy = 0; dy < 6; dy++) {
        for (int dx = 0; dx < 6; dx++) {
            CHECK_EQ(at(5 + dx, 7 + dy), k_px2x2[(dy / 3) * 2 + dx / 3]);
        }
    }
    CHECK_EQ(at(11, 7), test_background_at(11, 7));
    CHECK_EQ(at(5, 13), test_background_at(5, 13));
}

void test_scene_flip_x(void)
{
    gfx_scene_t s;
    setup(&s, 1);
    gfx_sprite_t sp = sprite(&k_img3x1, 0, 0, 2, GFX_SPRITE_FLIP_X);
    CHECK(gfx_scene_add(&s, &sp, 0));
    const gfx_surface_t dst = surface();
    render_full(&s, &dst);
    const gfx_color_t want[6] = {3, 3, 2, 2, 1, 1};
    for (int x = 0; x < 6; x++) {
        CHECK_EQ(at(x, 0), want[x]);
        CHECK_EQ(at(x, 1), want[x]);
    }
    CHECK_EQ(at(6, 0), test_background_at(6, 0));
    CHECK_EQ(at(0, 2), test_background_at(0, 2));
}

void test_scene_clips_sprites_at_screen_edges(void)
{
    gfx_scene_t s;
    setup(&s, 1);
    gfx_sprite_t left_top = sprite(&k_img3x2, -3, -1, 2, 0);
    gfx_sprite_t right_bottom = sprite(&k_img2x2, 60, 45, 3, 0);
    gfx_sprite_t flipped_edge = sprite(&k_img3x2, 62, -2, 3, GFX_SPRITE_FLIP_X);
    gfx_sprite_t offscreen = sprite(&k_sq5, -100, 0, 4, 0);
    CHECK(gfx_scene_add(&s, &left_top, 0));
    CHECK(gfx_scene_add(&s, &right_bottom, 1));
    CHECK(gfx_scene_add(&s, &flipped_edge, 2));
    CHECK(gfx_scene_add(&s, &offscreen, 3));
    const gfx_surface_t dst = surface();
    render_full(&s, &dst);
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            CHECK_EQ(at(x, y), test_reference_pixel(&s, x, y));
        }
    }
    // left_top starts 3 screen px left of the edge at scale 2: x = 0 is source column 1 (transparent), x = 1 is
    // the first half of column 2.
    CHECK_EQ(at(0, 0), test_background_at(0, 0));
    CHECK_EQ(at(1, 0), 2);
}

void test_scene_random_sprites_match_reference(void)
{
    uint32_t rng = 777;
    static uint8_t pixels[6][25];
    static gfx_image_t images[6];
    gfx_sprite_t sprites[6];
    for (int iter = 0; iter < 400; iter++) {
        gfx_scene_t s;
        const int aligns[3] = {1, 4, 8};
        setup(&s, aligns[iter % 3]);
        const int n = test_rand_range(&rng, 1, 6);
        for (int i = 0; i < n; i++) {
            images[i].w = (uint16_t)test_rand_range(&rng, 1, 5);
            images[i].h = (uint16_t)test_rand_range(&rng, 1, 5);
            for (int p = 0; p < images[i].w * images[i].h; p++) {
                pixels[i][p] = (uint8_t)test_rand_range(&rng, 0, 9);
            }
            images[i].px = pixels[i];
            uint8_t flags = 0;
            if (test_rand_range(&rng, 0, 3) == 0) {
                flags |= GFX_SPRITE_FLIP_X;
            }
            if (test_rand_range(&rng, 0, 7) == 0) {
                flags |= GFX_SPRITE_HIDDEN;
            }
            sprites[i] = sprite(&images[i], test_rand_range(&rng, -20, SW + 5), test_rand_range(&rng, -20, SH + 5),
                                test_rand_range(&rng, 1, 4), flags);
            CHECK(gfx_scene_add(&s, &sprites[i], test_rand_range(&rng, 0, 3)));
        }
        gfx_dirty_t d;
        gfx_dirty_init(&d, SW, SH, aligns[iter % 3]);
        const int rects = test_rand_range(&rng, 1, 4);
        for (int i = 0; i < rects; i++) {
            gfx_dirty_add(&d, (gfx_rect_t){test_rand_range(&rng, -10, SW), test_rand_range(&rng, -10, SH),
                                           test_rand_range(&rng, 1, SW), test_rand_range(&rng, 1, SH)});
        }
        const gfx_surface_t dst = surface();
        gfx_scene_render(&s, &d, &dst);
        for (int y = 0; y < SH; y++) {
            for (int x = 0; x < SW; x++) {
                const gfx_color_t want = test_dirty_covers(&d, x, y) ? test_reference_pixel(&s, x, y) : POISON;
                if (at(x, y) != want) {
                    printf("    iteration %d pixel (%d,%d)\n", iter, x, y);
                }
                CHECK_EQ(at(x, y), want);
            }
        }
    }
}

void test_scene_collect_reports_old_and_new_bounds(void)
{
    gfx_scene_t s;
    setup(&s, 1);
    gfx_sprite_t sp = sprite(&k_img3x2, 10, 10, 1, 0);
    CHECK(gfx_scene_add(&s, &sp, 0));
    gfx_dirty_t d;
    gfx_dirty_init(&d, SW, SH, 1);

    gfx_scene_collect(&s, &d); // first appearance
    CHECK_EQ(gfx_dirty_area(&d), 6);
    CHECK(test_dirty_covers(&d, 10, 10) && test_dirty_covers(&d, 12, 11));

    gfx_dirty_clear(&d);
    gfx_scene_collect(&s, &d); // nothing changed
    CHECK_EQ(d.count, 0);

    sp.x = 20; // move: old and new position
    gfx_dirty_clear(&d);
    gfx_scene_collect(&s, &d);
    CHECK_EQ(gfx_dirty_area(&d), 12);
    CHECK(test_dirty_covers(&d, 10, 10) && test_dirty_covers(&d, 22, 11));

    sp.flags = GFX_SPRITE_HIDDEN; // hide: where it was
    gfx_dirty_clear(&d);
    gfx_scene_collect(&s, &d);
    CHECK_EQ(gfx_dirty_area(&d), 6);
    CHECK(test_dirty_covers(&d, 20, 10));

    gfx_dirty_clear(&d);
    sp.x = 30; // moving while hidden changes nothing on screen
    gfx_scene_collect(&s, &d);
    CHECK_EQ(d.count, 0);

    sp.flags = 0; // show again at the new place
    gfx_scene_collect(&s, &d);
    CHECK_EQ(gfx_dirty_area(&d), 6);
    CHECK(test_dirty_covers(&d, 30, 10));

    // Same bounds, different content: image, palette, flip.
    const gfx_palette_t other_pal = {s_colors, 10};
    const struct {
        const gfx_image_t *image;
        const gfx_palette_t *palette;
        uint8_t flags;
    } steps[] = {{&k_img3x2b, &k_pal, 0}, {&k_img3x2b, &other_pal, 0}, {&k_img3x2b, &other_pal, GFX_SPRITE_FLIP_X}};
    for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        sp.image = steps[i].image;
        sp.palette = steps[i].palette;
        sp.flags = steps[i].flags;
        gfx_dirty_clear(&d);
        gfx_scene_collect(&s, &d);
        CHECK_EQ(gfx_dirty_area(&d), 6);
    }

    sp.scale = 2; // grows in place: old bounds lie inside the new ones
    gfx_dirty_clear(&d);
    gfx_scene_collect(&s, &d);
    CHECK_EQ(gfx_dirty_area(&d), 24);
}

void test_scene_collect_includes_invalidations(void)
{
    gfx_scene_t s;
    setup(&s, 1);
    gfx_dirty_t d;
    gfx_dirty_init(&d, SW, SH, 1);
    gfx_scene_invalidate(&s, (gfx_rect_t){0, 0, 4, 4});
    gfx_scene_invalidate(&s, (gfx_rect_t){-10, 40, 100, 100}); // clipped
    gfx_scene_collect(&s, &d);
    CHECK_EQ(gfx_dirty_area(&d), 16 + SW * 8);
    gfx_dirty_clear(&d);
    gfx_scene_collect(&s, &d); // consumed
    CHECK_EQ(d.count, 0);
}
