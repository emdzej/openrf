/* Collision shapes (ShapesOverlap 0x41c800 and helpers 0x41b8d0..0x41c3a0). See docs/game.md §4. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* One part of a shape (0x3c bytes in the original). Parts are chained by `next`, ordered top-down by z.
   type 1 = point, 2 = axis-aligned box, 3 = convex polygon (rotated by heading), 4 = swept segment
   (projectiles; rebuilt from the previous position by FUN_0041c930). */
typedef struct ShapePart ShapePart;
struct ShapePart {
    int32_t type;          /* +0x00 */
    ShapePart *next;       /* +0x04 */
    uint8_t b8;            /* +0x08 pickup flag (bit 1 = resupply/gate part, see FUN_0041f340) */
    uint8_t kind;          /* +0x09 pickup kind: 1 fuel, 2 ammo, 3 gate (FUN_00429040); bush: 4 = trunk-less */
    uint8_t solid;         /* +0x0a "what I am" bits (1 static, 2 vehicle, 0x40 bridge deck...) */
    uint8_t mask;          /* +0x0b "what I collide with" bits */
    int32_t zlo, zhi;      /* +0x0c/+0x10 vertical extent relative to the object z */
    int32_t dx, dy;        /* +0x14/+0x18 offset of the part */
    int32_t bbox[4];       /* +0x1c..+0x28 x0,y0,x1,y1 (type 2/3/4) */
    int32_t nverts;        /* +0x2c */
    int32_t (*verts)[3];   /* +0x30 (x,y,z) 16.16; mutable for type 4 */
    int32_t nedges;        /* +0x34 */
    const uint8_t *edges;  /* +0x38 vertex index per edge end */
    uint32_t addr;         /* original VA, for debugging */
};

enum { GADJ_NONE, GADJ_JITTER };     /* graphic +0x28: 0x41f210 = per-cell random offset (bushes, palms, rocks) */

/* Graphic/shape descriptor (the renderer uses more fields: +0 draw fn, +4 model, ...). */
typedef struct {
    uint32_t addr;
    ShapePart *shape;      /* +0x08 */
    int32_t radius;        /* +0x0c extent used to decide which neighbour cells ObjCheckCollision visits */
    uint8_t adjust;        /* +0x28 */
} Gfx;

/* DAT_00480ea4 / DAT_00480ea8: the two parts that overlapped in the last successful ShapesOverlap. */
extern ShapePart *g_hit_part_a, *g_hit_part_b;
/* DAT_00480e38: previous position during ObjMove (enables swept type-4 tests); NULL otherwise. */
extern int32_t *g_move_old_pos;

uint8_t shapes_overlap(const int32_t *pa, uint32_t ha, ShapePart *a, const int32_t *pb, uint32_t hb, ShapePart *b);
uint32_t shape_part_overlap(const int32_t *pa, uint32_t ha, ShapePart *a, const int32_t *pb, uint32_t hb, ShapePart *b); /* 0x41c3a0 */
int point_in_box(const int32_t *p, const int32_t *origin, const int32_t *box);  /* FUN_0041b8d0 */
void shape_sweep_setup(ShapePart *s, const int32_t *pos, const int32_t *old);  /* FUN_0041c930 */
