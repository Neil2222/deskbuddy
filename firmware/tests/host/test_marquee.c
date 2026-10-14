// The message marquee: text policy, how long a message stays up, what the LEDs show, and that scrolling redraws
// only the sign.
#include <string.h>

#include "gfx_repair.h"
#include "layout.h"
#include "marquee.h"
#include "test.h"

enum { W = 1024, H = 600, ALIGN = 32 };

static gfx_color_t s_strip[16 * 1024];
static gfx_color_t s_buf[2][W * H], s_ref[W * H];

static int glyphs(const char *utf8, uint8_t *out)
{
    return marquee_glyphs(utf8, out, MARQUEE_TEXT_MAX);
}

void test_marquee_text_policy(void)
{
    uint8_t g[MARQUEE_TEXT_MAX];
    // ASCII, spaces and punctuation as they are.
    const char *ascii = "Hi! 12:30, ok? (a+b)=\"c\" ~#@";
    CHECK_EQ(glyphs(ascii, g), (int)strlen(ascii));
    CHECK(memcmp(g, ascii, strlen(ascii)) == 0);

    // Accents dropped; typographic quotes, dashes and the ellipsis made plain; euro, degree and hearts kept.
    int n = glyphs("caf\xC3\xA9 \xC3\x91 \xE2\x80\x9Cx\xE2\x80\x9D \xE2\x80\x93 \xE2\x80\xA6 5\xE2\x82\xAC 20\xC2\xB0 "
                   "\xE2\x99\xA5",
                   g);
    const uint8_t expect[] = {'c', 'a', 'f', 'e', ' ', 'N', ' ', '"', 'x', '"', ' ', '-', ' ', '.', ' ', '5',
                              MARQUEE_GLYPH_EURO, ' ', '2', '0', MARQUEE_GLYPH_DEGREE, ' ', MARQUEE_GLYPH_HEART};
    CHECK_EQ(n, (int)sizeof(expect));
    CHECK(memcmp(g, expect, sizeof(expect)) == 0);

    // Emoji and other scripts show as a box; a red heart with its emoji-style selector is one heart; tabs become
    // spaces; zero-width joiners vanish.
    n = glyphs("\xF0\x9F\x98\x80\xE2\x9D\xA4\xEF\xB8\x8F\t\xE4\xBD\xA0" "a\xE2\x80\x8D" "b", g);
    const uint8_t expect2[] = {MARQUEE_GLYPH_UNKNOWN, MARQUEE_GLYPH_HEART, ' ', MARQUEE_GLYPH_UNKNOWN, 'a', 'b'};
    CHECK_EQ(n, (int)sizeof(expect2));
    CHECK(memcmp(g, expect2, sizeof(expect2)) == 0);

    // Broken UTF-8 (the protocol rejects it, but the sign must not choke on it): a box per bad byte.
    n = glyphs("a\xC3" "b\xFF", g);
    const uint8_t expect3[] = {'a', MARQUEE_GLYPH_UNKNOWN, 'b', MARQUEE_GLYPH_UNKNOWN};
    CHECK_EQ(n, (int)sizeof(expect3));
    CHECK(memcmp(g, expect3, sizeof(expect3)) == 0);

    // Never more than asked for.
    CHECK_EQ(marquee_glyphs("abcdef", g, 4), 4);
}

static marquee_t s_m;

static void start(const char *text)
{
    static gfx_scene_t scene;
    gfx_scene_init(&scene, W, H, layout_background, NULL, s_strip, 16 * 1024, ALIGN);
    marquee_init(&s_m, &scene, 30);
    marquee_start(&s_m, text, 1000);
}

// Short messages repeat until they have been up for a while; long ones scroll through once; then the sign hides.
void test_marquee_stays_long_enough_then_hides(void)
{
    start("Hi");
    CHECK(marquee_active(&s_m));
    CHECK(s_m.passes > 1 && s_m.passes <= MARQUEE_MAX_PASSES); // a short text runs again
    const uint32_t short_ms = marquee_duration_ms(&s_m);
    CHECK(short_ms >= MARQUEE_MIN_SHOW_MS);

    CHECK(marquee_apply(&s_m, 10, 20, 1000) <= MARQUEE_FRAME_MS);
    CHECK(!(s_m.sprite.flags & GFX_SPRITE_HIDDEN) && s_m.sprite.x == 10 && s_m.sprite.y == 20);
    CHECK(marquee_apply(&s_m, 10, 20, 1000 + short_ms - 1) <= MARQUEE_FRAME_MS);
    CHECK(marquee_active(&s_m));
    CHECK_EQ(marquee_apply(&s_m, 10, 20, 1000 + short_ms), UINT32_MAX);
    CHECK(!marquee_active(&s_m) && (s_m.sprite.flags & GFX_SPRITE_HIDDEN));

    char longest[MARQUEE_TEXT_MAX + 1];
    memset(longest, 'W', MARQUEE_TEXT_MAX);
    longest[MARQUEE_TEXT_MAX] = '\0';
    start(longest);
    CHECK_EQ(s_m.passes, 1);
    CHECK_EQ(s_m.ncols, MARQUEE_TEXT_MAX * 6 - 1); // 5 columns and a gap per W, no trailing gap
    // In from the right edge and out to the left, one column per MARQUEE_COL_MS whatever the frame rate.
    CHECK_EQ(marquee_duration_ms(&s_m),
             MARQUEE_INTRO_MS + (uint32_t)(MARQUEE_COLS + s_m.ncols) * MARQUEE_COL_MS);
    CHECK(marquee_duration_ms(&s_m) < 60000); // even the widest full-length message is done within a minute

    start("");
    CHECK_EQ(marquee_apply(&s_m, 0, 0, 1000 + MARQUEE_INTRO_MS + MARQUEE_COLS * MARQUEE_COL_MS * 3 + 1),
             UINT32_MAX); // nothing to show: just the sweep and an empty pass
}

// Whether the LED at (col, row) is lit in the image now shown: compares the colour of its centre pixel.
static bool lit(int col, int row)
{
    const gfx_image_t *img = s_m.sprite.image;
    const uint8_t idx = img->px[(MARQUEE_BEZEL + row * MARQUEE_LED + 1) * img->w + MARQUEE_BEZEL + col * MARQUEE_LED + 1];
    return s_m.sprite.palette->colors[idx] == GFX_RGB(255, 232, 160);
}

void test_marquee_shows_the_text_scrolling_left(void)
{
    start("I"); // trimmed to its three columns: 0x41 0x7F 0x41
    const uint32_t scroll = 1000 + MARQUEE_INTRO_MS;
    // After MARQUEE_COLS columns' time the text's first column has reached the left edge.
    marquee_apply(&s_m, 0, 0, scroll + MARQUEE_COLS * MARQUEE_COL_MS);
    for (int r = 1; r <= 7; r++) {
        CHECK(lit(1, r)); // the stem: font rows 0..6 on LED rows 1..7
    }
    CHECK(!lit(1, 0) && !lit(1, 8)); // blank rows above and below the font
    CHECK(lit(0, 1) && !lit(0, 2) && lit(0, 7) && lit(2, 1) && lit(2, 7)); // serifs
    CHECK(!lit(3, 4) && !lit(MARQUEE_COLS - 1, 4));
    // Within a frame nothing moves (the sign is redrawn once per frame)...
    marquee_apply(&s_m, 0, 0, scroll + MARQUEE_COLS * MARQUEE_COL_MS + MARQUEE_FRAME_MS / 2);
    CHECK(lit(1, 4) && lit(0, 1) && !lit(0, 4));
    // ...and the next frame has moved as far as the time says.
    marquee_apply(&s_m, 0, 0, scroll + MARQUEE_COLS * MARQUEE_COL_MS + MARQUEE_FRAME_MS);
    const int moved = MARQUEE_FRAME_MS / MARQUEE_COL_MS;
    static const uint8_t k_i[3] = {0x41, 0x7F, 0x41};
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < MARQUEE_ROWS; r++) {
            const int j = c + moved; // text column now under LED column c
            CHECK_EQ(lit(c, r), j < 3 && (((unsigned)k_i[j] << 1) >> r & 1u));
        }
    }
    // During the sweep the panel shows a lit bar, not text.
    marquee_apply(&s_m, 0, 0, 1000 + MARQUEE_INTRO_MS / 2);
    int lit_cols = 0;
    for (int c = 0; c < MARQUEE_COLS; c++) {
        lit_cols += lit(c, 0) && lit(c, 8);
    }
    CHECK(lit_cols >= 1 && lit_cols <= 3);
}

// The whole loop on two alternating buffers: every presented frame equals a full redraw, and scrolling redraws
// the sign only, never the whole screen.
void test_marquee_redraws_only_the_sign(void)
{
    static gfx_scene_t scene;
    static layout_t layout;
    static gfx_dirty_t changes, redraw, all;
    static gfx_repair_t repair;
    layout = layout_compute(W, H, 65, false, ALIGN);
    CHECK(gfx_scene_init(&scene, W, H, layout_background, &layout, s_strip, 16 * 1024, ALIGN));
    CHECK(marquee_init(&s_m, &scene, 30));
    gfx_dirty_init(&changes, W, H, ALIGN);
    gfx_dirty_init(&redraw, W, H, ALIGN);
    gfx_dirty_init(&all, W, H, ALIGN);
    gfx_dirty_add(&all, (gfx_rect_t){0, 0, W, H});
    gfx_repair_init(&repair, W, H, ALIGN);
    const gfx_surface_t ref = {s_ref, W, H, W};
    memset(s_buf, 0, sizeof(s_buf));
    gfx_scene_invalidate(&scene, (gfx_rect_t){0, 0, W, H});

    const int x = (layout.stage.w - MARQUEE_W) / 2, y = 22;
    const int sign_area = ((MARQUEE_W + 2 * ALIGN) * MARQUEE_H);
    int back = 0, frames = 0, bad = 0, too_big = 0;
    uint32_t end = 0;
    for (uint32_t t = 0; t < 40000; t += 10) {
        if (t == 500) {
            marquee_start(&s_m, "Lunch? 12:30 \xE2\x99\xA5", t);
            end = t + marquee_duration_ms(&s_m);
        }
        marquee_apply(&s_m, x, y, t);
        gfx_dirty_clear(&changes);
        gfx_scene_collect(&scene, &changes);
        if (changes.count == 0) {
            continue;
        }
        gfx_repair_next(&repair, &changes, &redraw);
        const gfx_surface_t dst = {s_buf[back], W, H, W};
        gfx_scene_render(&scene, &redraw, &dst);
        gfx_scene_render(&scene, &all, &ref);
        bad += memcmp(s_buf[back], s_ref, sizeof(s_ref)) != 0;
        back ^= 1;
        if (++frames > 2) { // after the two boot frames
            too_big += gfx_dirty_area(&redraw) > sign_area;
        }
    }
    CHECK(end > 500 && !marquee_active(&s_m));
    CHECK(frames > 100); // it really scrolled
    CHECK_EQ(bad, 0);
    CHECK_EQ(too_big, 0);
}
