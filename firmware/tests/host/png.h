// Minimal PNG writer for host tools (uncompressed deflate, no dependencies).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx_types.h"

// Writes an RGB565 surface as an 8-bit RGB PNG. Returns false on I/O or allocation failure.
bool png_write_rgb565(const char *path, const gfx_surface_t *s);

// rgba: w*h pixels of 4 bytes (R, G, B, A), row-major.
bool png_write_rgba(const char *path, const uint8_t *rgba, int w, int h);

// The same PNG in memory (malloc'd; the caller frees it), or NULL.
uint8_t *png_encode_rgba(const uint8_t *rgba, int w, int h, size_t *len);
