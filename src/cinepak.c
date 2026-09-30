/* Cinepak decoder, following the well-known public description of the
   bitstream (strips -> codebook chunks 0x20xx-0x27xx -> vector chunks 0x30xx). */
#include "cinepak.h"
#include <stdlib.h>
#include <string.h>

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static int clip(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

void cinepak_init(Cinepak *c, int w, int h)
{
    memset(c, 0, sizeof *c);
    c->w = w; c->h = h;
    c->frame = calloc((size_t)w * h, 4);
}

void cinepak_free(Cinepak *c) { free(c->frame); c->frame = NULL; }

static void load_codebook(CvidEntry *cb, unsigned id, const uint8_t *p, const uint8_t *end)
{
    int n = (id & 0x0400) ? 4 : 6;            /* 0x24xx..0x27xx: greyscale, Y only */
    int selective = id & 0x0100;
    uint32_t flag = 0, mask = 1;
    for (int i = 0; i < 256; i++) {
        if (selective && !(mask >>= 1)) {
            if (p + 4 > end) return;
            flag = be32(p); p += 4;
            mask = 0x80000000u;
        }
        if (!selective || (flag & mask)) {
            if (p + n > end) return;
            memcpy(cb[i].y, p, 4);
            cb[i].u = n == 6 ? (int8_t)p[4] : 0;
            cb[i].v = n == 6 ? (int8_t)p[5] : 0;
            p += n;
        }
    }
}

static uint32_t yuv(const CvidEntry *e, int k)
{
    int y = e->y[k], u = e->u, v = e->v;
    return 0xFF000000u | (uint32_t)clip(y + v * 2) << 16 | (uint32_t)clip(y - u / 2 - v) << 8 | (uint32_t)clip(y + u * 2);
}

static void decode_vectors(Cinepak *c, int s, unsigned id, const uint8_t *p, const uint8_t *end, int y1, int y2)
{
    uint32_t flag = 0, mask = 0;
    for (int y = y1; y < y2; y += 4) {
        for (int x = 0; x < c->w; x += 4) {
            if ((id & 1) && !(mask >>= 1)) {
                if (p + 4 > end) return;
                flag = be32(p); p += 4; mask = 0x80000000u;
            }
            if ((id & 1) && !(flag & mask)) continue;          /* inter: block unchanged */
            if (!(id & 2) && !(mask >>= 1)) {
                if (p + 4 > end) return;
                flag = be32(p); p += 4; mask = 0x80000000u;
            }
            uint32_t *dst = c->frame + (size_t)y * c->w + x;
            if ((id & 2) || !(flag & mask)) {
                /* V1: one entry, each Y value covers a 2x2 quadrant. */
                if (p >= end) return;
                const CvidEntry *e = &c->v1[s][*p++];
                uint32_t q[4] = { yuv(e, 0), yuv(e, 1), yuv(e, 2), yuv(e, 3) };
                for (int j = 0; j < 4 && y + j < c->h; j++)
                    for (int i = 0; i < 4 && x + i < c->w; i++)
                        dst[j * c->w + i] = q[(j >> 1) * 2 + (i >> 1)];
            } else {
                /* V4: four entries, each a 2x2 sub-block (TL, TR, BL, BR). */
                if (p + 4 > end) return;
                for (int k = 0; k < 4; k++) {
                    const CvidEntry *e = &c->v4[s][*p++];
                    int bx = (k & 1) * 2, by = (k >> 1) * 2;
                    for (int j = 0; j < 2; j++)
                        for (int i = 0; i < 2; i++)
                            if (y + by + j < c->h && x + bx + i < c->w)
                                dst[(by + j) * c->w + bx + i] = yuv(e, j * 2 + i);
                }
            }
        }
    }
}

int cinepak_decode(Cinepak *c, const uint8_t *data, size_t size)
{
    if (size < 10) return -1;
    const uint8_t *end = data + size;
    int frame_flags = data[0];
    int nstrips = (int)be16(data + 8);
    if (nstrips > 32) return -1;
    const uint8_t *p = data + 10;
    int y0 = 0;
    for (int s = 0; s < nstrips; s++) {
        if (p + 12 > end) return -1;
        unsigned ssize = be16(p + 2);
        int sh = (int)be16(p + 8);
        const uint8_t *send = p + ssize;
        if (send > end || ssize < 12) return -1;
        if (s > 0 && !(frame_flags & 1)) {
            memcpy(c->v1[s], c->v1[s - 1], sizeof c->v1[s]);
            memcpy(c->v4[s], c->v4[s - 1], sizeof c->v4[s]);
        }
        const uint8_t *q = p + 12;
        while (q + 4 <= send) {
            unsigned id = be16(q), csize = be16(q + 2);
            if (csize < 4 || q + csize > send) break;
            const uint8_t *cd = q + 4, *ce = q + csize;
            switch (id & 0xFF00) {
            case 0x2000: case 0x2100: case 0x2400: case 0x2500: load_codebook(c->v4[s], id, cd, ce); break;
            case 0x2200: case 0x2300: case 0x2600: case 0x2700: load_codebook(c->v1[s], id, cd, ce); break;
            case 0x3000: case 0x3100: case 0x3200: decode_vectors(c, s, id >> 8, cd, ce, y0, y0 + sh); break;
            default: break;
            }
            q += csize;
        }
        y0 += sh;
        p = send;
    }
    return 0;
}
