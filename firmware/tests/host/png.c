#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint8_t *put_chunk(uint8_t *o, const char *type, const uint8_t *data, size_t len)
{
    put_be32(o, (uint32_t)len);
    memcpy(o + 4, type, 4);
    if (len) {
        memcpy(o + 8, data, len);
    }
    put_be32(o + 8 + len, crc_update(0, o + 4, 4 + len));
    return o + 12 + len;
}

// Encodes filtered rows (raw: a filter byte and the pixels per row) as a PNG with stored deflate blocks.
static uint8_t *encode(const uint8_t *raw, size_t raw_len, int w, int h, uint8_t color_type, size_t *out_len)
{
    const size_t blocks = (raw_len + 65534) / 65535;
    const size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = malloc(z_len);
    uint8_t *png = malloc(8 + 25 + 12 + z_len + 12);
    if (!z || !png) {
        free(z);
        free(png);
        return NULL;
    }
    size_t o = 0;
    z[o++] = 0x78;
    z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t pos = 0; pos < raw_len;) {
        const size_t n = raw_len - pos < 65535 ? raw_len - pos : 65535;
        z[o++] = pos + n == raw_len ? 1 : 0;
        z[o++] = (uint8_t)n;
        z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n;
        z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, raw + pos, n);
        for (size_t i = 0; i < n; i++) {
            a = (a + raw[pos + i]) % 65521;
            b = (b + a) % 65521;
        }
        o += n;
        pos += n;
    }
    put_be32(z + o, (b << 16) | a);
    o += 4;

    uint8_t ihdr[13];
    put_be32(ihdr, (uint32_t)w);
    put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;           // bit depth
    ihdr[9] = color_type;  // 2: RGB, 6: RGBA
    ihdr[10] = 0;          // deflate
    ihdr[11] = 0;          // adaptive filtering
    ihdr[12] = 0;          // no interlace
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    memcpy(png, sig, 8);
    uint8_t *p = put_chunk(png + 8, "IHDR", ihdr, 13);
    p = put_chunk(p, "IDAT", z, o);
    p = put_chunk(p, "IEND", NULL, 0);
    free(z);
    *out_len = (size_t)(p - png);
    return png;
}

static bool write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(data, 1, len, f) == len;
    if (f && fclose(f) != 0) {
        ok = false;
    }
    return ok;
}

bool png_write_rgb565(const char *path, const gfx_surface_t *s)
{
    const size_t row_bytes = 1 + (size_t)s->w * 3; // filter byte + RGB
    uint8_t *raw = malloc(row_bytes * (size_t)s->h);
    if (!raw) {
        return false;
    }
    for (int y = 0; y < s->h; y++) {
        uint8_t *row = raw + (size_t)y * row_bytes;
        row[0] = 0;
        for (int x = 0; x < s->w; x++) {
            const gfx_color_t c = s->px[(size_t)y * s->stride + x];
            const unsigned r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
            row[1 + x * 3] = (uint8_t)((r << 3) | (r >> 2));
            row[2 + x * 3] = (uint8_t)((g << 2) | (g >> 4));
            row[3 + x * 3] = (uint8_t)((b << 3) | (b >> 2));
        }
    }
    size_t len = 0;
    uint8_t *png = encode(raw, row_bytes * (size_t)s->h, s->w, s->h, 2, &len);
    free(raw);
    const bool ok = png && write_file(path, png, len);
    free(png);
    return ok;
}

uint8_t *png_encode_rgba(const uint8_t *rgba, int w, int h, size_t *len)
{
    const size_t row_bytes = 1 + (size_t)w * 4;
    uint8_t *raw = malloc(row_bytes * (size_t)h);
    if (!raw) {
        return NULL;
    }
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * row_bytes] = 0;
        memcpy(raw + (size_t)y * row_bytes + 1, rgba + (size_t)y * w * 4, (size_t)w * 4);
    }
    uint8_t *png = encode(raw, row_bytes * (size_t)h, w, h, 6, len);
    free(raw);
    return png;
}

bool png_write_rgba(const char *path, const uint8_t *rgba, int w, int h)
{
    size_t len = 0;
    uint8_t *png = png_encode_rgba(rgba, w, h, &len);
    const bool ok = png && write_file(path, png, len);
    free(png);
    return ok;
}
