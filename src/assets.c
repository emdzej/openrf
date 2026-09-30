#include "assets.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char root[1024] = "cd";
static char pathbuf[2048];

void assets_set_root(const char *r) { snprintf(root, sizeof root, "%s", r); }

/* Resolve rel path component by component, matching case-insensitively
   (the disc uses upper case, the game's code uses mixed case). */
const char *assets_path(const char *rel)
{
    char cur[2048];
    snprintf(cur, sizeof cur, "%s", root);
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", rel);
    for (char *p = tmp; *p; p++) if (*p == '\\') *p = '/';
    for (char *tok = strtok(tmp, "/"); tok; tok = strtok(NULL, "/")) {
        DIR *d = opendir(cur);
        const char *match = tok;
        struct dirent *e;
        while (d && (e = readdir(d)))
            if (!strcasecmp(e->d_name, tok)) { match = e->d_name; break; }
        size_t n = strlen(cur);
        snprintf(cur + n, sizeof cur - n, "/%s", match);
        if (d) closedir(d);
    }
    snprintf(pathbuf, sizeof pathbuf, "%s", cur);
    return pathbuf;
}

uint8_t *file_read_all(const char *rel, size_t *size)
{
    FILE *f = fopen(assets_path(rel), "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    if (size) *size = (size_t)n;
    return buf;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

bool image_load_bmp8(const char *rel, Image8 *out)
{
    size_t sz;
    uint8_t *d = file_read_all(rel, &sz);
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
