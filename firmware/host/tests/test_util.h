// Shared fixtures: deterministic RNG, dirty-set invariant checks, and a naive reference renderer that is
// deliberately written differently from the compositor (per pixel, no runs or strips).
#pragma once

#include <stdbool.h>

#include "gfx_dirty.h"
#include "gfx_scene.h"

uint32_t test_rand(uint32_t *state);
int test_rand_range(uint32_t *state, int lo, int hi); // inclusive

bool test_dirty_covers(const gfx_dirty_t *d, int x, int y);

// Checks the gfx_dirty_t invariants. Returns NULL when they hold, otherwise a description of the violation.
const char *test_dirty_violation(const gfx_dirty_t *d);

// Background with a unique color per pixel (bit 15 set; test palettes stay below 0x8000).
void test_background(void *ctx, gfx_color_t *dst, int x, int y, int w);
gfx_color_t test_background_at(int x, int y);

// What the scene should show at (x, y), computed per pixel from the sprites.
gfx_color_t test_reference_pixel(const gfx_scene_t *s, int x, int y);
