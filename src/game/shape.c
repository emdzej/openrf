/* Shape overlap tests, ported from 0x41b8d0..0x41c930. All maths in 16.16; the segment test works in
   whole pixels (>>16) exactly like the original. */
#include "shape.h"
#include "fixmath.h"
#include <stddef.h>

ShapePart *g_hit_part_a, *g_hit_part_b;
int32_t *g_move_old_pos;

/* DAT_00471050: scratch buffer for transformed vertices (two polygons back to back). */
static int32_t vbuf[128][3];

/* FUN_0041ab70: out[i] = in[i] + pos (3 components). */
static void verts_add(int32_t (*out)[3], const int32_t (*in)[3], const int32_t *pos, int n)
{
    for (int i = 0; i < n; i++) {
        out[i][0] = pos[0] + in[i][0];
        out[i][1] = in[i][1] + pos[1];
        out[i][2] = in[i][2] + pos[2];
    }
}

/* Rotate (if the heading has a non-zero integer part) and translate a polygon into `out`. */
static void poly_place(int32_t (*out)[3], const ShapePart *p, const int32_t *pos, uint32_t heading)
{
    const int32_t (*src)[3] = (const int32_t (*)[3])p->verts;
    if (heading & 0xffff0000u) {
        mat_transform_verts(out, src, rot_mat[(int32_t)heading >> 16], p->nverts);
        src = (const int32_t (*)[3])out;
    }
    verts_add(out, src, pos, p->nverts);
}

/* FUN_0041b8d0 */
int point_in_box(const int32_t *p, const int32_t *o, const int32_t *box)
{
    return box[0] <= p[0] - o[0] && p[0] - o[0] <= box[2] && box[1] <= p[1] - o[1] && p[1] - o[1] <= box[3];
}

/* FUN_0041b910: do box a (at pa) and box b (at pb) overlap. */
static int boxes_overlap(const int32_t *pa, const int32_t *a, const int32_t *pb, const int32_t *b)
{
    int32_t d = pa[0] - pb[0];
    if (d < 0) { if ((a[2] - b[0]) + d < 0) return 0; }
    else if ((b[2] - a[0]) - d < 0) return 0;
    d = pa[1] - pb[1];
    if (d < 0) { if ((a[3] - b[1]) + d < 0) return 0; }
    else if ((b[3] - a[1]) - d < 0) return 0;
    return 1;
}

/* FUN_0041b990: segment a1-a2 vs b1-b2 in whole pixels. 1 = cross, -1 = collinear overlap, 0 = no. */
static int seg_intersect(const int32_t *a1, const int32_t *a2, const int32_t *b1, const int32_t *b2)
{
    int32_t by = b1[1], ay = a1[1], bx = b1[0];
    int32_t dby = (b2[1] - by) >> 16, dax = (a2[0] - a1[0]) >> 16, day = (a2[1] - ay) >> 16;
    int32_t dbx = (b2[0] - bx) >> 16;
    int32_t den = dby * dax - day * dbx;
    int32_t ox = (bx - a1[0]) >> 16, oy = (by - ay) >> 16;
    int32_t na = day * ox - oy * dax;
    int32_t nb = dby * ox - oy * dbx;
    if (den == 0) {
        if (na != 0) return 0;
        if (bx != a2[0]) {
            int32_t lo = a2[0] <= a1[0] ? a1[0] : a2[0];      /* max(a1.x, a2.x) */
            int32_t hi = bx <= b2[0] ? bx : b2[0];              /* min(b1.x, b2.x) */
            if (hi <= lo) {
                int32_t m1 = b2[0] <= bx ? bx : b2[0];          /* max(b) */
                int32_t m2 = a1[0] <= a2[0] ? a1[0] : a2[0];    /* min(a) */
                if (m2 <= m1) return -1;
            }
            return 0;
        }
        int32_t lo = a2[1] <= ay ? ay : a2[1];
        int32_t hi = by <= b2[1] ? by : b2[1];
        if (hi <= lo) {
            int32_t m1 = b2[1] <= by ? by : b2[1];
            int32_t m2 = ay <= a2[1] ? ay : a2[1];
            if (m2 <= m1) return -1;
        }
        return 0;
    }
    if (den > 0) return (na >= 0 && na <= den && nb >= 0 && nb <= den) ? 1 : 0;
    return (na < 1 && den <= na && nb < 1 && den <= nb) ? 1 : 0;
}

/* FUN_0041bb30: does segment p1-p2 cross any edge of the placed polygon (vertices in buf). */
static int seg_cross_poly(const int32_t *p1, const int32_t *p2, const ShapePart *poly, int32_t (*buf)[3])
{
    const int32_t *prev = buf[poly->nedges - 1];
    for (int i = 0; i < poly->nedges; i++) {
        const int32_t *cur = buf[poly->edges[i]];
        if (seg_intersect(p1, p2, cur, prev)) return 1;
        prev = cur;
    }
    return 0;
}

/* FUN_0041bc10: box (at pb, extents box) vs polygon part at pp. */
static uint32_t box_vs_poly(const int32_t *pb, const int32_t *box, const int32_t *pp, uint32_t heading, const ShapePart *poly)
{
    int32_t d = pb[0] - pp[0];
    if (d < 0) { if ((box[2] - poly->bbox[0]) + d < 0) return 0; }
    else if ((poly->bbox[2] - box[0]) - d < 0) return 0;
    d = pb[1] - pp[1];
    if (d < 0) { if ((box[3] - poly->bbox[1]) + d < 0) return 0; }
    else if ((poly->bbox[3] - box[1]) - d < 0) return 0;
    d = pp[0] - pb[0];
    if (box[0] <= d && d <= box[2] && box[1] <= pp[1] - pb[1] && pp[1] - pb[1] <= box[3]) return 1;
    poly_place(vbuf, poly, pp, heading);
    int32_t x0 = box[0] + pb[0], x1 = box[2] + pb[0], y0 = pb[1] + box[1], y1 = box[3] + pb[1];
    const int32_t e[4][2][2] = { { { x0, y0 }, { x1, y0 } }, { { x1, y1 }, { x1, y0 } },
                                 { { x1, y1 }, { x0, y1 } }, { { x0, y1 }, { x0, y0 } } };
    for (int k = 0; k < 4; k++)
        if (seg_cross_poly(e[k][0], e[k][1], poly, vbuf)) return 1;
    return seg_cross_poly(pb, pp, poly, vbuf) ? 0 : 1;
}

/* FUN_0041bf50: polygon vs polygon. */
static uint32_t poly_vs_poly(const int32_t *pa, uint32_t ha, const ShapePart *a, const int32_t *pb, uint32_t hb, const ShapePart *b)
{
    int32_t d = pa[0] - pb[0];
    if (d < 0) { if ((a->bbox[2] - b->bbox[0]) + d < 0) return 0; }
    else if ((b->bbox[2] - a->bbox[0]) - d < 0) return 0;
    d = pa[1] - pb[1];
    if (d < 0) { if ((a->bbox[3] - b->bbox[1]) + d < 0) return 0; }
    else if ((b->bbox[3] - a->bbox[1]) - d < 0) return 0;
    int32_t (*ba)[3] = vbuf, (*bb)[3] = vbuf + a->nverts;
    poly_place(ba, a, pa, ha);
    poly_place(bb, b, pb, hb);
    const int32_t *prev_a = ba[a->nedges - 1];
    for (int i = 0; i < a->nedges; i++) {
        const int32_t *cur_a = ba[a->edges[i]];
        const int32_t *prev_b = bb[b->nedges - 1];
        for (int j = 0; j < b->nedges; j++) {
            const int32_t *cur_b = bb[b->edges[j]];
            if (seg_intersect(cur_a, prev_a, cur_b, prev_b)) return 1;
            prev_b = cur_b;
        }
        prev_a = cur_a;
    }
    prev_a = ba[a->nedges - 1];
    for (int i = 0; i < a->nedges; i++) {
        const int32_t *cur_a = ba[a->edges[i]];
        if (seg_intersect(pa, pb, cur_a, prev_a)) {
            const int32_t *prev_b = bb[b->nedges - 1];
            for (int j = 0; j < b->nedges; j++) {
                const int32_t *cur_b = bb[b->edges[j]];
                if (seg_intersect(pa, pb, cur_b, prev_b)) return 0;
                prev_b = cur_b;
            }
            return 1;
        }
        prev_a = cur_a;
    }
    return 1;
}

/* FUN_0041c1f0: swept segment part (type 4, at pa) vs box at pb. */
static int sweep_vs_box(const int32_t *pa, const ShapePart *a, const int32_t *pb, const int32_t *box)
{
    int32_t d = pb[0] - pa[0];
    if (d < 0) { if ((box[2] - a->bbox[0]) + d < 0) return 0; }
    else if ((a->bbox[2] - box[0]) - d < 0) return 0;
    d = pb[1] - pa[1];
    if (d < 0) { if ((box[3] - a->bbox[1]) + d < 0) return 0; }
    else if ((a->bbox[3] - box[1]) - d < 0) return 0;
    d = pa[0] - pb[0];
    if (box[0] <= d && d <= box[2] && box[1] <= pa[1] - pb[1] && pa[1] - pb[1] <= box[3]) return 1;
    int32_t s[2][3];
    verts_add(s, (const int32_t (*)[3])a->verts, pa, 2);
    int32_t x0 = box[0] + pb[0], x1 = box[2] + pb[0], y0 = pb[1] + box[1], y1 = box[3] + pb[1];
    const int32_t e[4][2][2] = { { { x0, y0 }, { x1, y0 } }, { { x1, y0 }, { x1, y1 } },
                                 { { x1, y1 }, { x0, y1 } }, { { x0, y1 }, { x0, y0 } } };
    for (int k = 0; k < 3; k++)
        if (seg_intersect(s[0], s[1], e[k][0], e[k][1])) return 1;
    return seg_intersect(s[0], s[1], e[3][0], e[3][1]) != 0;
}

/* FUN_0041c3a0: one part against one part. Positions include the part offsets. */
uint32_t shape_part_overlap(const int32_t *p1, uint32_t h1, ShapePart *a, const int32_t *p2, uint32_t h2, ShapePart *b)
{
    int32_t pa[3] = { a->dx + p1[0], p1[1] + a->dy, 0 };
    int32_t pb[3] = { b->dx + p2[0], p2[1] + b->dy, 0 };
    int ta = a->type;
    if (ta == 4 && !g_move_old_pos) ta = 1;          /* case 4 falls through to case 1 without a sweep */
    switch (ta) {
    case 2:
        switch (b->type) {
        case 1: case 4:
            return a->bbox[0] <= pb[0] - pa[0] && pb[0] - pa[0] <= a->bbox[2] &&
                   a->bbox[1] <= pb[1] - pa[1] && pb[1] - pa[1] <= a->bbox[3];
        case 2: {
            int32_t d = pa[0] - pb[0];
            if (d < 0) { if ((a->bbox[2] - b->bbox[0]) + d < 0) return 0; }
            else if ((b->bbox[2] - a->bbox[0]) - d < 0) return 0;
            d = pa[1] - pb[1];
            if (d < 0) { if ((a->bbox[3] - b->bbox[1]) + d < 0) return 0; }
            else if ((b->bbox[3] - a->bbox[1]) - d < 0) return 0;
            return 1;
        }
        case 3: return box_vs_poly(pa, a->bbox, pb, h2, b);
        }
        break;
    case 3:
        switch (b->type) {
        case 1: case 4:
            if (!point_in_box(pb, pa, a->bbox)) return 0;
            poly_place(vbuf, a, pa, h1);
            return seg_cross_poly(pb, pa, a, vbuf) == 0;
        case 2: return box_vs_poly(pb, b->bbox, pa, h1, a);
        case 3: return poly_vs_poly(pa, h1, a, pb, h2, b);
        }
        break;
    case 4:
        switch (b->type) {
        case 2: return (uint32_t)sweep_vs_box(pa, a, pb, b->bbox);
        case 3: {
            if (!boxes_overlap(pa, a->bbox, pb, b->bbox)) return 0;
            poly_place(vbuf, b, pb, h2);
            /* NB: the original tests the *relative* sweep vector a->verts[0..1] against the placed edges. */
            const int32_t *prev = vbuf[b->nedges - 1];
            for (int i = 0; i < b->nedges; i++) {
                const int32_t *cur = vbuf[b->edges[i]];
                if (seg_intersect(a->verts[0], a->verts[1], cur, prev)) return 1;
                prev = cur;
            }
            return seg_cross_poly(pa, pb, b, vbuf) == 0;
        }
        default: return 0;
        }
    case 1:
        switch (b->type) {
        case 2:
            return b->bbox[0] <= pa[0] - pb[0] && pa[0] - pb[0] <= b->bbox[2] &&
                   b->bbox[1] <= pa[1] - pb[1] && pa[1] - pb[1] <= b->bbox[3];
        case 3:
            if (!point_in_box(pa, pb, b->bbox)) return 0;
            poly_place(vbuf, b, pb, h2);
            return seg_cross_poly(pa, pb, b, vbuf) == 0;
        default: return 0;
        }
    }
    return 0;
}

/* ShapesOverlap 0x41c800: walk both part lists by height; returns (solid_a | solid_b) of the first
   overlapping pair (and records it in g_hit_part_a/b), else 0. */
uint8_t shapes_overlap(const int32_t *pa, uint32_t ha, ShapePart *a, const int32_t *pb, uint32_t hb, ShapePart *b)
{
    ShapePart *ca, *cb;
    uint8_t r;
    if (!a || !b) return 0;
    if ((a->mask & b->solid) == 0) return 0;
    for (;;) {
        for (;;) {
            for (;;) {
                cb = b; ca = a;
                if (!ca || !cb) return 0;
                if (ca->zlo + pa[2] <= cb->zhi + pb[2]) break;
                b = cb->next;
            }
            b = cb;
            if (cb->zlo + pb[2] <= ca->zhi + pa[2]) break;
            a = ca->next;
        }
        if ((cb->solid & ca->mask) && shape_part_overlap(pa, ha, ca, pb, hb, cb) && (r = ca->solid | cb->solid) != 0) break;
        ShapePart *na = ca->next, *nb = cb->next;
        if (!na) {
            b = nb;
            if (!nb) { a = NULL; b = cb; }
        } else {
            a = na;
            if (nb) {
                a = ca; b = nb;
                if (pa[2] + ca->zlo < pb[2] + cb->zlo) { a = na; b = cb; }
            }
        }
    }
    g_hit_part_a = ca;
    g_hit_part_b = cb;
    return r;
}

/* FUN_0041c930: rebuild a swept part from the movement old -> pos. */
void shape_sweep_setup(ShapePart *s, const int32_t *pos, const int32_t *old)
{
    int32_t d = pos[2] - old[2];
    if (d < 0) { s->zhi = 0; s->zlo = d; } else { s->zlo = 0; s->zhi = d; }
    d = old[0] - pos[0];
    s->verts[0][0] = d;
    if (d < 0) { s->bbox[2] = 0; s->bbox[0] = d; } else { s->bbox[0] = 0; s->bbox[2] = d; }
    d = old[1] - pos[1];
    s->verts[0][1] = d;
    if (d < 0) { s->bbox[1] = d; s->bbox[3] = 0; } else { s->bbox[3] = d; s->bbox[1] = 0; }
}
