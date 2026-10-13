#include <string.h>

#include "gfx_dirty.h"
#include "test.h"
#include "test_util.h"

static bool rect_is(gfx_rect_t r, int x, int y, int w, int h)
{
    return r.x == x && r.y == y && r.w == w && r.h == h;
}

// Merging reorders the set, so look rectangles up instead of indexing.
static bool has_rect(const gfx_dirty_t *d, int x, int y, int w, int h)
{
    for (int i = 0; i < d->count; i++) {
        if (rect_is(d->rect[i], x, y, w, h)) {
            return true;
        }
    }
    return false;
}

void test_dirty_clips_to_screen(void)
{
    gfx_dirty_t d;
    gfx_dirty_init(&d, 100, 80, 1);
    gfx_dirty_add(&d, (gfx_rect_t){-10, -10, 20, 20});
    CHECK_EQ(d.count, 1);
    CHECK(rect_is(d.rect[0], 0, 0, 10, 10));

    gfx_dirty_add(&d, (gfx_rect_t){95, 75, 20, 20});
    CHECK_EQ(d.count, 2);
    CHECK(rect_is(d.rect[1], 95, 75, 5, 5));

    gfx_dirty_add(&d, (gfx_rect_t){100, 0, 5, 5});  // just right of the screen
    gfx_dirty_add(&d, (gfx_rect_t){0, 80, 5, 5});   // just below
    gfx_dirty_add(&d, (gfx_rect_t){-5, 0, 5, 5});   // just left
    gfx_dirty_add(&d, (gfx_rect_t){40, 40, 0, 10}); // empty
    gfx_dirty_add(&d, (gfx_rect_t){40, 40, 10, -1});
    CHECK_EQ(d.count, 2);
    CHECK_EQ(gfx_dirty_area(&d), 125);
}

void test_dirty_aligns_x_to_cache_lines(void)
{
    gfx_dirty_t d;
    gfx_dirty_init(&d, 100, 50, 32);
    gfx_dirty_add(&d, (gfx_rect_t){33, 5, 10, 10});
    CHECK_EQ(d.count, 1);
    CHECK(rect_is(d.rect[0], 32, 5, 32, 10));

    gfx_dirty_add(&d, (gfx_rect_t){90, 20, 5, 5}); // 90..94 -> 64..95
    CHECK_EQ(d.count, 2);
    CHECK(has_rect(&d, 64, 20, 32, 5));

    // Near the right edge the aligned end is clamped to the screen width (100 is not a multiple of 32).
    gfx_dirty_add(&d, (gfx_rect_t){98, 30, 1, 5}); // 98 -> 96..99
    CHECK_EQ(d.count, 3);
    CHECK(has_rect(&d, 96, 30, 4, 5));

    // Exactly aligned input is unchanged; y and h are never aligned.
    gfx_dirty_add(&d, (gfx_rect_t){0, 41, 32, 3});
    CHECK(has_rect(&d, 0, 41, 32, 3));
    CHECK(test_dirty_violation(&d) == NULL);
}

void test_dirty_merges_overlapping_and_flush_rects(void)
{
    gfx_dirty_t d;
    gfx_dirty_init(&d, 100, 100, 1);
    gfx_dirty_add(&d, (gfx_rect_t){0, 0, 10, 10});
    gfx_dirty_add(&d, (gfx_rect_t){5, 5, 10, 10});
    CHECK_EQ(d.count, 1);
    CHECK(rect_is(d.rect[0], 0, 0, 15, 15));

    gfx_dirty_add(&d, (gfx_rect_t){2, 2, 3, 3}); // already covered
    CHECK_EQ(d.count, 1);
    CHECK(rect_is(d.rect[0], 0, 0, 15, 15));

    gfx_dirty_add(&d, (gfx_rect_t){40, 40, 5, 5}); // disjoint
    CHECK_EQ(d.count, 2);

    gfx_dirty_add(&d, (gfx_rect_t){15, 0, 5, 15}); // flush with the first: merging costs nothing
    CHECK_EQ(d.count, 2);
    CHECK(has_rect(&d, 0, 0, 20, 15));
    CHECK(has_rect(&d, 40, 40, 5, 5));

    gfx_dirty_add(&d, (gfx_rect_t){20, 0, 5, 5}); // touches but would grow the box: stays separate
    CHECK_EQ(d.count, 3);
    CHECK(test_dirty_violation(&d) == NULL);
}

void test_dirty_merge_chains_stay_disjoint(void)
{
    gfx_dirty_t d;
    gfx_dirty_init(&d, 100, 100, 1);
    gfx_dirty_add(&d, (gfx_rect_t){0, 0, 10, 10});
    gfx_dirty_add(&d, (gfx_rect_t){30, 0, 10, 10});
    gfx_dirty_add(&d, (gfx_rect_t){60, 20, 10, 10});
    CHECK_EQ(d.count, 3);
    // Bridges the first two; their union then overlaps nothing else.
    gfx_dirty_add(&d, (gfx_rect_t){5, 0, 30, 2});
    CHECK_EQ(d.count, 2);
    CHECK(test_dirty_violation(&d) == NULL);
    // A tall rectangle overlapping the merged one, whose union then reaches the third.
    gfx_dirty_add(&d, (gfx_rect_t){35, 5, 30, 20});
    CHECK_EQ(d.count, 1);
    CHECK(rect_is(d.rect[0], 0, 0, 70, 30));
}

void test_dirty_overflow_keeps_coverage(void)
{
    enum { W = 200, H = 200 };
    static bool want[H][W];
    memset(want, 0, sizeof(want));
    gfx_dirty_t d;
    gfx_dirty_init(&d, W, H, 1);
    for (int i = 0; i < 40; i++) { // far more than GFX_DIRTY_MAX disjoint rectangles
        const gfx_rect_t r = {(i % 8) * 25, (i / 8) * 40, 5, 5};
        gfx_dirty_add(&d, r);
        for (int y = r.y; y < r.y + r.h; y++) {
            for (int x = r.x; x < r.x + r.w; x++) {
                want[y][x] = true;
            }
        }
        CHECK(d.count <= GFX_DIRTY_MAX);
        CHECK(test_dirty_violation(&d) == NULL);
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            CHECK(!want[y][x] || test_dirty_covers(&d, x, y));
        }
    }
}

void test_dirty_random_adds_keep_invariants(void)
{
    enum { W = 200, H = 120 };
    static bool want[H][W];
    uint32_t rng = 12345;
    for (int trial = 0; trial < 60; trial++) {
        memset(want, 0, sizeof(want));
        gfx_dirty_t d;
        gfx_dirty_init(&d, W, H, trial % 2 ? 8 : 1);
        const int adds = test_rand_range(&rng, 1, 40);
        for (int i = 0; i < adds; i++) {
            const gfx_rect_t r = {test_rand_range(&rng, -30, W), test_rand_range(&rng, -30, H),
                                  test_rand_range(&rng, 0, 60), test_rand_range(&rng, 0, 40)};
            gfx_dirty_add(&d, r);
            const gfx_rect_t c = gfx_rect_intersect(r, d.screen);
            for (int y = c.y; y < c.y + c.h; y++) {
                for (int x = c.x; x < c.x + c.w; x++) {
                    want[y][x] = true;
                }
            }
            const char *violation = test_dirty_violation(&d);
            if (violation) {
                printf("    trial %d add %d: %s\n", trial, i, violation);
            }
            CHECK(violation == NULL);
        }
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                CHECK(!want[y][x] || test_dirty_covers(&d, x, y));
            }
        }
    }
}
