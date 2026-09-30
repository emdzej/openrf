/* Loaders for original Return Fire data files. */
#pragma once
#include "platform.h"
#include <stddef.h>

typedef struct {
    int w, h;
    uint8_t *pixels;       /* top-down, w*h indices */
    RGB palette[256];
} Image8;

/* Data root (directory containing ART/, SOUND/, TITLE/, WORLDS/ ...). */
void assets_set_root(const char *root);
const char *assets_path(const char *rel);   /* rel uses '/', case-insensitive lookup */

/* .RFA and .BMP are plain 8-bit Windows BMPs. */
bool image_load_bmp8(const char *rel, Image8 *out);
void image_free(Image8 *img);
void image_blit(const Image8 *img, Framebuffer *fb, int x, int y);

uint8_t *file_read_all(const char *rel, size_t *size);
