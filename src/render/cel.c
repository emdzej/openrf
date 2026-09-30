/* Cel rasterisers for explicit-corner CCBs (Cel_DrawList 0x4262f0 dispatch):
   Cel_DrawTrapezoid 0x40fe4f (mode 0xc, floor), Cel_DrawQuad 0x410b9c + Cel_ScanEdge 0x410120
   (models and water floor, modes 0-5/0x10-0x13), Cel_DrawShadowSpans 0x411ba5 (mode 0xd).
   Straight port of tools/view.py (Raster); intermediate edge maths in 64 bits like the Python. */
#include "render.h"
#include <string.h>

static const SpriteBank *sb;
static const uint8_t *remap_override;     /* cel_draw_px: runtime PLUT for mode 0x11 */

void cel_init(const SpriteBank *b) { sb = b; }

static int64_t cdiv(int64_t a, int64_t b) { return a / b; }   /* C division, truncates */

/* Mode 0x10 key colours (PRE1 bytes set at runtime for CCB 452-456 / 524-578). */
static const uint8_t *keyblend(int idx)
{
    static const uint8_t k524[4] = { 0x7d, 0x7c, 0x7b, 0x7a }, k452[4] = { 0x0a, 0x7a, 0x7c, 0x80 };
    if (idx >= 524 && idx <= 578) return k524;
    if (idx >= 452 && idx <= 456) return k452;
    return NULL;
}

/* ---- pixel ops (Span_* per internal mode) ---- */
enum { OP_KEY, OP_COPY, OP_TABLE, OP_BLEND, OP_BRIGHT, OP_KEYBLEND, OP_PLUT };
typedef struct {
    int op;
    const uint8_t *tab;        /* OP_TABLE darken row, OP_PLUT remap */
    int tablen;
    uint8_t keys[5];
} PixOp;

static void pixop_init(PixOp *p, const Sprite *s, int idx)
{
    int mode = (int)s->mode;
    const uint8_t *kb = keyblend(idx);
    memset(p, 0, sizeof *p);
    if (kb) mode = 0x10;
    switch (mode) {
    case 0: case 0x13: case 0xc:
        p->op = ((s->flags & 0x20) && mode == 0) ? OP_COPY : OP_KEY; break;
    case 1: case 2:
        p->op = OP_TABLE; p->tab = sb->shade[mode == 1 ? 4 : 2]; break;
    case 3: case 4: p->op = OP_BLEND; break;
    case 5: p->op = OP_BRIGHT; break;
    case 0x10:
        p->op = OP_KEYBLEND;
        memcpy(p->keys, kb ? kb : (const uint8_t[4]){ 0x0b, 0x0b, 0x0b, 0x0b }, 4);
        p->keys[4] = 0x0b;
        break;
    case 0x11:
        p->op = OP_PLUT;
        if (remap_override) { p->tab = remap_override; p->tablen = 16; }
        else if (s->plut < sb->size) {
            p->tab = sb->data + s->plut;
            p->tablen = sb->size - s->plut < 256 ? (int)(sb->size - s->plut) : 256;
        } else p->op = OP_KEY;
        break;
    default: p->op = OP_KEY; break;
    }
}

static inline void pixop(const PixOp *p, uint8_t *d, uint8_t s)
{
    switch (p->op) {
    case OP_COPY: *d = s; return;
    case OP_KEY: if (s) *d = s; return;
    case OP_TABLE: if (s) *d = p->tab[*d]; return;
    case OP_BLEND: if (s) *d = sb->blend[s][*d]; return;
    case OP_BRIGHT: if (s && s < 32) *d = sb->bright[s][*d]; return;
    case OP_KEYBLEND:
        if (s) {
            bool k = s == p->keys[0] || s == p->keys[1] || s == p->keys[2] || s == p->keys[3] || s == p->keys[4];
            *d = k ? sb->blend[s][*d] : s;
        }
        return;
    case OP_PLUT: if (s) *d = s < p->tablen ? p->tab[s] : s; return;
    }
}

/* ---- Cel_DrawTrapezoid 0x40fe4f (mode 0xc) ---- */
static void trapezoid(Framebuffer *fb, const ClipRect *c, const int *p, const Sprite *s)
{
    int w = s->w, h = s->h;
    const uint8_t *src = s->pixels;
    if (!src) return;
    int y = p[1], n = p[7] - p[1];
    if (n <= 0) return;
    int64_t L = (int64_t)p[0] << 16, R = (int64_t)p[2] << 16;
    int64_t dL = cdiv(((int64_t)p[6] << 16) - ((int64_t)p[0] << 16), n);
    int64_t dR = cdiv(((int64_t)p[4] << 16) - ((int64_t)p[2] << 16), n);
    int64_t dv = cdiv(((int64_t)h << 16) - 1, n), V = 0;
    if (y < c->y0) {
        int k = c->y0 - y;
        if (k > n) return;
        L += dL * k; R += dR * k; V = dv * k; n -= k; y = c->y0;
    } else if (y > c->y1) return;
    if (c->y1 < y + n) n = c->y1 - y;
    bool opaque = s->flags & 0x20;
    int xr_lim = c->x1 + 1;
    for (;;) {
        int xl = (int)(L >> 16);
        int span = (int)(R >> 16) - xl;
        if (span > 0) {
            int64_t du = cdiv((int64_t)w << 16, span), U = 0;
            int cnt = -1;
            if (xl < c->x0) {
                int k = c->x0 - xl;
                if (k <= span) { U = k * du; xl += k; cnt = span - k; }
            } else if (xl <= xr_lim) cnt = span;
            if (cnt >= 0) {
                if (xl + cnt > xr_lim) cnt = xr_lim - xl;
                const uint8_t *row = src + (V >> 16) * w;
                uint8_t *o = fb->pixels + y * fb->w + xl;
                for (int i = 0; i < cnt; i++, o++) {
                    uint8_t px = row[U >> 16];
                    U += du;
                    if (px || opaque) *o = px;
                }
            }
        }
        if (n < 1) return;
        L += dL; R += dR; V += dv; y++; n--;
    }
}

/* ---- Cel_DrawShadowSpans 0x411ba5 (mode 0xd): s16 row offsets, u8 x0,x1 pairs in 1/256 width ---- */
static void shadow_spans(Framebuffer *fb, const ClipRect *c, const int *p, const Sprite *s)
{
    const uint8_t *d = sb->data;
    size_t sp = s->src;
    const uint8_t *tbl = sb->shade[4];
    int h = s->h;
    if (sp + (size_t)h * 2 > sb->size) return;
    int y = p[1], n = p[7] - p[1] + 1;
    if (n <= 0) return;
    int64_t L = (int64_t)p[0] << 16, R = (int64_t)p[2] << 16;
    int64_t dL = cdiv(((int64_t)p[6] << 16) - ((int64_t)p[0] << 16), n);
    int64_t dR = cdiv(((int64_t)p[4] << 16) - ((int64_t)p[2] << 16), n);
    int64_t dv = cdiv(((int64_t)h << 16) - 1, n), V = 0;
    if (y < c->y0) {
        int k = c->y0 - y;
        if (n < k) return;
        L += dL * k; R += dR * k; V = dv * k; n -= k; y = c->y0;
    } else if (y > c->y1) return;
    if (c->y1 < n + y) n = c->y1 - y;
    for (;;) {
        size_t ra = sp + (size_t)(V >> 16) * 2;
        int16_t ro = (int16_t)(d[ra] | d[ra + 1] << 8);
        if (ro) {
            size_t q = sp + ro;
            int64_t wid = (R - L) >> 4;
            for (;;) {
                int x0 = (int)((((int64_t)d[q] * wid >> 4) + L) >> 16);
                if (x0 <= c->x1) {
                    if (x0 < c->x0) x0 = c->x0;
                    int x1 = (int)((((int64_t)d[q + 1] * wid >> 4) + L) >> 16);
                    if (x1 >= c->x0) {
                        if (x1 > c->x1) x1 = c->x1;
                        uint8_t *o = fb->pixels + y * fb->w;
                        for (int x = x0; x <= x1; x++) o[x] = tbl[o[x]];
                    }
                }
                q += 2;
                if (d[q] == 0) break;
            }
        }
        if (n < 1) return;
        L += dL; R += dR; V += dv; y++; n--;
    }
}

/* ---- Cel_DrawQuad 0x410b9c + Cel_ScanEdge 0x410120 ---- */
typedef struct { int64_t dx, du, dv, x, u, v; int y0, y1, ky; int64_t kx; } Edge;

typedef struct {
    Edge e[4];
    int n;
    int64_t bmin, bmax;
} EdgeList;

static void scan_edge(EdgeList *L, const ClipRect *c, int x1, int y1, int x2, int y2,
                      int64_t u1, int64_t v1, int64_t u2, int64_t v2)
{
    if (x1 < L->bmin) L->bmin = x1;
    if (x2 < L->bmin) L->bmin = x2;
    if (x1 > L->bmax) L->bmax = x1;
    if (x2 > L->bmax) L->bmax = x2;
    int64_t du = (u2 - u1) + 1, dv = (v2 - v1) + 1;
    Edge e;
    if (y1 < y2) {
        if (y2 < c->y0 || c->y1 < y1) return;
        int k = y1 - y2;
        e = (Edge){ cdiv((int64_t)(x1 - x2) << 16, k), -cdiv(du, k), -cdiv(dv, k),
                    (int64_t)x1 << 16, u1, v1, y1, y2 < c->y1 ? y2 : c->y1, 0, 0 };
    } else if (y1 == y2) {
        if (x1 == x2) return;
        if (y1 < c->y0 || c->y1 < y1) return;
        if (x1 < x2) e = (Edge){ 0x7fffffff, 0x7fffffff, 0, (int64_t)x1 << 16, u1, v1, y1, y1, 0, 0 };
        else e = (Edge){ 0x7fffffff, 0x7fffffff, 0, (int64_t)x2 << 16, u2, v2, y1, y1, 0, 0 };
    } else {
        if (y1 < c->y0 || c->y1 < y2) return;
        int k = y2 - y1;
        e = (Edge){ cdiv((int64_t)(x2 - x1) << 16, k), cdiv(du, k), cdiv(dv, k),
                    (int64_t)x2 << 16, u2, v2, y2, y1 < c->y1 ? y1 : c->y1, 0, 0 };
    }
    e.ky = e.y0; e.kx = e.x;
    /* sorted insert by (start y, start x, dx) */
    int pos = 0;
    while (pos < L->n) {
        const Edge *o = &L->e[pos];
        if (e.ky < o->ky) break;
        if (e.ky == o->ky && (o->kx > e.kx || (o->kx == e.kx && o->dx >= e.dx))) break;
        pos++;
    }
    memmove(&L->e[pos + 1], &L->e[pos], (size_t)(L->n - pos) * sizeof(Edge));
    L->n++;
    Edge *ne = &L->e[pos];
    *ne = e;
    if (ne->y0 < c->y0) {
        int k = c->y0 - ne->y0;
        ne->x += ne->dx * k; ne->y0 = c->y0; ne->u += ne->du * k; ne->v += ne->dv * k;
    }
}

typedef struct {
    Framebuffer *fb;
    const ClipRect *c;
    const uint8_t *src;
    int w, h;
    PixOp op;
} QuadCtx;

/* Cel_TexSpanRows 0x4104d5: inclusive spans [L>>16, R>>16], affine u/v. */
static int spans(QuadCtx *q, int y, int yend, Edge *le, Edge *re)
{
    if (y > yend) return y;
    const ClipRect *c = q->c;
    for (int r = yend - y + 1; r > 0; r--) {
        int64_t xl = le->x >> 16, xr = re->x >> 16;
        int64_t xe = xr < c->x1 ? xr : c->x1;
        int64_t n = xr - xl + 1, du = 0, dv = 0;
        if (n != 0) { dv = cdiv(re->v - le->v, n); du = cdiv(re->u - le->u, n); }
        int64_t U = le->u, V = le->v;
        if (xl < c->x0) {
            int64_t k = c->x0 - xl;
            U += k * du; V += k * dv; xl = c->x0;
        }
        if (xl <= xe && y >= 0 && y < q->fb->h) {
            uint8_t *o = q->fb->pixels + (size_t)y * q->fb->w + xl;
            for (int64_t x = xl; x <= xe; x++, o++) {
                int64_t uu = U >> 16, vv = V >> 16;
                if (uu >= 0 && uu < q->w && vv >= 0 && vv < q->h) pixop(&q->op, o, q->src[vv * q->w + uu]);
                U += du; V += dv;
            }
        }
        le->x += le->dx; le->u += le->du; le->v += le->dv;
        re->x += re->dx; re->u += re->du; re->v += re->dv;
        y++;
    }
    return y;
}

static void quad(Framebuffer *fb, const ClipRect *c, const int *p, const Sprite *s, int idx)
{
    if (!s->pixels) return;
    EdgeList L = { .n = 0, .bmin = 0x7fffffff, .bmax = -0x7fffffff };
    int64_t W1 = (int64_t)(s->w - 1) << 16, H1 = (int64_t)(s->h - 1) << 16;
    scan_edge(&L, c, p[0], p[1], p[2], p[3], 0, 0, W1, 0);
    scan_edge(&L, c, p[2], p[3], p[4], p[5], W1, 0, W1, H1);
    scan_edge(&L, c, p[6], p[7], p[4], p[5], 0, H1, W1, H1);
    scan_edge(&L, c, p[0], p[1], p[6], p[7], 0, 0, 0, H1);
    if (!(c->x0 <= L.bmax && L.bmin <= c->x1 && L.n)) return;
    QuadCtx q = { fb, c, s->pixels, s->w, s->h, { 0 } };
    pixop_init(&q.op, s, idx);
    int ri = 0;
    Edge *a = &L.e[ri++];
    if (ri >= L.n) return;
    Edge *b = &L.e[ri++], *le, *re;
    if (b->kx < a->kx) { le = b; re = a; } else { le = a; re = b; }
    int y = le->y0;
    if (le->y0 != le->y1 && re->y0 != re->y1)
        y = spans(&q, y, ri < L.n ? L.e[ri].y0 : re->y1, le, re);
    while (ri < L.n) {
        Edge *e = &L.e[ri++];
        if (e->y0 == le->y1) le = e; else re = e;
        y = spans(&q, y, ri < L.n ? L.e[ri].y0 : re->y1, le, re);
    }
}

/* Dispatch (Cel_DrawList 0x4262f0). corners: 4 x (x,y) 16.16 relative to the clip origin in
   logical (320x240) units; scale 2 renders hi-res (corners >>15). mode -1 = the sprite's PRE0. */
void cel_draw(Framebuffer *fb, const ClipRect *clip, int scale, int idx, const int32_t corners[8], int mode)
{
    if (!sb || idx < 0 || idx >= sb->count) return;
    const Sprite *s = &sb->spr[idx];
    int m = mode < 0 ? (int)s->mode : mode;
    int sh = scale == 2 ? 15 : 16, p[8];
    for (int k = 0; k < 4; k++) {
        p[k * 2] = (corners[k * 2] >> sh) + clip->x0;
        p[k * 2 + 1] = (corners[k * 2 + 1] >> sh) + clip->y0;
    }
    if (m == 0xc) trapezoid(fb, clip, p, s);
    else if (m == 0xd) shadow_spans(fb, clip, p, s);
    else if ((m >= 0 && m <= 5) || (m >= 0x10 && m <= 0x13)) quad(fb, clip, p, s, idx);
}

/* Same dispatch for a patched CCB copy: p = 4 corners already in screen pixels (Cel_ComputeCorners /
   Cel_ExplicitCornersToScreen output), s = sprite with the CCB's flags/pixels, remap = runtime PLUT
   for mode 0x11 (NULL = the sprite's). */
void cel_draw_px(Framebuffer *fb, const ClipRect *clip, const Sprite *s, int idx, const int p[8], int mode,
                 const uint8_t *remap)
{
    if (!sb) return;
    Sprite t = *s;
    t.mode = (uint32_t)mode;
    if (mode == 0xc) trapezoid(fb, clip, p, &t);
    else if (mode == 0xd) shadow_spans(fb, clip, p, &t);
    else if ((mode >= 0 && mode <= 5) || (mode >= 0x10 && mode <= 0x13)) {
        remap_override = remap;
        quad(fb, clip, p, &t, idx);
        remap_override = NULL;
    }
}
