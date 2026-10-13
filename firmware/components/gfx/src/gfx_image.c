#include "gfx_image.h"

#include <string.h>

bool gfx_image_from_ascii(gfx_image_t *img, uint8_t *storage, size_t capacity, const char *const *rows, int n_rows,
                          const char *keys)
{
    if (!img || !storage || !rows || !keys || n_rows <= 0 || n_rows > UINT16_MAX || !rows[0]) {
        return false;
    }
    const size_t w = strlen(rows[0]);
    const size_t n_keys = strlen(keys);
    if (w == 0 || w > UINT16_MAX || n_keys == 0 || n_keys > 256 || w * (size_t)n_rows > capacity) {
        return false;
    }
    for (int y = 0; y < n_rows; y++) {
        if (!rows[y] || strlen(rows[y]) != w) {
            return false;
        }
        for (size_t x = 0; x < w; x++) {
            const char *k = memchr(keys, rows[y][x], n_keys);
            if (!k) {
                return false;
            }
            storage[(size_t)y * w + x] = (uint8_t)(k - keys);
        }
    }
    img->w = (uint16_t)w;
    img->h = (uint16_t)n_rows;
    img->px = storage;
    return true;
}
