/* Patched-CCB drawing: Cel_DrawList 0x4262f0 for the cels the UI/HUD code builds by hand.
   Textured modes go through the shared rasterisers in cel.c (cel_draw_px); the 1:1 blits
   (Cel_BlitUnscaled2xH 0x41124d, Cel_BlitUnscaledB 0x4118ba, Cel_BlitUnscaledE 0x41100f) and the
   rectangle ops (Cel_FillRect 0x411628, Cel_ShadeRect 0x41176c) live here. */
#include "ccb.h"
#include <string.h>

static const SpriteBank *sb;

void ccb_init(const SpriteBank *b) { sb = b; }
const SpriteBank *ccb_bank(void) { return sb; }

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

Ccb ccb_get(int idx)
{
    Ccb c;
    memset(&c, 0, sizeof c);
    c.idx = -1;
    if (!sb || idx < 0 || idx >= sb->count) { c.flags = CCB_F_SKIP; return c; }
    const uint8_t *p = sb->data + 0x10 + (size_t)idx * 0x44;
    const Sprite *s = &sb->spr[idx];
    c.idx = idx;
    c.flags = rd32(p);
    c.src = s->pixels;
    for (int k = 0; k < 8; k++) c.v[k] = (int32_t)rd32(p + 0x10 + k * 4);
    c.mode = (int)s->mode;
    c.p1 = (int32_t)s->param;
    c.w = s->w;
    c.h = s->h;
    return c;
}

void ccb_set_scale(Ccb *c, int32_t w, int32_t h)             /* Cel_SetScale 0x4336c0 */
{
    const uint8_t *sh = NULL;
    uint32_t tab = sb ? rd32(sb->data + 0xc) : 0;
    if (sb && c->idx >= 0 && tab + (size_t)(c->idx + 1) * 8 <= sb->size) sh = sb->data + tab + c->idx * 8;
    c->v[CCB_HDDY] = c->v[CCB_HDDX] = c->v[CCB_VDX] = c->v[CCB_HDY] = 0;
    int32_t ws = sh ? (int32_t)rd32(sh) : 0, hs = sh ? (int32_t)rd32(sh + 4) : 0;
    int32_t x = w << 4;
    c->v[CCB_HDX] = ws ? x >> ws : (c->w ? x / c->w : 0);
    c->v[CCB_VDY] = hs ? h >> hs : (c->h ? h / c->h : 0);
}

void ccb_subrect(Ccb *c, int x, int y, int w, int h)         /* FUN_00433570 (template == NULL) */
{
    c->mode = 6;
    if (w < 1) w = c->w - x;
    if (h < 1) h = c->h - y;
    c->h = h;
    c->p1 = w;
    if (c->src) c->src += (size_t)c->w * y + x;
}

uint8_t ccb_rgb555_index(uint16_t v)
{
    int r = ((v >> 10) & 0x1f) << 3, g = ((v >> 5) & 0x1f) << 3, b = (v & 0x1f) << 3;
    int best = 10, bd = 1 << 30;
    for (int i = 10; i < 246; i++) {
        const RGB *p = &sb->palette[i];
        int d = (p->r - r) * (p->r - r) + (p->g - g) * (p->g - g) + (p->b - b) * (p->b - b);
        if (d < bd) { bd = d; best = i; }
    }
    return (uint8_t)best;
}

/* Cel_ComputeCorners 0x4260a0 (sh 16) / Cel_ComputeCorners2x 0x426180 (sh 15). */
static void compute_corners(const Ccb *c, const ClipRect *clip, int sh, int p[8])
{
    const int32_t *v = c->v;
    int32_t w = c->w, h = c->h;
    int x = (v[CCB_X] >> sh) + clip->x0, y = (v[CCB_Y] >> sh) + clip->y0;
    int32_t vx = (int32_t)((int64_t)v[CCB_VDX] * h), vy = (int32_t)((int64_t)v[CCB_VDY] * h);
    p[0] = x;
    p[1] = y;
    p[2] = ((int32_t)((int64_t)(v[CCB_HDX] >> 4) * w) >> sh) + x;
    p[3] = ((int32_t)((int64_t)(v[CCB_HDY] >> 4) * w) >> sh) + y;
    p[4] = ((int32_t)((int64_t)((int32_t)((int64_t)v[CCB_HDDX] * h + v[CCB_HDX]) >> 4) * w + vx) >> sh) + x;
    p[5] = ((int32_t)((int64_t)((int32_t)((int64_t)v[CCB_HDDY] * h + v[CCB_HDY]) >> 4) * w + vy) >> sh) + y;
    p[6] = (vx >> sh) + x;
    p[7] = (vy >> sh) + y;
}

static void corners(const Ccb *c, const ClipRect *clip, int sh, int p[8])
{
    if (c->flags & CCB_F_EXPLICIT)                            /* Cel_ExplicitCornersToScreen 0x426260 */
        for (int k = 0; k < 4; k++) { p[k * 2] = (c->v[k * 2] >> sh) + clip->x0; p[k * 2 + 1] = (c->v[k * 2 + 1] >> sh) + clip->y0; }
    else compute_corners(c, clip, sh, p);
}

enum { BLIT_KEY, BLIT_COPY, BLIT_BLEND };

/* 1:1 blit of sw x sh source pixels at (x, y), pixel-doubled when scale == 2. */
static void blit(Framebuffer *fb, const ClipRect *clip, const uint8_t *src, int stride, int sw, int shh,
                 int x, int y, int scale, int op)
{
    if (!src || sw <= 0 || shh <= 0) return;
    int x0 = x, y0 = y, x1 = x + sw * scale - 1, y1 = y + shh * scale - 1;
    if (x0 < clip->x0) x0 = clip->x0;
    if (y0 < clip->y0) y0 = clip->y0;
    if (x1 > clip->x1) x1 = clip->x1;
    if (y1 > clip->y1) y1 = clip->y1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;
    for (int sy = y0; sy <= y1; sy++) {
        const uint8_t *row = src + (size_t)((sy - y) / scale) * stride;
        uint8_t *d = fb->pixels + (size_t)sy * fb->w;
        for (int sx = x0; sx <= x1; sx++) {
            uint8_t s = row[(sx - x) / scale];
            if (op == BLIT_COPY) d[sx] = s;
            else if (s) d[sx] = op == BLIT_BLEND ? sb->blend[s][d[sx]] : s;
        }
    }
}

/* Cel_FillRect / Cel_ShadeRect geometry: x = p0, w = p1.x - p0.x, h = p2.y - p0.y. */
static void rect_op(Framebuffer *fb, const ClipRect *clip, const int p[8], int fill, const uint8_t *tab)
{
    int x = p[0], y = p[1], w = p[2] - x, h = p[5] - y;
    if (x < clip->x0) { w -= clip->x0 - x; x = clip->x0; }
    if (y < clip->y0) { h -= clip->y0 - y; y = clip->y0; }
    if (clip->x1 <= x + w) w = clip->x1 - x + 1;
    if (clip->y1 <= y + h) h = clip->y1 - y + 1;
    if (x + w > fb->w) w = fb->w - x;
    if (y + h > fb->h) h = fb->h - y;
    if (w <= 0 || h <= 0 || x < 0 || y < 0) return;
    for (int j = 0; j < h; j++) {
        uint8_t *d = fb->pixels + (size_t)(y + j) * fb->w + x;
        if (tab) for (int i = 0; i < w; i++) d[i] = tab[d[i]];
        else memset(d, fill, (size_t)w);
    }
}

void ccb_draw(Framebuffer *fb, const ClipRect *clip, int scale, const Ccb *c)
{
    if (!sb || (c->flags & CCB_F_SKIP)) return;
    int sh = scale == 2 ? 15 : 16, p[8];
    switch (c->mode) {
    case 6:                                                   /* Cel_BlitUnscaled2xH: point at +0x10 */
        blit(fb, clip, c->src, c->w, c->p1 ? c->p1 : c->w, c->h, (c->v[0] >> sh) + clip->x0,
             (c->v[1] >> sh) + clip->y0, scale, (c->flags & CCB_F_OPAQUE) ? BLIT_COPY : BLIT_KEY);
        return;
    case 0xe:                                                 /* Cel_BlitUnscaledE */
        blit(fb, clip, c->src, c->w, c->w, c->h, (c->v[0] >> sh) + clip->x0, (c->v[1] >> sh) + clip->y0,
             scale, BLIT_KEY);
        return;
    case 0xb: case 0xf:                                       /* Cel_BlitUnscaledB: translucent */
        corners(c, clip, sh, p);
        blit(fb, clip, c->src, c->w, c->p1 ? c->p1 : c->w, c->h, p[0], p[1], scale, BLIT_BLEND);
        return;
    case 7: case 9: case 10: {                                /* Cel_FillRect */
        corners(c, clip, sh, p);
        int col = 0;
        if (c->mode == 9) col = (uint8_t)c->p1;
        else if (c->mode == 10) {
            const uint8_t *pl = c->plut ? c->plut : (c->idx >= 0 ? sb->data + sb->spr[c->idx].plut : NULL);
            col = pl ? ccb_rgb555_index((uint16_t)(pl[2] | pl[3] << 8)) : 0;
        }
        rect_op(fb, clip, p, col, NULL);
        return;
    }
    case 8: {                                                 /* Cel_ShadeRect: T_SHADE[p1 >> 11] */
        int32_t l = c->p1 > 0xffff ? 0xffff : c->p1;
        if (l < 0) l = 0;
        corners(c, clip, sh, p);
        rect_op(fb, clip, p, 0, sb->shade[l >> 11]);
        return;
    }
    default: {
        if (!((c->mode >= 0 && c->mode <= 5) || (c->mode >= 0x10 && c->mode <= 0x13) || c->mode == 0xc || c->mode == 0xd))
            return;
        corners(c, clip, sh, p);
        Sprite s = { 0 };
        if (c->idx >= 0) s = sb->spr[c->idx];
        s.flags = c->flags;
        s.w = c->w;
        s.h = c->h;
        s.pixels = c->src;
        cel_draw_px(fb, clip, &s, c->idx, p, c->mode, c->plut);
        return;
    }
    }
}
