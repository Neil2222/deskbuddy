#include "office_skeleton.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "office_font.h"

// --- Design ---------------------------------------------------------------------------------------------------------
// Screen positions are for a 1024 x 600 screen (centred in larger ones). Art pixels are SC x SC screen pixels.

#define DESIGN_W 1024
#define DESIGN_H 600
#define SC 2
#define PI_F 3.14159265f
#define TWO_PI 6.2831853f

// The background: the clock's near-black (Buddy's switched-off face is this colour) and the shadows on it.
#define BG GFX_RGB(8, 9, 12)
#define BG_SHADOW GFX_RGB(4, 5, 7)
#define SHADOW_OFFSET 7 // screen pixels down and right (the light comes from the top left)

// Palette of the movement, the bridgework and the timeplate.
enum {
    P_BRASS = 1,   // 7 levels
    P_BRONZE = 8,  // 7 levels
    P_STEEL = 15,  // 5 levels
    P_RUBY = 20,   // 3 levels
    P_BLUED = 23,  // 3 levels
    P_SHADOW = 26, // cast on what lies below
    P_ENAMEL = 27, // 3 levels: the timeplate
    P_WINDOW = 30, // 3 levels: the digit windows (the numerals' ink fades into the middle one)
    P_COUNT = 33,
};
static const uint8_t k_rgb[P_COUNT][3] = {
    {0, 0, 0},
    // brass: deep shadow to polished highlight
    {44, 26, 10}, {84, 54, 20}, {126, 86, 34}, {168, 120, 50}, {204, 156, 70}, {232, 192, 104}, {252, 232, 168},
    // bronze: darker, redder
    {34, 20, 14}, {62, 38, 24}, {94, 58, 34}, {128, 82, 46}, {162, 106, 60}, {196, 138, 84}, {230, 184, 132},
    // steel
    {30, 34, 42}, {64, 70, 84}, {108, 116, 132}, {156, 164, 180}, {214, 220, 232},
    // ruby
    {96, 8, 24}, {188, 28, 52}, {255, 128, 150},
    // blued steel
    {18, 28, 70}, {38, 70, 156}, {112, 152, 236},
    // cast shadow
    {3, 3, 5},
    // enamel: dark, base, light
    {12, 11, 15}, {24, 22, 30}, {44, 40, 52},
    // windows: dark, base, light
    {5, 5, 7}, {11, 11, 15}, {26, 25, 32},
};

// The numerals: ivory on the windows' colour.
static const uint8_t k_ivory[3] = {240, 230, 200};
static const uint8_t k_ink_shadow[3] = {3, 3, 4};
// The date: brass engraved into the plaque's enamel.
static const uint8_t k_date_ink[3] = {226, 184, 104};

// Numerals as large as Classic's, with a heavier stroke.
static const office_font_style_t k_num = {.unit = 8.0f, .stroke = 6.2f, .tracking = 0};
#define PAD 2
#define DIGIT_GAP 8  // art pixels between the two digits of HH and of MM (each has its own window)
#define COLON_GAP 20 // art pixels between HH and MM
#define WINDOW_MARGIN 2
static const office_font_style_t k_date = {.unit = 2.0f, .stroke = 1.8f, .tracking = 9.0f};
#define DATE_W 480

// The timeplate (screen): plate, digits in the middle at Classic's height, and the plaque under it.
#define PLATE_X 210
#define PLATE_Y 132
#define PLATE_W 302 // art
#define PLATE_MAIN_H 120
#define PLATE_H 152
#define PLAQUE_Y0 124 // art, relative to the plate
#define PLAQUE_Y1 148
#define PLAQUE_X0 18
#define PLAQUE_X1 284
#define PLATE_IMG_W (PLATE_W + 3) // room for the shadow it casts on the movement
#define PLATE_IMG_H (PLATE_H + 3)

// --- Materials --------------------------------------------------------------------------------------------------------

enum {
    M_NONE,
    M_BRASS,         // a brass wheel
    M_BRONZE,        // a bronze wheel
    M_BRASS_GROOVE,  // a turned groove in a wheel
    M_BRONZE_GROOVE,
    M_PINION,        // steel pinion leaves on a wheel
    M_COLLAR,        // the brass collar on the anchor's arbor
    M_ARBOR,         // the steel arbor end
    M_ANCHOR,        // the polished steel anchor
    M_FRAME,         // the frosted bronze frame behind the wheels
    M_BRIDGE,        // the escapement's cock in front
    M_CHATON,        // the polished ring a jewel sits in
    M_RUBY,
    M_BLUED,         // screw heads
    M_SLOT,          // a screw's slot
    M_WINDOW,
    M_ENAMEL,
    M_BEZEL,         // the timeplate's brass frame
    M_STUD,          // the colon's brass studs
    M_COUNT,
};

typedef struct {
    uint8_t start, levels, height;
    float base, bevel, spec;
} material_t;

static const material_t k_mat[M_COUNT] = {
    [M_BRASS] = {P_BRASS, 7, 1, 0.50f, 0.36f, 0.30f},
    [M_BRONZE] = {P_BRONZE, 7, 1, 0.52f, 0.36f, 0.26f},
    [M_BRASS_GROOVE] = {P_BRASS, 7, 1, 0.24f, 0.0f, 0.0f},
    [M_BRONZE_GROOVE] = {P_BRONZE, 7, 1, 0.24f, 0.0f, 0.0f},
    [M_PINION] = {P_STEEL, 5, 2, 0.46f, 0.42f, 0.16f},
    [M_COLLAR] = {P_BRASS, 7, 3, 0.66f, 0.34f, 0.10f},
    [M_ARBOR] = {P_STEEL, 5, 4, 0.66f, 0.40f, 0.0f},
    [M_ANCHOR] = {P_STEEL, 5, 1, 0.54f, 0.42f, 0.0f},
    [M_FRAME] = {P_BRONZE, 7, 1, 0.27f, 0.42f, 0.0f},
    [M_BRIDGE] = {P_BRONZE, 7, 1, 0.40f, 0.50f, 0.0f},
    [M_CHATON] = {P_BRASS, 7, 3, 0.64f, 0.40f, 0.0f},
    [M_RUBY] = {P_RUBY, 3, 4, 0.45f, 0.50f, 0.0f},
    [M_BLUED] = {P_BLUED, 3, 4, 0.45f, 0.50f, 0.0f},
    [M_SLOT] = {P_BLUED, 3, 3, 0.0f, 0.0f, 0.0f},
    [M_WINDOW] = {P_WINDOW, 3, 1, 0.50f, 0.0f, 0.0f},
    [M_ENAMEL] = {P_ENAMEL, 3, 2, 0.50f, 0.60f, 0.0f},
    [M_BEZEL] = {P_BRASS, 7, 3, 0.52f, 0.40f, 0.0f},
    [M_STUD] = {P_BRASS, 7, 4, 0.60f, 0.45f, 0.0f},
};

// Towards the light (top left), normalised.
#define LX -0.6f
#define LY -0.8f

// --- Small helpers ----------------------------------------------------------------------------------------------------

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// atan2 within about 0.002 rad: plenty for teeth a few pixels wide, and much cheaper than atan2f on the board.
static float fast_atan2(float y, float x)
{
    const float ax = fabsf(x), ay = fabsf(y);
    const float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx == 0) {
        return 0;
    }
    const float a = mn / mx, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) {
        r = 1.57079637f - r;
    }
    if (x < 0) {
        r = PI_F - r;
    }
    return y < 0 ? -r : r;
}

// Distance from p to the segment a-b.
static float seg_dist(float px, float py, float ax, float ay, float bx, float by)
{
    const float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    float u = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0;
    u = clampf(u, 0, 1);
    const float ex = px - ax - u * dx, ey = py - ay - u * dy;
    return sqrtf(ex * ex + ey * ey);
}

// Signed distance to a rounded rectangle (negative inside).
static float rrect_dist(float px, float py, float x0, float y0, float x1, float y1, float r)
{
    const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    const float qx = fabsf(px - cx) - ((x1 - x0) / 2 - r), qy = fabsf(py - cy) - ((y1 - y0) / 2 - r);
    const float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    const float in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

// A screw head: blued, with a slot at `angle`.
static uint8_t screw(float px, float py, float cx, float cy, float r, float angle)
{
    const float dx = px - cx, dy = py - cy;
    if (dx * dx + dy * dy > r * r) {
        return M_NONE;
    }
    const float across = fabsf(-sinf(angle) * dx + cosf(angle) * dy);
    return across < r * 0.22f ? M_SLOT : M_BLUED;
}

// A jewel in its polished ring.
static uint8_t jewel(float px, float py, float cx, float cy, float r)
{
    const float d2 = (px - cx) * (px - cx) + (py - cy) * (py - cy);
    if (d2 <= r * r * 0.36f) {
        return M_RUBY;
    }
    return d2 <= r * r ? M_CHATON : M_NONE;
}

// --- The shading pass ---------------------------------------------------------------------------------------------
// An image is drawn in two steps: a material per art pixel (supersampled, hard edges), then a shade per pixel from
// its material, its edges (bevels lit from the top left), what lies higher above it (cast shadows) and, for wheels,
// circular graining with a highlight that stays put while the wheel turns. Materials are produced row by row into a
// ring of five rows, so no image-sized scratch memory is needed.

typedef void (*fill_row_fn)(const void *ctx, int y, uint8_t *row, int w);

typedef struct {
    bool wheel;    // graining and a fixed highlight around (cx, cy)
    float cx, cy;  // art pixels
    float radius;
    bool cast;     // transparent pixels next to the image's parts become cast shadow (bridgework, timeplate)
} shade_opts_t;

static uint8_t height_of(uint8_t m)
{
    return m ? k_mat[m].height : 0;
}

static uint8_t shade(const uint8_t *const rows[5], int x, int y, int w, const shade_opts_t *o)
{
    const uint8_t m = rows[2][x];
    if (!m) {
        if (o->cast) {
            const bool near1 = x >= 1 && rows[1][x - 1];
            const bool near2 = x >= 2 && rows[0][x - 2];
            if (near1 || near2 || (x >= 1 && rows[0][x - 1]) || (x >= 2 && rows[1][x - 2])) {
                return P_SHADOW;
            }
        }
        return 0;
    }
    const material_t *mt = &k_mat[m];
    const int h = mt->height;
    float nx = 0, ny = 0, weight = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            const int qx = x + dx;
            const uint8_t q = qx >= 0 && qx < w ? rows[2 + dy][qx] : 0;
            if ((dx || dy) && height_of(q) < h) {
                nx += (float)dx;
                ny += (float)dy;
                weight = 1;
            }
        }
    }
    if (weight == 0) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                const int qx = x + dx;
                if ((dx == 2 || dx == -2 || dy == 2 || dy == -2)) {
                    const uint8_t q = qx >= 0 && qx < w ? rows[2 + dy][qx] : 0;
                    if (height_of(q) < h) {
                        nx += (float)dx;
                        ny += (float)dy;
                        weight = 0.5f;
                    }
                }
            }
        }
    }
    float v = mt->base;
    const float len = sqrtf(nx * nx + ny * ny);
    if (len > 0) {
        v += mt->bevel * weight * (nx * LX + ny * LY) / len;
    }
    // Cast shadows: something higher just up and left of this pixel.
    const uint8_t ul1 = x >= 1 ? rows[1][x - 1] : 0, ul2 = x >= 2 ? rows[0][x - 2] : 0;
    if (height_of(ul1) > h) {
        v -= 0.24f;
    } else if (height_of(ul2) > h) {
        v -= 0.12f;
    }
    if (o->wheel && mt->spec > 0) {
        const float dx = x + 0.5f - o->cx, dy = y + 0.5f - o->cy;
        const float r = sqrtf(dx * dx + dy * dy);
        // Circular graining, and the bright streak a turned surface shows along the light's axis.
        v += 0.035f * sinf(r * 1.9f);
        if (r > 0.5f) {
            const float c = (dx * LX + dy * LY) / r; // cos of the angle to the light's direction
            const float c2 = c * c;
            v += mt->spec * c2 * c2 * c2 * (0.35f + 0.65f * r / o->radius) * (c > 0 ? 1.0f : 0.55f);
        }
    }
    if (m == M_BRIDGE || m == M_FRAME) { // frosting: a fine, fixed stipple
        const unsigned hsh = (unsigned)(x * 73856093) ^ (unsigned)(y * 19349663);
        v += ((hsh >> 7) & 3) == 0 ? 0.06f : 0.0f;
    }
    int level = (int)floorf(clampf(v, 0, 0.999f) * mt->levels);
    return (uint8_t)(mt->start + level);
}

static void render_image(uint8_t *out, int w, int h, fill_row_fn fill, const void *ctx, const shade_opts_t *o,
                         uint8_t *ring)
{
    // rows[k] holds image row y - 2 + k (zeros outside the image); the five buffers rotate as y advances.
    uint8_t *rows[5];
    for (int i = 0; i < 5; i++) {
        rows[i] = ring + (size_t)i * w;
        memset(rows[i], 0, (size_t)w);
    }
    for (int i = 0; i < 3 && i < h; i++) {
        fill(ctx, i, rows[2 + i], w);
    }
    for (int y = 0; y < h; y++) {
        if (y > 0) {
            uint8_t *first = rows[0];
            memmove(rows, rows + 1, 4 * sizeof(rows[0]));
            rows[4] = first;
            if (y + 2 < h) {
                fill(ctx, y + 2, rows[4], w);
            } else {
                memset(rows[4], 0, (size_t)w);
            }
        }
        const uint8_t *const view[5] = {rows[0], rows[1], rows[2], rows[3], rows[4]};
        for (int x = 0; x < w; x++) {
            out[(size_t)y * w + x] = shade(view, x, y, w, o);
        }
    }
}

// Picks a pixel's material from its samples: the highest material that, with everything above it, covers at least
// half of the pixel. Hard edges (pixel art), placed where an antialiased edge would be at its midpoint.
static uint8_t majority(const uint8_t *samples, int n)
{
    int count[M_COUNT] = {0};
    for (int i = 0; i < n; i++) {
        count[samples[i]]++;
    }
    int covered = 0;
    for (int hgt = 4; hgt >= 1; hgt--) {
        uint8_t best = M_NONE;
        int best_n = 0;
        for (int m = 1; m < M_COUNT; m++) {
            if (k_mat[m].height == hgt && count[m] > 0) {
                covered += count[m];
                if (count[m] > best_n) {
                    best_n = count[m];
                    best = (uint8_t)m;
                }
            }
        }
        if (best && 2 * covered >= n) {
            return best;
        }
    }
    return M_NONE;
}

#define SS 3 // samples per art pixel side
#define SAMPLE_DR 0.5f // how far a sample lies from its pixel's centre, at most (sqrt(2) / 3, rounded up)

// --- Wheels -----------------------------------------------------------------------------------------------------

typedef struct {
    int teeth;
    float x, y;       // centre on the design screen (pixels)
    uint8_t metal;    // M_BRASS or M_BRONZE
    uint8_t spokes;   // crossings
    float curve;      // how far the crossings sweep (radians from hub to rim)
    uint8_t pinion;   // leaves of the steel pinion on the same arbor (0: none)
    bool ratchet;     // escape-wheel teeth
    float r_pitch;    // art pixels
    float r_tip, r_root, r_rim, r_hub, spoke_w, p_tip, p_root, r_collar, r_arbor;
    float theta0;     // rotation of frame 0
} wheel_t;

// Circular pitch of the trains: 7.5 art pixels (15 on screen) per tooth, so all wheels of a train mesh.
#define PITCH 7.5f

typedef struct {
    int teeth;
    int parent;      // meshes with this wheel (-1: placed at x, y)
    float x, y;      // screen, when parent < 0
    float mesh_deg;  // direction from the parent's centre, when parent >= 0 (0 = right, 90 = down)
    uint8_t metal, spokes, pinion;
    float curve;
    bool ratchet;    // the escape wheel
} wheel_def_t;

static const wheel_def_t k_wheels[OFFICE_SKELETON_GEARS] = {
    // The left train.
    {48, -1, 140, 190, 0, M_BRONZE, 6, 10, 0.55f, false},
    {20, 0, 0, 0, 100, M_BRASS, 4, 8, 0, false},
    {30, 1, 0, 0, 40, M_BRASS, 5, 0, -0.5f, false},
    // The right train.
    {40, -1, 880, 420, 0, M_BRONZE, 5, 9, 0.5f, false},
    {24, 3, 0, 0, -80, M_BRASS, 4, 8, 0, false},
    {36, 4, 0, 0, -130, M_BRONZE, 6, 10, -0.45f, false},
    // The escape wheel, under the plaque.
    {15, -1, 512, 542, 0, M_BRASS, 5, 0, 0, true},
};
#define ESCAPE (OFFICE_SKELETON_GEARS - 1)
#define ESCAPE_TIP 26.0f // art pixels

static void wheel_setup(wheel_t *wh, const wheel_def_t *d, const wheel_t *parent)
{
    memset(wh, 0, sizeof(*wh));
    wh->teeth = d->teeth;
    wh->metal = d->metal;
    wh->spokes = d->spokes;
    wh->curve = d->curve;
    wh->pinion = d->pinion;
    wh->ratchet = d->ratchet;
    if (wh->ratchet) {
        wh->r_tip = ESCAPE_TIP;
        wh->r_root = ESCAPE_TIP - 7;
        wh->r_pitch = ESCAPE_TIP - 3;
    } else {
        const float module = PITCH / PI_F;
        wh->r_pitch = d->teeth * PITCH / TWO_PI;
        wh->r_tip = wh->r_pitch + module * 0.95f;
        wh->r_root = wh->r_pitch - module * 1.2f;
    }
    wh->r_rim = wh->r_root - (3.5f + wh->r_pitch * 0.09f);
    wh->r_hub = 6.0f + wh->r_pitch * 0.15f;
    wh->spoke_w = 2.0f + wh->r_pitch * 0.045f;
    wh->r_collar = 4.0f + wh->r_pitch * 0.05f;
    wh->r_arbor = 2.2f;
    if (wh->pinion) {
        wh->p_tip = wh->r_hub + 2.5f + wh->r_pitch * 0.03f;
        wh->p_root = wh->p_tip - 2.6f;
    }
    if (parent) {
        const float dist = parent->r_pitch + wh->r_pitch; // art pixels
        const float a = d->mesh_deg * PI_F / 180;
        wh->x = parent->x + dist * SC * cosf(a);
        wh->y = parent->y + dist * SC * sinf(a);
        // Mesh: where the parent has a tooth centred on the line between the centres, this wheel has a gap.
        const float pitch_p = TWO_PI / parent->teeth, pitch_w = TWO_PI / wh->teeth;
        float up = (a - parent->theta0) / pitch_p;
        up -= floorf(up);
        wh->theta0 = a + PI_F - (0.5f - up) * pitch_w;
    } else {
        wh->x = d->x;
        wh->y = d->y;
        wh->theta0 = 0;
    }
}

// Tooth-local phase in [0, 1) of angle a (radians, wheel frame) for n teeth, and its distance from a tooth centre.
static float tooth_dist(float a, int n)
{
    float u = a * n / TWO_PI;
    u -= floorf(u);
    return u < 0.5f ? u : 1 - u;
}

static uint8_t wheel_material(const wheel_t *wh, float r, float a)
{
    if (r <= wh->r_arbor) {
        return M_RUBY; // the front pivot runs in a jewel
    }
    if (r <= wh->r_collar) {
        return M_CHATON;
    }
    if (wh->pinion && r <= wh->p_tip) {
        if (r <= wh->p_root) {
            return M_PINION;
        }
        const float k = (r - wh->p_root) / (wh->p_tip - wh->p_root);
        if (tooth_dist(a, wh->pinion) < 0.30f - 0.12f * k) {
            return M_PINION;
        }
    }
    if (r > wh->r_tip) {
        return M_NONE;
    }
    if (r > wh->r_root) { // teeth
        const float k = (r - wh->r_root) / (wh->r_tip - wh->r_root);
        if (wh->ratchet) {
            float u = a * wh->teeth / TWO_PI;
            u -= floorf(u);
            return u < 0.62f * (1 - k) + 0.07f ? wh->metal : M_NONE; // a radial face, a long sloped back
        }
        float half = 0.30f - 0.08f * k;
        if (k > 0.82f) {
            half *= 1 - (k - 0.82f) * 1.6f; // rounded tips
        }
        return tooth_dist(a, wh->teeth) < half ? wh->metal : M_NONE;
    }
    if (r >= wh->r_rim || r <= wh->r_hub) {
        // A turned groove around the middle of the rim and around the hub.
        const float groove = (wh->r_rim + wh->r_root) / 2, hub_groove = wh->r_hub - 2.0f;
        if (fabsf(r - groove) < 0.55f || (wh->r_hub > 9 && fabsf(r - hub_groove) < 0.5f)) {
            return wh->metal == M_BRASS ? M_BRASS_GROOVE : M_BRONZE_GROOVE;
        }
        return wh->metal;
    }
    // Crossings: curved spokes, a little wider at both ends where they join hub and rim.
    const float t = (r - wh->r_hub) / (wh->r_rim - wh->r_hub);
    const float pitch = TWO_PI / wh->spokes;
    float s = (a - wh->curve * t) / pitch;
    s -= floorf(s);
    const float across = (s < 0.5f ? s : 1 - s) * pitch * r;
    float half = wh->spoke_w * (1.2f - 0.3f * t);
    const float near_hub = r - wh->r_hub, near_rim = wh->r_rim - r;
    if (near_hub < 3) {
        half += (3 - near_hub) * 0.9f;
    }
    if (near_rim < 3) {
        half += (3 - near_rim) * 0.9f;
    }
    return across < half ? wh->metal : M_NONE;
}

typedef struct {
    const wheel_t *wh;
    float theta; // rotation of this frame
    float c;     // centre in image coordinates
} wheel_ctx_t;

// One row of a wheel: polar coordinates once per pixel, sub-samples by a first-order step from there.
static void wheel_row(const void *ctx, int y, uint8_t *row, int w)
{
    const wheel_ctx_t *wc = ctx;
    const float py = y + 0.5f - wc->c;
    for (int x = 0; x < w; x++) {
        const float px = x + 0.5f - wc->c;
        const float r2 = px * px + py * py;
        const float r = sqrtf(r2);
        // Sub-samples lie within SAMPLE_DR of r: where nothing changes over that band, one look is enough.
        const wheel_t *wh = wc->wh;
        if (r > wh->r_tip + SAMPLE_DR) {
            row[x] = M_NONE;
            continue;
        }
        const float groove = (wh->r_rim + wh->r_root) / 2;
        if (r - SAMPLE_DR > wh->r_rim && r + SAMPLE_DR < wh->r_root && fabsf(r - groove) > 0.55f + SAMPLE_DR) {
            row[x] = wh->metal; // the plain rim
            continue;
        }
        const float a = fast_atan2(py, px) - wc->theta;
        uint8_t samples[SS * SS];
        int n = 0;
        for (int sy = 0; sy < SS; sy++) {
            for (int sx = 0; sx < SS; sx++) {
                const float dx = (sx + 0.5f) / SS - 0.5f, dy = (sy + 0.5f) / SS - 0.5f;
                float rr, aa;
                if (r > 2.5f) {
                    rr = r + (dx * px + dy * py) / r;
                    aa = a + (dy * px - dx * py) / r2;
                } else {
                    const float qx = px + dx, qy = py + dy;
                    rr = sqrtf(qx * qx + qy * qy);
                    aa = fast_atan2(qy, qx) - wc->theta;
                }
                samples[n++] = wheel_material(wc->wh, rr, aa);
            }
        }
        row[x] = majority(samples, n);
    }
}

// --- Generic sampled shapes ---------------------------------------------------------------------------------------

typedef uint8_t (*sample_fn)(const void *ctx, float x, float y);

typedef struct {
    sample_fn sample;
    const void *ctx;
} sampled_t;

static void sampled_row(const void *ctx, int y, uint8_t *row, int w)
{
    const sampled_t *s = ctx;
    for (int x = 0; x < w; x++) {
        uint8_t samples[SS * SS];
        int n = 0;
        for (int sy = 0; sy < SS; sy++) {
            for (int sx = 0; sx < SS; sx++) {
                samples[n++] = s->sample(s->ctx, x + (sx + 0.5f) / SS, y + (sy + 0.5f) / SS);
            }
        }
        row[x] = majority(samples, n);
    }
}

// --- The anchor -----------------------------------------------------------------------------------------------------
// Pivot above the escape wheel; two arms reach out and down, ending in ruby pallets on the wheel's tooth tips.

#define ANCHOR_W 72
#define ANCHOR_H 60
#define ANCHOR_PX 36.0f // pivot in the image
#define ANCHOR_PY 30.0f
#define ANCHOR_TILT 0.06f // radians either side

typedef struct {
    float tilt;
    float drop; // pivot to escape wheel centre, art pixels
} anchor_ctx_t;

static uint8_t anchor_sample(const void *ctx, float x, float y)
{
    const anchor_ctx_t *ac = ctx;
    // Into the anchor's own frame (pivot at the origin, untilted).
    const float dx = x - ANCHOR_PX, dy = y - ANCHOR_PY;
    const float c = cosf(-ac->tilt), s = sinf(-ac->tilt);
    const float ax = c * dx - s * dy, ay = s * dx + c * dy;
    const float r2 = ax * ax + ay * ay;
    if (r2 <= 2.2f * 2.2f) {
        return M_ARBOR;
    }
    if (r2 <= 5.0f * 5.0f) {
        return M_COLLAR;
    }
    // The pallets sit on the wheel's tip circle, 30 degrees either side of the top.
    const float px = ESCAPE_TIP * 0.5f, py = ac->drop - ESCAPE_TIP * 0.866f;
    const float ex = 25.0f, ey = 3.0f; // the elbows
    const float fax = fabsf(ax);
    if (seg_dist(fax, ay, px, py - 4.0f, px - 1.0f, py + 1.5f) < 2.2f) {
        return M_RUBY;
    }
    if (seg_dist(fax, ay, 0, 0, ex, ey) < 3.0f || seg_dist(fax, ay, ex, ey, px, py - 3.5f) < 2.6f ||
        r2 <= 8.0f * 8.0f) {
        return M_ANCHOR;
    }
    return M_NONE;
}

// --- The works: the frame behind the wheels and the escapement's cock in front -------------------------------------
// Built from capsules, discs, circular and elliptical bands, jewels and screws, in design-screen pixels.

#define WORKS_MAX 12
typedef struct {
    uint8_t material;
    float ox, oy; // the image's top left on the design screen
    int segs;
    float s[WORKS_MAX][5]; // x0, y0, x1, y1, half width
    int discs;
    float d[WORKS_MAX][3]; // x, y, r
    int rings;
    float r[WORKS_MAX][4]; // x, y, radius, half width
    int arches;
    float a[2][5]; // x, y, rx, ry, half width
    int jewels;
    float j[WORKS_MAX][2];
    int screws;
    float sc[WORKS_MAX][3]; // x, y, slot angle
} works_t;

#define JEWEL 9.0f // screen radius of a jewel's ring
#define SCREW 7.5f // screen radius of a screw head

static uint8_t works_sample(const void *ctx, float x, float y)
{
    const works_t *w = ctx;
    const float sx = w->ox + x * SC, sy = w->oy + y * SC;
    for (int i = 0; i < w->screws; i++) {
        const uint8_t m = screw(sx, sy, w->sc[i][0], w->sc[i][1], SCREW, w->sc[i][2]);
        if (m) {
            return m;
        }
    }
    for (int i = 0; i < w->jewels; i++) {
        const uint8_t m = jewel(sx, sy, w->j[i][0], w->j[i][1], JEWEL);
        if (m) {
            return m;
        }
    }
    for (int i = 0; i < w->segs; i++) {
        if (seg_dist(sx, sy, w->s[i][0], w->s[i][1], w->s[i][2], w->s[i][3]) < w->s[i][4]) {
            return w->material;
        }
    }
    for (int i = 0; i < w->discs; i++) {
        const float dx = sx - w->d[i][0], dy = sy - w->d[i][1];
        if (dx * dx + dy * dy < w->d[i][2] * w->d[i][2]) {
            return w->material;
        }
    }
    for (int i = 0; i < w->rings; i++) {
        const float dx = sx - w->r[i][0], dy = sy - w->r[i][1];
        if (fabsf(sqrtf(dx * dx + dy * dy) - w->r[i][2]) < w->r[i][3]) {
            return w->material;
        }
    }
    for (int i = 0; i < w->arches; i++) {
        // Distance to an ellipse, to first order: |q - 1| / |grad q|.
        const float dx = sx - w->a[i][0], dy = sy - w->a[i][1], rx = w->a[i][2], ry = w->a[i][3];
        const float q = sqrtf(dx * dx / (rx * rx) + dy * dy / (ry * ry));
        if (q > 0) {
            const float gx = dx / (rx * rx * q), gy = dy / (ry * ry * q);
            if (fabsf(q - 1) / sqrtf(gx * gx + gy * gy) < w->a[i][4]) {
                return w->material;
            }
        }
    }
    return M_NONE;
}

static void works_seg(works_t *w, float x0, float y0, float x1, float y1, float half)
{
    if (w->segs < WORKS_MAX) {
        const float s[5] = {x0, y0, x1, y1, half};
        memcpy(w->s[w->segs++], s, sizeof(s));
    }
}

static void works_disc(works_t *w, float x, float y, float r)
{
    if (w->discs < WORKS_MAX) {
        const float d[3] = {x, y, r};
        memcpy(w->d[w->discs++], d, sizeof(d));
    }
}

static void works_screw(works_t *w, float x, float y, float angle)
{
    if (w->screws < WORKS_MAX) {
        const float s[3] = {x, y, angle};
        memcpy(w->sc[w->screws++], s, sizeof(s));
    }
}

static void works_jewel(works_t *w, float x, float y)
{
    if (w->jewels < WORKS_MAX) {
        w->j[w->jewels][0] = x;
        w->j[w->jewels++][1] = y;
    }
}

// One row of the works. Most of a full-screen image is empty: only pixels near a part's bounds are sampled.
#define SPANS_MAX (4 * WORKS_MAX + 4)
static void works_row(const void *ctx, int y, uint8_t *row, int w)
{
    const works_t *wk = ctx;
    const float m = 2.0f * SC; // margin, screen pixels
    const float y0 = wk->oy + y * SC - m, y1 = wk->oy + (y + 1) * SC + m;
    float spans[SPANS_MAX][2];
    int n = 0;
#define SPAN(cy0, cy1, cx0, cx1)                                                                                       \
    do {                                                                                                               \
        if ((cy1) >= y0 && (cy0) <= y1 && n < SPANS_MAX) {                                                             \
            spans[n][0] = (cx0) - m;                                                                                   \
            spans[n++][1] = (cx1) + m;                                                                                 \
        }                                                                                                              \
    } while (0)
    for (int i = 0; i < wk->segs; i++) {
        const float *s = wk->s[i];
        SPAN(fminf(s[1], s[3]) - s[4], fmaxf(s[1], s[3]) + s[4], fminf(s[0], s[2]) - s[4], fmaxf(s[0], s[2]) + s[4]);
    }
    for (int i = 0; i < wk->discs; i++) {
        SPAN(wk->d[i][1] - wk->d[i][2], wk->d[i][1] + wk->d[i][2], wk->d[i][0] - wk->d[i][2],
             wk->d[i][0] + wk->d[i][2]);
    }
    for (int i = 0; i < wk->rings; i++) {
        const float r = wk->r[i][2] + wk->r[i][3];
        SPAN(wk->r[i][1] - r, wk->r[i][1] + r, wk->r[i][0] - r, wk->r[i][0] + r);
    }
    for (int i = 0; i < wk->screws; i++) {
        SPAN(wk->sc[i][1] - SCREW, wk->sc[i][1] + SCREW, wk->sc[i][0] - SCREW, wk->sc[i][0] + SCREW);
    }
    for (int i = 0; i < wk->jewels; i++) {
        SPAN(wk->j[i][1] - JEWEL, wk->j[i][1] + JEWEL, wk->j[i][0] - JEWEL, wk->j[i][0] + JEWEL);
    }
    for (int i = 0; i < wk->arches; i++) {
        // The band between the inner and the outer ellipse crosses this row in (at most) two stretches.
        const float *a = wk->a[i];
        const float ro_x = a[2] + a[4], ro_y = a[3] + a[4], ri_x = a[2] - a[4], ri_y = a[3] - a[4];
        const float d0 = fabsf(y0 - a[1]), d1 = fabsf(y1 - a[1]);
        const float dnear = (y0 - a[1]) * (y1 - a[1]) <= 0 ? 0 : fminf(d0, d1), dfar = fmaxf(d0, d1);
        if (dnear >= ro_y) {
            continue;
        }
        const float xo = ro_x * sqrtf(1 - dnear * dnear / (ro_y * ro_y));
        const float xi = dfar < ri_y ? ri_x * sqrtf(1 - dfar * dfar / (ri_y * ri_y)) : 0;
        SPAN(y0, y1, a[0] - xo, a[0] - xi);
        SPAN(y0, y1, a[0] + xi, a[0] + xo);
    }
#undef SPAN
    for (int x = 0; x < w; x++) {
        const float sx0 = wk->ox + x * SC, sx1 = sx0 + SC;
        bool near = false;
        for (int i = 0; i < n && !near; i++) {
            near = sx1 >= spans[i][0] && sx0 <= spans[i][1];
        }
        if (!near) {
            row[x] = M_NONE;
            continue;
        }
        uint8_t samples[SS * SS];
        int k = 0;
        for (int sy = 0; sy < SS; sy++) {
            for (int sx = 0; sx < SS; sx++) {
                samples[k++] = works_sample(wk, x + (sx + 0.5f) / SS, y + (sy + 0.5f) / SS);
            }
        }
        row[x] = majority(samples, k);
    }
}

// --- The timeplate ----------------------------------------------------------------------------------------------

typedef struct {
    float wx0[4], wx1[4], wy0, wy1; // digit windows (art, plate image)
    float colon_x, colon_y[2];
} plate_ctx_t;

static uint8_t plate_sample(const void *ctx, float x, float y)
{
    const plate_ctx_t *p = ctx;
    // Corner screws, the colon's studs.
    static const float k_screw[4][2] = {{11, 11}, {PLATE_W - 11, 11}, {11, PLATE_MAIN_H - 11},
                                        {PLATE_W - 11, PLATE_MAIN_H - 11}};
    for (int i = 0; i < 4; i++) {
        const uint8_t m = screw(x, y, k_screw[i][0], k_screw[i][1], 4.5f, 0.6f + i * 0.9f);
        if (m) {
            return m;
        }
    }
    for (int i = 0; i < 2; i++) {
        const float dx = x - p->colon_x, dy = y - p->colon_y[i];
        if (dx * dx + dy * dy <= 4.6f * 4.6f) {
            return M_STUD;
        }
    }
    const float d = rrect_dist(x, y, 0.5f, 0.5f, PLATE_W - 0.5f, PLATE_MAIN_H - 0.5f, 9);
    if (d <= 0) {
        if (d > -4.5f) {
            return M_BEZEL;
        }
        for (int i = 0; i < 4; i++) {
            if (x >= p->wx0[i] && x < p->wx1[i] && y >= p->wy0 && y < p->wy1) {
                return M_WINDOW;
            }
        }
        return M_ENAMEL;
    }
    // The plaque under the plate, hung from two posts.
    for (int i = 0; i < 2; i++) {
        const float post = i ? PLATE_W - 62.0f : 62.0f;
        if (fabsf(x - post) < 4 && y > PLATE_MAIN_H - 2 && y < PLAQUE_Y0 + 2) {
            return M_BEZEL;
        }
    }
    const float dq = rrect_dist(x, y, PLAQUE_X0, PLAQUE_Y0, PLAQUE_X1, PLAQUE_Y1, 6);
    if (dq <= 0) {
        for (int i = 0; i < 2; i++) {
            const uint8_t m = screw(x, y, i ? PLAQUE_X1 - 8.0f : PLAQUE_X0 + 8.0f, (PLAQUE_Y0 + PLAQUE_Y1) / 2.0f,
                                    3.6f, 0.3f + i * 1.2f);
            if (m) {
                return m;
            }
        }
        return dq > -3.0f ? M_BEZEL : M_ENAMEL;
    }
    return M_NONE;
}

// --- Background -------------------------------------------------------------------------------------------------

void office_skeleton_background(void *ctx, gfx_color_t *dst, int x, int y, int w)
{
    const office_skeleton_t *k = ctx;
    for (int i = 0; i < w; i++) {
        dst[i] = BG;
    }
    for (int g = 0; g < OFFICE_SKELETON_GEARS; g++) {
        const office_skeleton_shadow_t *s = &k->shadow[g];
        const float dy = y + 0.5f - s->y;
        if (fabsf(dy) >= s->r) {
            continue;
        }
        const float half = sqrtf(s->r * s->r - dy * dy);
        int x0 = (int)ceilf(s->x - half - 0.5f) - x, x1 = (int)floorf(s->x + half - 0.5f) - x;
        x0 = x0 < 0 ? 0 : x0;
        x1 = x1 >= w ? w - 1 : x1;
        for (int i = x0; i <= x1; i++) {
            dst[i] = BG_SHADOW;
        }
    }
    const gfx_rect_t p = k->plate_shadow;
    if (y >= p.y && y < p.y + p.h) {
        int x0 = p.x - x, x1 = p.x + p.w - x;
        x0 = x0 < 0 ? 0 : x0;
        x1 = x1 > w ? w : x1;
        for (int i = x0; i < x1; i++) {
            dst[i] = BG_SHADOW;
        }
    }
}

// --- Setup ----------------------------------------------------------------------------------------------------------

static gfx_color_t rgb(const uint8_t c[3])
{
    return GFX_RGB(c[0], c[1], c[2]);
}

// Ink levels 1..OFFICE_INK_LEVELS blend from `from` to `to`.
static void ramp(gfx_color_t *colors, const uint8_t from[3], const uint8_t to[3])
{
    colors[0] = 0;
    for (int i = 1; i <= OFFICE_INK_LEVELS; i++) {
        colors[i] = GFX_RGB(from[0] + (to[0] - from[0]) * i / OFFICE_INK_LEVELS,
                            from[1] + (to[1] - from[1]) * i / OFFICE_INK_LEVELS,
                            from[2] + (to[2] - from[2]) * i / OFFICE_INK_LEVELS);
    }
}

static void place(gfx_sprite_t *sp, const gfx_image_t *img, const gfx_palette_t *pal, int scale, int x, int y)
{
    *sp = (gfx_sprite_t){.image = img, .palette = pal, .x = x, .y = y, .scale = (uint8_t)scale,
                         .flags = GFX_SPRITE_HIDDEN};
}

typedef struct {
    uint8_t *mem;
    size_t size, used;
} arena_t;

static uint8_t *take(arena_t *a, size_t n)
{
    if (a->used + n > a->size) {
        return NULL;
    }
    uint8_t *p = a->mem + a->used;
    a->used += n;
    return p;
}

// The numerals with a little cast shadow (index 8), as if applied on the window's floor.
static void numeral(char ch, uint8_t *px, int cw, int chh)
{
    memset(px, 0, (size_t)cw * chh);
    const float glyph_w = 6 * k_num.unit + k_num.stroke;
    office_font_draw(ch, &k_num, px, cw, chh, (cw - glyph_w) / 2, PAD);
    for (int y = chh - 1; y >= 1; y--) {
        for (int x = cw - 1; x >= 1; x--) {
            uint8_t *p = &px[y * cw + x];
            if (*p == 0 && px[(y - 1) * cw + x - 1] >= 4) {
                *p = OFFICE_INK_LEVELS + 1;
            }
        }
    }
}

bool office_skeleton_init(office_skeleton_t *k, gfx_scene_t *scene, gfx_rect_t screen, uint8_t *mem,
                          size_t mem_size, int z)
{
    memset(k, 0, sizeof(*k));
    k->screen = screen;
    if (screen.w < DESIGN_W || screen.h < DESIGN_H || !mem) {
        return false;
    }
    const int ox = screen.x + (screen.w - DESIGN_W) / 2, oy = screen.y + (screen.h - DESIGN_H) / 2;
    arena_t arena = {mem, mem_size, 0};

    for (int i = 0; i < P_COUNT; i++) {
        k->colors[i] = rgb(k_rgb[i]);
    }
    k->colors[0] = 0;
    k->pal = (gfx_palette_t){k->colors, P_COUNT};
    ramp(k->ink, k_rgb[P_WINDOW + 1], k_ivory);
    k->ink[OFFICE_INK_LEVELS + 1] = rgb(k_ink_shadow);
    k->ink_pal = (gfx_palette_t){k->ink, OFFICE_INK_LEVELS + 2};
    ramp(k->date_ink, k_rgb[P_ENAMEL + 1], k_date_ink);
    k->date_pal = (gfx_palette_t){k->date_ink, OFFICE_INK_LEVELS + 1};

    // Ring rows for the shading pass, at the end of the arena (freed again below).
    const size_t ring_size = (size_t)5 * (DESIGN_W / SC);
    uint8_t *ring = mem_size >= ring_size ? mem + mem_size - ring_size : NULL;
    arena.size = ring ? mem_size - ring_size : 0;

    // Wheels: every quarter-tooth position.
    wheel_t wheels[OFFICE_SKELETON_GEARS];
    for (int g = 0; g < OFFICE_SKELETON_GEARS; g++) {
        const wheel_def_t *d = &k_wheels[g];
        wheel_setup(&wheels[g], d, d->parent >= 0 ? &wheels[d->parent] : NULL);
        k->dir[g] = (int8_t)(d->parent >= 0 ? -k->dir[d->parent] : 1);
        const wheel_t *wh = &wheels[g];
        const int size = 2 * (int)ceilf(wh->r_tip + 2);
        const shade_opts_t opts = {true, size / 2.0f, size / 2.0f, wh->r_tip, false};
        for (int f = 0; f < OFFICE_SKELETON_FRAMES; f++) {
            uint8_t *px = take(&arena, (size_t)size * size);
            if (!px || !ring) {
                return false;
            }
            const wheel_ctx_t wc = {wh, wh->theta0 + f * (TWO_PI / wh->teeth) / OFFICE_SKELETON_FRAMES,
                                    size / 2.0f};
            render_image(px, size, size, wheel_row, &wc, &opts, ring);
            k->gear_img[g][f] = (gfx_image_t){(uint16_t)size, (uint16_t)size, px};
        }
        place(&k->gear[g], &k->gear_img[g][0], &k->pal, SC, ox + (int)lroundf(wh->x) - size * SC / 2,
              oy + (int)lroundf(wh->y) - size * SC / 2);
        k->shadow[g] = (office_skeleton_shadow_t){ox + wh->x + SHADOW_OFFSET, oy + wh->y + SHADOW_OFFSET,
                                                  (wh->r_root - 1) * SC};
    }

    // The anchor, tilted to one side (the sprite flips for the other).
    const wheel_t *esc = &wheels[ESCAPE];
    const anchor_ctx_t ac = {ANCHOR_TILT, ESCAPE_TIP * 1.45f};
    {
        uint8_t *px = take(&arena, ANCHOR_W * ANCHOR_H);
        if (!px) {
            return false;
        }
        const sampled_t s = {anchor_sample, &ac};
        const shade_opts_t opts = {false, 0, 0, 0, false};
        render_image(px, ANCHOR_W, ANCHOR_H, sampled_row, &s, &opts, ring);
        k->anchor_img = (gfx_image_t){ANCHOR_W, ANCHOR_H, px};
        const float pivot_y = esc->y - ac.drop * SC;
        place(&k->anchor, &k->anchor_img, &k->pal, SC, ox + (int)lroundf(esc->x - ANCHOR_PX * SC),
              oy + (int)lroundf(pivot_y - ANCHOR_PY * SC));
    }

    // The frame behind the wheels: an arch around the movement, a strut out to each arbor, a crest on top.
    const float pivot_y = esc->y - ac.drop * SC;
    {
        static works_t fr; // kept off the stack
        memset(&fr, 0, sizeof(fr));
        fr.material = M_FRAME;
        const float cx = DESIGN_W / 2.0f, cy = 372, rx = 482, ry = 342;
        const float arches[2][5] = {{cx, cy, rx, ry, 11}, {cx, cy, rx - 28, ry - 28, 3.5f}};
        memcpy(fr.a, arches, sizeof(arches));
        fr.arches = 2;
        for (int g = 0; g < ESCAPE; g++) {
            const float wx = wheels[g].x, wy = wheels[g].y;
            const float q = sqrtf((wx - cx) * (wx - cx) / (rx * rx) + (wy - cy) * (wy - cy) / (ry * ry));
            const float ax = cx + (wx - cx) / q, ay = cy + (wy - cy) / q;
            works_seg(&fr, wx, wy, ax, ay, 8);
            works_disc(&fr, ax, ay, 16);
            works_screw(&fr, ax, ay, 0.5f + g * 0.7f);
        }
        // The crest: a ring with a jewel, flanked by two small scrolls.
        const float crest[3][4] = {{cx, 30, 24, 5}, {cx - 52, 40, 12, 3.5f}, {cx + 52, 40, 12, 3.5f}};
        memcpy(fr.r, crest, sizeof(crest));
        fr.rings = 3;
        works_jewel(&fr, cx, 30);
        const int fw = DESIGN_W / SC, fh = DESIGN_H / SC;
        uint8_t *px = take(&arena, (size_t)fw * fh);
        if (!px) {
            return false;
        }
        const shade_opts_t opts = {false, 0, 0, 0, true};
        render_image(px, fw, fh, works_row, &fr, &opts, ring);
        k->frame_img = (gfx_image_t){(uint16_t)fw, (uint16_t)fh, px};
        place(&k->frame, &k->frame_img, &k->pal, SC, ox, oy);
    }

    // The escapement's cock in front: from a foot at the bottom over the escape wheel's arbor to the anchor's.
    {
        static works_t ck;
        memset(&ck, 0, sizeof(ck));
        ck.material = M_BRIDGE;
        ck.ox = esc->x - 40;
        ck.oy = 2 * floorf((pivot_y - 24) / 2); // an even screen row, so the image ends exactly at the bottom
        works_seg(&ck, esc->x, pivot_y, esc->x, esc->y, 10);
        works_disc(&ck, esc->x, pivot_y, 15);
        works_disc(&ck, esc->x, esc->y, 17);
        works_seg(&ck, esc->x, esc->y, esc->x + 96, DESIGN_H - 8, 9);
        works_disc(&ck, esc->x + 96, DESIGN_H - 8, 14);
        works_screw(&ck, esc->x + 96, DESIGN_H - 8, 2.2f);
        works_jewel(&ck, esc->x, pivot_y);
        works_jewel(&ck, esc->x, esc->y);
        const int cw = 160 / SC, chh = (DESIGN_H - (int)ck.oy) / SC;
        uint8_t *px = take(&arena, (size_t)cw * chh);
        if (!px) {
            return false;
        }
        const shade_opts_t opts = {false, 0, 0, 0, true};
        render_image(px, cw, chh, works_row, &ck, &opts, ring);
        k->cock_img = (gfx_image_t){(uint16_t)cw, (uint16_t)chh, px};
        place(&k->cock, &k->cock_img, &k->pal, SC, ox + (int)lroundf(ck.ox), oy + (int)lroundf(ck.oy));
    }

    // Numerals: tabular cells, as Classic's.
    const float glyph_w = 6 * k_num.unit + k_num.stroke;
    k->cell_w = (int)ceilf(glyph_w) + 2 * PAD;
    k->cell_h = (int)ceilf(10 * k_num.unit + k_num.stroke) + 2 * PAD;
    const size_t cell = (size_t)k->cell_w * k->cell_h;
    static const char k_cells[11] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-'};
    for (int i = 0; i < 11; i++) {
        uint8_t *px = take(&arena, cell);
        if (!px) {
            return false;
        }
        numeral(k_cells[i], px, k->cell_w, k->cell_h);
        k->glyph[i] = (gfx_image_t){(uint16_t)k->cell_w, (uint16_t)k->cell_h, px};
    }
    for (int d = 0; d < 4; d++) {
        for (int b = 0; b < 2; b++) {
            k->roll_px[d][b] = take(&arena, cell);
            if (!k->roll_px[d][b]) {
                return false;
            }
        }
    }
    // Layout: HH:MM centred at Classic's height.
    const int time_w = (4 * k->cell_w + 2 * DIGIT_GAP + COLON_GAP) * SC, time_h = k->cell_h * SC;
    const int tx = ox + (DESIGN_W - time_w) / 2, ty = oy + DESIGN_H * 42 / 100 - time_h / 2;
    int digit_x[4];
    for (int d = 0, x = tx; d < 4; d++) {
        digit_x[d] = x;
        x += (k->cell_w + (d == 1 ? COLON_GAP : DIGIT_GAP)) * SC;
        place(&k->digit[d], &k->glyph[10], &k->ink_pal, SC, digit_x[d], ty);
    }

    // The timeplate around them, windows and colon studs where the digits are.
    {
        const int plate_x = ox + PLATE_X, plate_y = oy + PLATE_Y;
        plate_ctx_t p;
        for (int d = 0; d < 4; d++) {
            p.wx0[d] = (float)(digit_x[d] - plate_x) / SC - WINDOW_MARGIN;
            p.wx1[d] = p.wx0[d] + k->cell_w + 2 * WINDOW_MARGIN;
        }
        p.wy0 = (float)(ty - plate_y) / SC - WINDOW_MARGIN;
        p.wy1 = p.wy0 + k->cell_h + 2 * WINDOW_MARGIN;
        p.colon_x = (p.wx1[1] + p.wx0[2]) / 2;
        p.colon_y[0] = p.wy0 + WINDOW_MARGIN + k->cell_h * 0.36f;
        p.colon_y[1] = p.wy0 + WINDOW_MARGIN + k->cell_h * 0.68f;
        uint8_t *px = take(&arena, (size_t)PLATE_IMG_W * PLATE_IMG_H);
        if (!px) {
            return false;
        }
        const sampled_t s = {plate_sample, &p};
        const shade_opts_t opts = {false, 0, 0, 0, true};
        render_image(px, PLATE_IMG_W, PLATE_IMG_H, sampled_row, &s, &opts, ring);
        k->plate_img = (gfx_image_t){PLATE_IMG_W, PLATE_IMG_H, px};
        place(&k->plate, &k->plate_img, &k->pal, SC, plate_x, plate_y);
        k->plate_shadow = (gfx_rect_t){plate_x + SHADOW_OFFSET, plate_y + SHADOW_OFFSET, PLATE_W * SC,
                                       PLATE_MAIN_H * SC};
        // The date: centred on the plaque.
        k->date_w = DATE_W;
        k->date_h = (int)ceilf(10 * k_date.unit + k_date.stroke) + 2 * PAD;
        k->date_px[0] = take(&arena, (size_t)k->date_w * k->date_h);
        k->date_px[1] = take(&arena, (size_t)k->date_w * k->date_h);
        if (!k->date_px[0] || !k->date_px[1]) {
            return false;
        }
        const int plaque_cy = plate_y + (PLAQUE_Y0 + PLAQUE_Y1) * SC / 2;
        place(&k->date, NULL, &k->date_pal, 1, ox + (DESIGN_W - k->date_w) / 2, plaque_cy - k->date_h / 2);
    }
    k->mem_used = arena.used;

    bool ok = gfx_scene_add(scene, &k->frame, z);
    for (int g = 0; ok && g < OFFICE_SKELETON_GEARS; g++) {
        ok = gfx_scene_add(scene, &k->gear[g], z + 1);
    }
    ok = ok && gfx_scene_add(scene, &k->anchor, z + 1) && gfx_scene_add(scene, &k->cock, z + 2) &&
         gfx_scene_add(scene, &k->plate, z + 3);
    for (int d = 0; ok && d < 4; d++) {
        ok = gfx_scene_add(scene, &k->digit[d], z + 4);
    }
    return ok && gfx_scene_add(scene, &k->date, z + 4);
}

static void set_hidden(gfx_sprite_t *sp, bool hidden)
{
    sp->flags = (uint8_t)(hidden ? sp->flags | GFX_SPRITE_HIDDEN : sp->flags & ~GFX_SPRITE_HIDDEN);
}

void office_skeleton_show(office_skeleton_t *k, bool visible)
{
    k->visible = visible;
    for (int g = 0; g < OFFICE_SKELETON_GEARS; g++) {
        set_hidden(&k->gear[g], !visible);
    }
    set_hidden(&k->frame, !visible);
    set_hidden(&k->anchor, !visible);
    set_hidden(&k->cock, !visible);
    set_hidden(&k->plate, !visible);
    for (int d = 0; d < 4; d++) {
        set_hidden(&k->digit[d], !visible);
    }
    set_hidden(&k->date, !visible);
}

// Renders the date into the buffer the screen is not showing, so the scene sees a new image pointer.
static void set_date(office_skeleton_t *k, const char *text)
{
    if (k->date.image && strcmp(text, k->date_text) == 0) {
        return;
    }
    snprintf(k->date_text, sizeof(k->date_text), "%s", text);
    const int next = k->date.shown.image == &k->date_img[0] ? 1 : 0;
    uint8_t *px = k->date_px[next];
    memset(px, 0, (size_t)k->date_w * k->date_h);
    const float w = office_font_text_width(text, &k_date);
    office_font_draw_text(text, &k_date, px, k->date_w, k->date_h, (k->date_w - w) / 2, PAD);
    k->date_img[next] = (gfx_image_t){(uint16_t)k->date_w, (uint16_t)k->date_h, px};
    k->date.image = &k->date_img[next];
}

// A digit window part way through a roll: `from` moving up and out, `to` coming in from below; `step` of 3.
static void set_roll(office_skeleton_t *k, int d, int from, int to, int step)
{
    const int next = k->digit[d].shown.image == &k->roll_img[d][0] ? 1 : 0;
    uint8_t *px = k->roll_px[d][next];
    const int cw = k->cell_w, ch = k->cell_h, shift = ch * step / 3;
    const uint8_t *a = k->glyph[from].px, *b = k->glyph[to].px;
    for (int y = 0; y < ch; y++) {
        const int sy = y + shift;
        memcpy(px + (size_t)y * cw, sy < ch ? a + (size_t)sy * cw : b + (size_t)(sy - ch) * cw, (size_t)cw);
    }
    k->roll_img[d][next] = (gfx_image_t){(uint16_t)cw, (uint16_t)ch, px};
    k->digit[d].image = &k->roll_img[d][next];
}

static void set_frames(office_skeleton_t *k, uint32_t escape, uint32_t left, uint32_t right)
{
    for (int g = 0; g < OFFICE_SKELETON_GEARS; g++) {
        const uint32_t step = g == ESCAPE ? escape : g < 3 ? left : right;
        const uint32_t f = (k->dir[g] > 0 ? step : 0u - step) % OFFICE_SKELETON_FRAMES;
        k->gear[g].image = &k->gear_img[g][f];
    }
    k->anchor.flags = (uint8_t)(escape & 1 ? k->anchor.flags | GFX_SPRITE_FLIP_X
                                           : k->anchor.flags & ~GFX_SPRITE_FLIP_X);
}

uint32_t office_skeleton_apply(office_skeleton_t *k, const struct tm *tm, uint32_t ms)
{
    char text[OFFICE_DATE_MAX];
    office_clock_date_text(tm, text);
    set_date(k, text);
    if (!tm) { // stopped: nothing moves until a time source sets the clock
        for (int d = 0; d < 4; d++) {
            k->digit[d].image = &k->glyph[10];
        }
        set_frames(k, 0, 0, 0);
        return 1000;
    }
    ms %= 1000;
    const int h = tm->tm_hour < 0 || tm->tm_hour > 23 ? 0 : tm->tm_hour;
    const int m = tm->tm_min < 0 || tm->tm_min > 59 ? 0 : tm->tm_min;
    const int s = tm->tm_sec < 0 || tm->tm_sec > 59 ? 0 : tm->tm_sec;
    const uint32_t secs = (uint32_t)(h * 3600 + m * 60 + s);
    uint32_t next = 1000 - ms;

    // On the hour: a quick whir of the whole movement (a whole number of turns of the frames: no jump after it).
    uint32_t extra = 0;
    const uint32_t into_hour = (uint32_t)s * 1000 + ms;
    const uint32_t flourish_ms = OFFICE_SKELETON_FLOURISH_STEPS * OFFICE_SKELETON_FLOURISH_MS;
    if (m == 0 && into_hour < flourish_ms) {
        extra = into_hour / OFFICE_SKELETON_FLOURISH_MS + 1;
        const uint32_t to_next = OFFICE_SKELETON_FLOURISH_MS - into_hour % OFFICE_SKELETON_FLOURISH_MS;
        next = to_next < next ? to_next : next;
        if (extra >= OFFICE_SKELETON_FLOURISH_STEPS) {
            extra = OFFICE_SKELETON_FLOURISH_STEPS;
        }
    }
    // The right train steps 7 s past each quarter minute, so it never steps together with the left one.
    set_frames(k, secs + extra, secs / 5 + extra, (secs + 8) / 15 + extra);

    // The time; at a minute change the changed digits roll up.
    const int now_d[4] = {h / 10, h % 10, m / 10, m % 10};
    const bool rolling = s == 0 && ms < 2 * OFFICE_SKELETON_TICK_FRAME_MS;
    int prev_d[4];
    if (rolling) {
        const int pm = (h * 60 + m + 24 * 60 - 1) % (24 * 60);
        prev_d[0] = pm / 60 / 10;
        prev_d[1] = pm / 60 % 10;
        prev_d[2] = pm % 60 / 10;
        prev_d[3] = pm % 60 % 10;
        const uint32_t to_next = OFFICE_SKELETON_TICK_FRAME_MS - ms % OFFICE_SKELETON_TICK_FRAME_MS;
        next = to_next < next ? to_next : next;
    }
    for (int d = 0; d < 4; d++) {
        if (rolling && prev_d[d] != now_d[d]) {
            set_roll(k, d, prev_d[d], now_d[d], 1 + (int)(ms / OFFICE_SKELETON_TICK_FRAME_MS));
        } else {
            k->digit[d].image = &k->glyph[now_d[d]];
        }
    }
    return next;
}
