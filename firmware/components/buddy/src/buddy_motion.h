// Small timing and easing helpers shared by the behavior modules. Private to the buddy component.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Wrap-around safe "now is at or after t".
static inline bool buddy_reached(uint32_t now, uint32_t t)
{
    return (int32_t)(now - t) >= 0;
}

static inline uint32_t buddy_min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

// Parabolic hop: 0 at both ends, `height` halfway, rounded to whole art pixels.
static inline uint8_t buddy_hop(uint32_t t, uint32_t duration, uint32_t height)
{
    if (t >= duration) {
        return 0;
    }
    const uint64_t num = 4ull * height * t * (duration - t);
    const uint64_t den = (uint64_t)duration * duration;
    return (uint8_t)((num + den / 2) / den);
}

// Smoothstep from 0 (t = 0) to 255 (t >= duration): slow start, slow end.
static inline uint8_t buddy_ease(uint32_t t, uint32_t duration)
{
    if (t >= duration) {
        return 255;
    }
    const uint64_t x = (uint64_t)t * 1024 / duration; // 0..1023
    return (uint8_t)(x * x * (3 * 1024 - 2 * x) * 255 / (1024ull * 1024 * 1024));
}
