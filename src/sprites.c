#include "sprites.h"
#include "assets.h"
#include "vfs.h"
#include <stdlib.h>
#include <string.h>

enum { HDR = 0x10, CCB_SIZE = 0x44, CCB_FLAG_BGND = 0x20 };

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* Windows reserves palette slots 0-9 and 246-255 (system colours). */
static const RGB win_static_lo[10] = { {0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},
    {128,0,128},{0,128,128},{192,192,192},{192,220,192},{166,202,240} };
static const RGB win_static_hi[10] = { {255,251,240},{160,160,164},{128,128,128},{255,0,0},
    {0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255} };

static void load_trans(SpriteBank *b);

bool sprites_load(SpriteBank *b, const char *rel)
{
    size_t sz;
    memset(b, 0, sizeof *b);
    b->data = vfs_read_all(rel, &sz);
    if (!b->data || sz < HDR || memcmp(b->data, "CCBA", 4)) return false;
    b->size = sz;
    b->count = (int)rd32(b->data + 8);
    if ((size_t)HDR + (size_t)b->count * CCB_SIZE > sz) return false;
    b->spr = calloc((size_t)b->count, sizeof *b->spr);
    for (int i = 0; i < b->count; i++) {
        const uint8_t *c = b->data + HDR + i * CCB_SIZE;
        Sprite *s = &b->spr[i];
        s->flags = rd32(c);
        s->mode = rd32(c + 0x34);
        s->param = rd32(c + 0x38);
        s->w = (int)rd32(c + 0x3C);
        s->h = (int)rd32(c + 0x40);
        uint32_t src = rd32(c + 8);
        s->src = src;
        s->plut = rd32(c + 0x0C);
        s->pixels = (src < sz && src + (size_t)s->w * s->h <= sz) ? b->data + src : NULL;
    }
    /* The LOGPALETTE handed to DirectDraw follows the 0x400-byte palette block. */
    uint32_t pal = rd32(b->data + HDR + 0x0C);
    const uint8_t *lp = b->data + pal + 0x400 + 4;
    for (int i = 0; i < 256; i++) b->palette[i] = (RGB){ lp[i * 4], lp[i * 4 + 1], lp[i * 4 + 2] };
    memcpy(b->palette, win_static_lo, sizeof win_static_lo);
    memcpy(b->palette + 246, win_static_hi, sizeof win_static_hi);
    load_trans(b);
    return true;
}

static int nearest(const RGB *pal, int r, int g, int bl)
{
    /* GetNearestPaletteIndex over the game's colour range (10..245). */
    int best = 10, bd = 1 << 30;
    for (int i = 10; i < 246; i++) {
        int dr = pal[i].r - r, dg = pal[i].g - g, db = pal[i].b - bl;
        int d = dr * dr + dg * dg + db * db;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void build_trans(SpriteBank *b)
{
    const RGB *p = b->palette;
    for (int a = 0; a < 256; a++)
        for (int c = 0; c < 256; c++)
            b->blend[a][c] = (uint8_t)nearest(p, (p[a].r + p[c].r) / 2, (p[a].g + p[c].g) / 2, (p[a].b + p[c].b) / 2);
    for (int k = 0; k < 32; k++)
        for (int c = 0; c < 256; c++) {
            b->shade[k][c] = (uint8_t)nearest(p, p[c].r * (31 - k) / 32, p[c].g * (31 - k) / 32, p[c].b * (31 - k) / 32);
            int r = p[c].r + 3 * (k + 1), g = p[c].g + 3 * (k + 1), bl = p[c].b + 3 * (k + 1);
            b->bright[k][c] = (uint8_t)nearest(p, r > 255 ? 255 : r, g > 255 ? 255 : g, bl > 255 ? 255 : bl);
        }
}

static void load_trans(SpriteBank *b)
{
    size_t sz;
    uint8_t *t = vfs_read_all("ART/TRANS.TBL", &sz);
    if (t && sz == 4 + sizeof b->blend + sizeof b->shade + sizeof b->bright && t[0] == 1) {
        memcpy(b->blend, t + 4, sizeof b->blend);
        memcpy(b->shade, t + 4 + sizeof b->blend, sizeof b->shade);
        memcpy(b->bright, t + 4 + sizeof b->blend + sizeof b->shade, sizeof b->bright);
    } else {
        build_trans(b);   /* the original regenerates it the same way when missing */
    }
    free(t);
}

void sprites_free(SpriteBank *b)
{
    free(b->spr);
    free(b->data);
    memset(b, 0, sizeof *b);
}

void sprite_draw(const SpriteBank *b, int idx, Framebuffer *fb, int x, int y)
{
    if (idx < 0 || idx >= b->count) return;
    const Sprite *s = &b->spr[idx];
    if (!s->pixels) return;
    /* TODO: shadow (0xD), translucent and remap modes; see docs/car.md. */
    bool opaque = (s->flags & CCB_FLAG_BGND) && s->mode == 0;
    int x0 = x < 0 ? -x : 0, y0 = y < 0 ? -y : 0;
    int x1 = x + s->w > fb->w ? fb->w - x : s->w;
    int y1 = y + s->h > fb->h ? fb->h - y : s->h;
    if (x0 >= x1 || y0 >= y1) return;
    for (int j = y0; j < y1; j++) {
        const uint8_t *src = s->pixels + j * s->w;
        uint8_t *dst = fb->pixels + (y + j) * fb->w + x;
        if (opaque) memcpy(dst + x0, src + x0, (size_t)(x1 - x0));
        else for (int i = x0; i < x1; i++) if (src[i]) dst[i] = src[i];
    }
}
