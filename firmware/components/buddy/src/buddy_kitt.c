#include "buddy_kitt.h"

#include <math.h>
#include <string.h>

#include "buddy_motion.h"

// Turning into KITT (ms since the start).
#define HALF_MS 150u      // eyes half shut...
#define DIM_AT 300u       // ...shut, and the face's glow turned down
#define WIPE_END 1300u    // the traces (k_in) have crossed: the face screen is black
#define LINES_GONE 1360u  // the lines they left have faded
#define FLICKER_AT 1400u  // one short flicker of the housing
#define FLICKER_END 1450u
#define HOUSING_AT 1500u  // the housing comes up, its rim first
#define HOUSING_FULL 1560u
#define LIGHT_AT 1600u    // the light fades in at the left end, full strength at BUDDY_KITT_IN_MS
// Turning back (ms since the way back began).
#define SLOW_MS 500u      // the sweep slows to a stop in the middle
#define FADE_AT 300u      // the light fades...
#define FADE_END 600u     // ...out; the housing dims
#define HOUSING_OFF 700u  // the housing is dark; the traces (k_back) race back
#define BACK_END 1300u
#define OPEN_HALF 1500u   // his eyes open again
#define OPEN_AT 1650u

#define TRACE 4    // a trace's length, overlay pixels
#define ECHOES 8   // the light's tail: where it was this many times before...
#define ECHO_MS 28 // ...this far apart

// One trace per band of rows (top, middle, bottom), each with its own start and length.
static const struct {
    uint16_t at, ms;
} k_in[3] = {{600, 620}, {660, 640}, {630, 560}}, k_back[3] = {{700, 560}, {730, 540}, {760, 520}};

enum { C_CLEAR, C_BLACK, C_RIM, C_CHANNEL, C_1, C_2, C_3, C_4, C_HOT, C_COUNT };

// Not const: keeps the palette in internal RAM (read-only data is mapped from PSRAM on this board).
static gfx_color_t s_colors[C_COUNT] = {
    0,
    GFX_RGB(5, 5, 7),       // the face screen, switched off
    GFX_RGB(30, 5, 7),      // the housing's rim
    GFX_RGB(52, 7, 9),      // its channel: the lamps, unlit
    GFX_RGB(96, 10, 10),    // the light, from its faintest...
    GFX_RGB(150, 16, 14),
    GFX_RGB(205, 26, 20),
    GFX_RGB(245, 44, 30),
    GFX_RGB(255, 110, 84),  // ...to its hot core
};
static const gfx_palette_t s_palette = {s_colors, C_COUNT};

const gfx_palette_t *buddy_kitt_palette(void)
{
    return &s_palette;
}

uint32_t buddy_kitt_pose(uint32_t t, uint32_t out, buddy_pose_t *p)
{
    *p = (buddy_pose_t){.state = BUDDY_STATE_KITT, .eyes = BUDDY_EYES_CLOSED, .mouth = BUDDY_MOUTH_NEUTRAL,
                        .antenna = BUDDY_ANTENNA_CALM, .tint = BUDDY_TINT_DROWSY};
    p->kitt = (buddy_kitt_t){false, t, out};
    if (!out || t < out) {
        if (t < HALF_MS) {
            p->eyes = BUDDY_EYES_HALF;
            p->tint = BUDDY_TINT_NORMAL;
            return HALF_MS - t;
        }
        if (t < DIM_AT) {
            p->tint = BUDDY_TINT_NORMAL;
            return DIM_AT - t;
        }
        if (t < k_in[0].at) {
            return k_in[0].at - t;
        }
        p->kitt.on = true;
        p->antenna = t >= WIPE_END ? BUDDY_ANTENNA_STEADY : BUDDY_ANTENNA_CALM;
        if (t < FLICKER_AT) {
            return buddy_min_u32(BUDDY_KITT_FRAME_MS, FLICKER_AT - t);
        }
        if (t < FLICKER_END) {
            return FLICKER_END - t;
        }
        if (t < HOUSING_AT) {
            return HOUSING_AT - t;
        }
        return out ? buddy_min_u32(BUDDY_KITT_FRAME_MS, out - t) : BUDDY_KITT_FRAME_MS;
    }
    const uint32_t u = t - out;
    if (u < BACK_END) {
        p->kitt.on = true;
        p->antenna = BUDDY_ANTENNA_STEADY;
        return buddy_min_u32(BUDDY_KITT_FRAME_MS, BACK_END - u);
    }
    p->tint = BUDDY_TINT_NORMAL;
    if (u < OPEN_HALF) {
        return OPEN_HALF - u;
    }
    if (u < OPEN_AT) {
        p->eyes = BUDDY_EYES_HALF;
        return OPEN_AT - u;
    }
    p->eyes = BUDDY_EYES_OPEN;
    return u < BUDDY_KITT_OUT_MS ? BUDDY_KITT_OUT_MS - u : 1;
}

// --- Drawing ---------------------------------------------------------------------------------------------------

static float smooth(float x)
{
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    return x * x * (3.0f - 2.0f * x);
}

// The sweep, `tau` ms after the light came on at the left end: 0 (left) .. 1 (right), easing at both ends.
static float sweep(float tau)
{
    const float period = (float)BUDDY_KITT_SWEEP_MS;
    float phase = fmodf(tau, period);
    phase = phase < 0.0f ? phase + period : phase;
    return 0.5f - 0.5f * cosf(6.2831853f * phase / period);
}

// Where the light is at t: sweeping; on the way back it slows to a stop and glides to the middle.
static float light_at(uint32_t t, uint32_t out)
{
    if (!out || t < out) {
        return sweep((float)t - (float)LIGHT_AT);
    }
    const float s = (float)SLOW_MS, u = (float)(t - out) < s ? (float)(t - out) : s;
    const float p = sweep((float)out - (float)LIGHT_AT + u - u * u / (2.0f * s));
    return p + (0.5f - p) * smooth(u / s);
}

// How bright the light is at t: 0..1.
static float brightness(uint32_t t, uint32_t out)
{
    if (!out || t < out) {
        return t < LIGHT_AT ? 0.0f : t >= BUDDY_KITT_IN_MS ? 1.0f : (float)(t - LIGHT_AT) / (BUDDY_KITT_IN_MS - LIGHT_AT);
    }
    const uint32_t u = t - out;
    return u < FADE_AT ? 1.0f : u >= FADE_END ? 0.0f : 1.0f - (float)(u - FADE_AT) / (FADE_END - FADE_AT);
}

// The housing at t: 0 dark, 1 dim (all rim colour), 2 lit (the channel slightly brighter than its rim).
static int housing(uint32_t t, uint32_t out)
{
    if (!out || t < out) {
        if (t >= FLICKER_AT && t < FLICKER_END) {
            return 1;
        }
        return t < HOUSING_AT ? 0 : t < HOUSING_FULL ? 1 : 2;
    }
    const uint32_t u = t - out;
    return u < FADE_END ? 2 : u < HOUSING_OFF ? 1 : 0;
}

// The traces: in band b the face screen is black left of `front` (overlay pixels) and shows his face right of it;
// the trace's bright head is at the front, its tail behind it (to the left going in, to the right coming back).
static void traces(uint8_t *px, int w, int h, uint32_t t, bool back)
{
    for (int b = 0; b < 3; b++) {
        const float into = back ? (float)t - k_back[b].at : (float)t - k_in[b].at;
        const float e = smooth(into / (back ? k_back[b].ms : k_in[b].ms));
        const float front = back ? w - e * (w + TRACE) : e * (w + TRACE);
        const int y0 = b * h / 3, y1 = (b + 1) * h / 3, line = (y0 + y1) / 2;
        for (int y = y0; y < y1; y++) {
            for (int x = 0; x < w; x++) {
                const float d = back ? (float)x - front : front - (float)x - 1.0f; // >= 0: on the trace's tail
                uint8_t c = (float)x < front ? C_BLACK : C_CLEAR;
                if (y == line && d >= 0.0f && d < TRACE) {
                    c = d < 1.0f ? C_HOT : d < 2.0f ? C_4 : d < 3.0f ? C_3 : C_2;
                } else if (y == line && !back && d >= TRACE && t < LINES_GONE) {
                    c = C_RIM; // the line it leaves, fading soon
                }
                px[y * w + x] = c;
            }
        }
    }
}

void buddy_kitt_draw(const buddy_kitt_t *k, uint8_t *px, int w, int h)
{
    const uint32_t t = k->t, out = k->out;
    const bool back = out && t >= out;
    memset(px, C_BLACK, (size_t)w * h);
    if (!back && t < LINES_GONE) {
        traces(px, w, h, t, false);
    } else if (back && t - out >= HOUSING_OFF) {
        traces(px, w, h, t - out, true);
    }
    const int level = housing(t, out);
    if (!level) {
        return;
    }

    // The housing: a dark-red channel across the middle with a rim around it; the lamps fill the channel.
    const int f = h / 8 > 1 ? h / 8 : 1, y0 = h / 2 - f / 2, y1 = y0 + f - 1; // lamp rows
    const int x0 = f, x1 = w - 1 - f, lamps = x1 - x0 + 1;                     // lamp columns
    for (int x = x0 - 1; x <= x1 + 1; x++) {
        px[(y0 - 1) * w + x] = px[(y1 + 1) * w + x] = C_RIM;
    }
    for (int y = y0; y <= y1; y++) {
        px[y * w + x0 - 1] = px[y * w + x1 + 1] = C_RIM;
        memset(px + y * w + x0, level == 2 ? C_CHANNEL : C_RIM, (size_t)lamps);
    }
    const float light = brightness(t, out);
    if (light <= 0.0f) {
        return;
    }

    // The light and its tail: where it was over the last few frames, each echo fainter.
    float at[ECHOES + 1];
    int n = 0;
    for (; n <= ECHOES && t >= (uint32_t)n * ECHO_MS; n++) {
        at[n] = light_at(t - (uint32_t)n * ECHO_MS, out) * (lamps - 1);
    }
    for (int x = 0; x < lamps; x++) {
        float v = 0.0f;
        for (int j = 0; j < n; j++) {
            const float d = fabsf(x - at[j]);
            // The lamp nearest the light is always fully lit; its neighbours show where between two lamps it is.
            const float near = j == 0 && d <= 0.5f ? 1.0f : 1.0f - d / 1.6f, fade = 1.0f - (float)j / (ECHOES + 1);
            v = near * fade * fade > v ? near * fade * fade : v;
        }
        v *= light;
        const uint8_t c = v > 0.85f   ? C_HOT
                          : v > 0.6f  ? C_4
                          : v > 0.38f ? C_3
                          : v > 0.2f  ? C_2
                          : v > 0.08f ? C_1
                                      : 0;
        if (!c) {
            continue;
        }
        for (int y = y0; y <= y1; y++) {
            px[y * w + x0 + x] = c;
        }
        if (c >= C_3) { // a faint glow on the rim above and below
            px[(y0 - 1) * w + x0 + x] = px[(y1 + 1) * w + x0 + x] = (uint8_t)(c - 3);
        }
    }
}
