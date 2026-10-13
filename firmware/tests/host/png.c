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
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
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

static bool write_chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t head[8];
    put_be32(head, (uint32_t)len);
    memcpy(head + 4, type, 4);
    uint32_t crc = crc_update(0, head + 4, 4);
    crc = crc_update(crc, data, len);
    uint8_t tail[4];
    put_be32(tail, crc);
    return fwrite(head, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) && fwrite(tail, 1, 4, f) == 4;
}

bool png_write_rgb565(const char *path, const gfx_surface_t *s)
{
    const size_t row_bytes = 1 + (size_t)s->w * 3; // filter byte + RGB
    const size_t raw_len = row_bytes * (size_t)s->h;
    const size_t blocks = (raw_len + 65534) / 65535;
    const size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *raw = malloc(raw_len);
    uint8_t *z = malloc(z_len);
    if (!raw || !z) {
        free(raw);
        free(z);
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
    // zlib stream with stored (uncompressed) deflate blocks.
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
    put_be32(ihdr, (uint32_t)s->w);
    put_be32(ihdr + 4, (uint32_t)s->h);
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 2;  // RGB
    ihdr[10] = 0; // deflate
    ihdr[11] = 0; // adaptive filtering
    ihdr[12] = 0; // no interlace
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(sig, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13) && write_chunk(f, "IDAT", z, o) &&
              write_chunk(f, "IEND", NULL, 0);
    if (f && fclose(f) != 0) {
        ok = false;
    }
    free(raw);
    free(z);
    return ok;
}
