/* Loaders for original Return Fire data files. */
#pragma once
#include "platform.h"
#include <stddef.h>

typedef struct {
    int w, h;
    uint8_t *pixels;       /* top-down, w*h indices */
    RGB palette[256];
} Image8;

/* .RFA and .BMP are plain 8-bit Windows BMPs. Paths are disc-relative (vfs.h). */
bool image_load_bmp8(const char *rel, Image8 *out);
void image_free(Image8 *img);
void image_blit(const Image8 *img, Framebuffer *fb, int x, int y);
