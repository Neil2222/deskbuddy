// Exports DeskBuddy's own artwork for other applications (the Windows companion first): transparent PNGs and a
// multi-size Windows icon. Everything is drawn from the firmware's sprite definitions with the firmware's own
// decoder and palette, so the pictures are exactly the pixels on the device; scaling is integer nearest-neighbour.
//
//   buddy_icons <output_dir>          (tools/host-test.ps1 -Icons writes assets/buddy)
//
// Writes:
//   face-<expression>.png, @8x     Buddy's head: antenna, head, eyes, mouth (24x21 at 1x). The expressions are
//                                  eye/mouth pairs the firmware itself shows: neutral (idle), happy (happy hops),
//                                  surprised (a tap), sleepy (mid-blink, antenna light off), and the moods
//                                  relaxed, peukie (with the cigarette at his mouth), angry and working.
//   buddy.png, buddy@8x.png        the whole robot, standing
//   can.png, moon.png              the Monster can and the Office Mode moon, as on the device
//   icon-<size>.png, buddy.ico     the application icon in 16..256 px. From 24 px up: the whole head at the largest
//                                  integer scale that fits. 16 and 20 px are too small for the 24-px-wide head, so
//                                  they are exact crops of it (face screen with eyes and mouth; at 20 px with the
//                                  body bezel), never a redrawn miniature.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buddy_art.h"
#include "gfx_scene.h"
#include "office_clock.h"
#include "png.h"

typedef struct {
    int w, h;
    uint8_t *px; // RGBA
} canvas_t;

static canvas_t make(int w, int h)
{
    canvas_t c = {w, h, calloc((size_t)w * h, 4)};
    if (!c.px) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return c;
}

// Draws an indexed image (index 0 transparent) with its palette, as the compositor does, at 1x; mirrored if flip.
static void blit_flip(canvas_t *c, const gfx_image_t *img, const gfx_palette_t *pal, int x0, int y0, bool flip)
{
    for (int y = 0; y < img->h; y++) {
        for (int x = 0; x < img->w; x++) {
            const uint8_t i = img->px[y * img->w + (flip ? img->w - 1 - x : x)];
            const int cx = x0 + x, cy = y0 + y;
            if (!i || cx < 0 || cy < 0 || cx >= c->w || cy >= c->h) {
                continue;
            }
            const gfx_color_t v = pal->colors[i]; // RGB565, as on the device
            const unsigned r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
            uint8_t *p = c->px + ((size_t)cy * c->w + cx) * 4;
            p[0] = (uint8_t)((r << 3) | (r >> 2));
            p[1] = (uint8_t)((g << 2) | (g >> 4));
            p[2] = (uint8_t)((b << 3) | (b >> 2));
            p[3] = 255;
        }
    }
}

static void blit(canvas_t *c, const gfx_image_t *img, const gfx_palette_t *pal, int x0, int y0)
{
    blit_flip(c, img, pal, x0, y0, false);
}

static canvas_t crop(const canvas_t *s, int x0, int y0, int w, int h)
{
    canvas_t c = make(w, h);
    for (int y = 0; y < h; y++) {
        memcpy(c.px + (size_t)y * w * 4, s->px + ((size_t)(y0 + y) * s->w + x0) * 4, (size_t)w * 4);
    }
    return c;
}

// Crops to the non-transparent pixels.
static canvas_t trim(const canvas_t *s)
{
    int x0 = s->w, y0 = s->h, x1 = -1, y1 = -1;
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            if (s->px[((size_t)y * s->w + x) * 4 + 3]) {
                x0 = x < x0 ? x : x0, y0 = y < y0 ? y : y0, x1 = x > x1 ? x : x1, y1 = y > y1 ? y : y1;
            }
        }
    }
    return crop(s, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

// s scaled by k (nearest neighbour), centred on a size x size transparent square (or unpadded when size is 0).
static canvas_t place(const canvas_t *s, int k, int size)
{
    const int w = size ? size : s->w * k, h = size ? size : s->h * k;
    const int ox = (w - s->w * k) / 2, oy = (h - s->h * k) / 2;
    canvas_t c = make(w, h);
    for (int y = 0; y < s->h * k; y++) {
        for (int x = 0; x < s->w * k; x++) {
            memcpy(c.px + ((size_t)(oy + y) * w + ox + x) * 4, s->px + ((size_t)(y / k) * s->w + x / k) * 4, 4);
        }
    }
    return c;
}

static void save(const char *dir, const char *name, const canvas_t *c)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    if (!png_write_rgba(path, c->px, c->w, c->h)) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    printf("%s (%dx%d)\n", path, c->w, c->h);
}

// The head as the firmware assembles it: antenna ball, head (with the stem), eyes and mouth, in art pixels.
static canvas_t head(const buddy_art_t *a, buddy_eyes_t eyes, buddy_mouth_t mouth, const gfx_image_t *antenna)
{
    canvas_t c = make(a->w, a->head_at.y + a->head.h);
    blit(&c, antenna, &a->palette, a->antenna_at.x, a->antenna_at.y);
    blit(&c, &a->head, &a->palette, a->head_at.x, a->head_at.y);
    for (int i = 0; i < 2; i++) { // the right eye mirrored for asymmetric expressions, as the view does
        blit_flip(&c, &a->eyes[eyes], &a->palette, a->eye_at[i].x, a->eye_at[i].y, i == 1 && a->eye_mirror[eyes]);
    }
    blit(&c, &a->mouth[mouth], &a->palette, a->mouth_at.x, a->mouth_at.y);
    return c;
}

static void put_le16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    put_le16(p, v & 0xFFFF);
    put_le16(p + 2, v >> 16);
}

// One icon image: up to 64 px as a 32-bit DIB with an AND mask (read by everything), larger ones as PNG.
static uint8_t *ico_image(const canvas_t *c, size_t *len)
{
    if (c->w > 64) {
        return png_encode_rgba(c->px, c->w, c->h, len);
    }
    const size_t mask_row = (size_t)((c->w + 31) / 32) * 4;
    *len = 40 + (size_t)c->w * c->h * 4 + mask_row * c->h;
    uint8_t *d = calloc(*len, 1);
    if (!d) {
        return NULL;
    }
    put_le32(d, 40);
    put_le32(d + 4, (uint32_t)c->w);
    put_le32(d + 8, (uint32_t)c->h * 2); // colour plus mask
    put_le16(d + 12, 1);
    put_le16(d + 14, 32);
    uint8_t *px = d + 40, *mask = px + (size_t)c->w * c->h * 4;
    for (int y = 0; y < c->h; y++) {
        const uint8_t *src = c->px + (size_t)(c->h - 1 - y) * c->w * 4; // bottom-up
        for (int x = 0; x < c->w; x++) {
            uint8_t *p = px + ((size_t)y * c->w + x) * 4;
            p[0] = src[x * 4 + 2], p[1] = src[x * 4 + 1], p[2] = src[x * 4 + 0], p[3] = src[x * 4 + 3];
            if (!src[x * 4 + 3]) {
                mask[(size_t)y * mask_row + x / 8] |= (uint8_t)(0x80 >> (x % 8));
            }
        }
    }
    return d;
}

static void save_ico(const char *dir, const canvas_t *img, int n)
{
    uint8_t *data[16];
    size_t len[16], total = 6 + 16 * (size_t)n;
    for (int i = 0; i < n; i++) {
        data[i] = ico_image(&img[i], &len[i]);
        if (!data[i]) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        total += len[i];
    }
    uint8_t *ico = calloc(total, 1);
    if (!ico) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    put_le16(ico + 2, 1); // icon
    put_le16(ico + 4, (unsigned)n);
    size_t offset = 6 + 16 * (size_t)n;
    for (int i = 0; i < n; i++) {
        uint8_t *e = ico + 6 + 16 * i;
        e[0] = (uint8_t)(img[i].w >= 256 ? 0 : img[i].w);
        e[1] = (uint8_t)(img[i].h >= 256 ? 0 : img[i].h);
        put_le16(e + 4, 1);
        put_le16(e + 6, 32);
        put_le32(e + 8, (uint32_t)len[i]);
        put_le32(e + 12, (uint32_t)offset);
        memcpy(ico + offset, data[i], len[i]);
        offset += len[i];
        free(data[i]);
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/buddy.ico", dir);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(ico, 1, total, f) != total || fclose(f) != 0) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    free(ico);
    printf("%s (%d sizes)\n", path, n);
}

static void background(void *ctx, gfx_color_t *dst, int x, int y, int w)
{
    (void)ctx, (void)x, (void)y;
    memset(dst, 0, (size_t)w * sizeof(*dst));
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    const buddy_art_t *a = buddy_art_get();
    if (!a) {
        fprintf(stderr, "artwork failed to load\n");
        return 1;
    }

    // Faces.
    static const struct {
        const char *name;
        buddy_eyes_t eyes;
        buddy_mouth_t mouth;
        bool lit, cig;
    } faces[] = {
        {"neutral", BUDDY_EYES_OPEN, BUDDY_MOUTH_NEUTRAL, true, false},
        {"happy", BUDDY_EYES_HAPPY, BUDDY_MOUTH_SMILE, true, false},
        {"surprised", BUDDY_EYES_WIDE, BUDDY_MOUTH_OPEN, true, false},
        {"sleepy", BUDDY_EYES_CLOSED, BUDDY_MOUTH_NEUTRAL, false, false},
        {"relaxed", BUDDY_EYES_LIDDED, BUDDY_MOUTH_SMILE, true, false},
        {"peukie", BUDDY_EYES_LIDDED, BUDDY_MOUTH_SMIRK, true, true},
        {"angry", BUDDY_EYES_ANGRY, BUDDY_MOUTH_FROWN, true, false},
        {"working", BUDDY_EYES_FOCUS, BUDDY_MOUTH_TONGUE, true, false},
    };
    canvas_t neutral = {0};
    for (size_t i = 0; i < sizeof(faces) / sizeof(faces[0]); i++) {
        canvas_t full = head(a, faces[i].eyes, faces[i].mouth, faces[i].lit ? &a->antenna_on : &a->antenna_off);
        if (faces[i].cig) { // in the corner of his mouth, glowing, all within the face screen
            const buddy_point_t m = a->props.hand_mouth;
            blit(&full, &a->props.cig[1][1], &a->palette, m.x - BUDDY_CIG_GRIP, m.y - 1);
        }
        canvas_t face = trim(&full);
        char name[64];
        snprintf(name, sizeof(name), "face-%s", faces[i].name);
        save(dir, name, &face);
        canvas_t big = place(&face, 8, 0);
        snprintf(name, sizeof(name), "face-%s@8x", faces[i].name);
        save(dir, name, &big);
        free(big.px);
        free(face.px);
        if (i == 0) {
            neutral = full;
        } else {
            free(full.px);
        }
    }

    // The whole robot.
    canvas_t body = head(a, BUDDY_EYES_OPEN, BUDDY_MOUTH_NEUTRAL, &a->antenna_on);
    canvas_t robot = make(a->w, a->h);
    memcpy(robot.px, body.px, (size_t)body.w * body.h * 4);
    blit(&robot, &a->torso, &a->palette, a->torso_at.x, a->torso_at.y);
    blit(&robot, &a->shadow, &a->palette, a->shadow_at.x, a->shadow_at.y);
    canvas_t robot_trim = trim(&robot);
    save(dir, "buddy", &robot_trim);
    canvas_t robot_big = place(&robot_trim, 8, 0);
    save(dir, "buddy@8x", &robot_big);

    // Props from the control panel: the Monster can and the Office Mode moon.
    canvas_t can = make(a->props.can[BUDDY_CAN_LEVELS - 1].w, a->props.can[BUDDY_CAN_LEVELS - 1].h);
    blit(&can, &a->props.can[BUDDY_CAN_LEVELS - 1], &a->palette, 0, 0);
    save(dir, "can", &can);
    static gfx_color_t strip[64];
    static gfx_scene_t scene;
    static office_button_t moon_button;
    if (!gfx_scene_init(&scene, 64, 64, background, NULL, strip, 64, 1) ||
        !office_button_init(&moon_button, &scene, (gfx_rect_t){0, 0, 64, 64}, 1, 0)) {
        fprintf(stderr, "moon setup failed\n");
        return 1;
    }
    canvas_t moon = make(moon_button.sprite.image->w, moon_button.sprite.image->h);
    blit(&moon, moon_button.sprite.image, moon_button.sprite.palette, 0, 0);
    save(dir, "moon", &moon);

    // Application icon. Head rows/columns below are in the head image; the head starts at head_at in the canvas.
    canvas_t face = trim(&neutral);
    const int hy = a->head_at.y;
    canvas_t tiny = crop(&neutral, 6, hy + 3, 16, 14);  // face screen, eyes, mouth, bezel top and chin
    canvas_t small = crop(&neutral, 4, hy + 2, 20, 15); // the same with the body bezel at the sides
    static const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
    canvas_t icons[sizeof(sizes) / sizeof(sizes[0])];
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        const int s = sizes[i];
        const canvas_t *src = s == 16 ? &tiny : s == 20 ? &small : &face;
        const int k = s >= 24 ? (s / face.w < s / face.h ? s / face.w : s / face.h) : 1;
        icons[i] = place(src, k, s);
        char name[32];
        snprintf(name, sizeof(name), "icon-%d", s);
        save(dir, name, &icons[i]);
    }
    save_ico(dir, icons, (int)(sizeof(sizes) / sizeof(sizes[0])));
    return 0;
}
