// Minimal PNG writer for host tools (uncompressed deflate, no dependencies).
#pragma once

#include <stdbool.h>

#include "gfx_types.h"

// Writes an RGB565 surface as an 8-bit RGB PNG. Returns false on I/O or allocation failure.
bool png_write_rgb565(const char *path, const gfx_surface_t *s);
