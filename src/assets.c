#include "assets.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

bool image_load_bmp8(const char *rel, Image8 *out)
{
    size_t sz;
    uint8_t *d = vfs_read_all(rel, &sz);
    if (!d) return false;
    bool ok = false;
    if (sz >= 54 && d[0] == 'B' && d[1] == 'M' && rd16(d + 28) == 8 && rd32(d + 30) == 0) {
        uint32_t off = rd32(d + 10), hsz = rd32(d + 14);
        int w = (int)rd32(d + 18), h = (int)rd32(d + 22);
        bool bottom_up = h > 0;
        if (h < 0) h = -h;
        uint32_t ncol = rd32(d + 46);
        if (!ncol) ncol = 256;
        const uint8_t *pal = d + 14 + hsz;
        int stride = (w + 3) & ~3;
        /* Some files are truncated by a few bytes; clamp rather than reject. */
        out->w = w; out->h = h;
        out->pixels = calloc((size_t)w * h, 1);
        memset(out->palette, 0, sizeof out->palette);
        for (uint32_t i = 0; i < ncol && i < 256; i++)
            out->palette[i] = (RGB){ pal[i * 4 + 2], pal[i * 4 + 1], pal[i * 4] };
        for (int y = 0; y < h; y++) {
            size_t src = off + (size_t)(bottom_up ? h - 1 - y : y) * stride;
            if (src >= sz) continue;
            size_t n = (size_t)w;
            if (src + n > sz) n = sz - src;
            memcpy(out->pixels + (size_t)y * w, d + src, n);
        }
        ok = true;
    }
    free(d);
    return ok;
}

void image_free(Image8 *img) { free(img->pixels); img->pixels = NULL; }

void image_blit(const Image8 *img, Framebuffer *fb, int x, int y)
{
    for (int j = 0; j < img->h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= fb->h) continue;
        for (int i = 0; i < img->w; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= fb->w) continue;
            fb->pixels[dy * fb->w + dx] = img->pixels[j * img->w + i];
        }
    }
}
