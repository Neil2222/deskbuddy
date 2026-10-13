#include "gfx_anim.h"
#include "test.h"

static const gfx_image_t k_a = {1, 1, NULL}, k_b = {1, 1, NULL}, k_c = {1, 1, NULL};

void test_anim_loops(void)
{
    const gfx_anim_frame_t frames[] = {{&k_a, 100}, {&k_b, 50}};
    const gfx_anim_t anim = {frames, 2, true};
    uint32_t next = 0;
    CHECK(gfx_anim_image(&anim, 0, &next) == &k_a);
    CHECK_EQ(next, 100);
    CHECK(gfx_anim_image(&anim, 99, &next) == &k_a);
    CHECK_EQ(next, 1);
    CHECK(gfx_anim_image(&anim, 100, &next) == &k_b);
    CHECK_EQ(next, 50);
    CHECK(gfx_anim_image(&anim, 150, &next) == &k_a); // wrapped
    CHECK_EQ(next, 100);
    CHECK(gfx_anim_image(&anim, 1000, NULL) == &k_b); // 1000 % 150 = 100
    CHECK(gfx_anim_image(&anim, UINT32_MAX, &next) != NULL);
    CHECK(next >= 1 && next <= 100);
}

void test_anim_holds_last_frame(void)
{
    const gfx_anim_frame_t frames[] = {{&k_a, 30}, {&k_b, 30}, {&k_c, 30}};
    const gfx_anim_t once = {frames, 3, false};
    uint32_t next = 0;
    CHECK(gfx_anim_image(&once, 65, &next) == &k_c);
    CHECK_EQ(next, 25);
    CHECK(gfx_anim_image(&once, 90, &next) == &k_c);
    CHECK_EQ(next, UINT32_MAX);
    CHECK(gfx_anim_image(&once, 5000, &next) == &k_c);
    CHECK_EQ(next, UINT32_MAX);

    const gfx_anim_t single = {frames, 1, true}; // one frame never changes, even when looping
    CHECK(gfx_anim_image(&single, 12345, &next) == &k_a);
    CHECK_EQ(next, UINT32_MAX);
}
