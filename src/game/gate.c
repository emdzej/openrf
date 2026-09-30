/* Gates: class Gate (13, descriptor 0x44ade0): GateInit 0x423cd0, GateUpdate 0x423d50, GateDestroy 0x423ec0,
   hit_static 0x423fa0 (never blocks), damage 0x423f50; SpawnGate 0x423fb0 (from the vehicle resupply check
   FUN_00429040 when an own vehicle touches the gate's pickup zone, part kind 3).
   While the Gate object exists the cell's BNO (43 H / 44 V) is removed and the object draws the gate with
   the same model (the wrappers 0x4175a0..0x4176f0 narrow the leaves by obj+0x64). */
#include "rules.h"
#include <string.h>

enum { SND_GATE_MOVE = 0x43ea50, SND_GATE_CLOSE = 0x43ea68, GATE_FULL = 0xf0000 };

/* GateInit's 0x104-byte heap copy of the graphic: the two door parts it moves. */
typedef struct { Gfx g; ShapePart part[2]; int vertical; } GateExt;
static GateExt gate_ext[OBJ_POOL];

static void snd(uint32_t ev, Obj *o) { if (game_hooks.sound) game_hooks.sound(1, ev, o); }

/* FUN_00417b50: BnoDamage without the destroy: 1 when the cell's hit points reach 0. */
static int cell_hp_damage(int32_t amount, Obj *src, uint32_t *cell, const BnoDef *d)
{
    (void)src;
    const BnoDef *p = d ? d : &bno_defs[CELL_BNO(*cell)];
    uint32_t hp = *cell & 0xe000000u;
    if (!hp) return 0;
    int32_t v = amount - p->armour;
    if (v <= 0) return 0;
    if (p->mult) v *= p->mult;
    v >>= 16;
    if (v < 1) v = 1;
    int32_t h = (int32_t)(hp >> 25);
    if (h <= v) { *cell &= 0xf1ffffffu; return 1; }
    int32_t nh = h - v;
    if (h > 1 && nh < 2 && (p->flags & 0xff8000u) && game_hooks.spawn_men) game_hooks.spawn_men(cell, p);
    *cell = (*cell & ~0xe000000u) | (((uint32_t)nh << 25) & 0xe000000u);
    return 0;
}

static int gate_init(const ObjClass *c, Obj *o, void *arg)            /* GateInit 0x423cd0 */
{
    (void)c;
    const Gfx *t = arg;
    GateExt *x = &gate_ext[o->id & 0x1ff];
    x->g = *t;
    x->vertical = t == &rgfx_442d70;                                   /* copy +0x100 */
    x->part[0] = *t->shape;
    x->part[1] = *t->shape->next;
    x->part[0].next = &x->part[1];                                     /* copy[0x23] = copy + 0xc4 */
    x->part[1].next = NULL;
    x->g.shape = &x->part[0];                                          /* copy[2] = copy + 0x88 */
    GATE_OPEN(o) = 0;
    GATE_CLOSED(o) = 0;
    o->gfx = &x->g;
    GATE_TARGET(o) = GATE_FULL;
    snd(SND_GATE_MOVE, o);
    return 1;
}

static void gate_update(const ObjClass *c, Obj *o)                    /* GateUpdate 0x423d50 */
{
    (void)c;
    if (!(*o->cell & 0xe000000u)) { obj_mark_delete(o); return; }      /* destroyed meanwhile */
    GateExt *x = &gate_ext[o->id & 0x1ff];
    int32_t old = GATE_OPEN(o);
    bool closing = GATE_TARGET(o) < old;
    GATE_OPEN(o) = approach(old, GATE_TARGET(o), g_dt << 15);         /* 0.5 px/tick */
    if (GATE_OPEN(o) == 0) {
        if (GATE_CLOSED(o) == 0) snd(SND_GATE_CLOSE, o);
        GATE_CLOSED(o) += g_dt;
        if (GATE_CLOSED(o) > 0x3c) { obj_mark_delete(o); return; }     /* shut for a second: back to the BNO */
    } else GATE_CLOSED(o) = 0;
    for (;;) {
        if (!x->vertical) { x->part[0].dx = -0x100000 - GATE_OPEN(o); x->part[1].dx = GATE_OPEN(o) + 0x100000; }
        else { x->part[0].dy = -0x100000 - GATE_OPEN(o); x->part[1].dy = GATE_OPEN(o) + 0x100000; }
        if (GATE_OPEN(o) == GATE_FULL) {                               /* close once the opener has left the zone */
            Obj *p = o->parent;
            if (!p || !p->gfx || !p->gfx->shape ||
                !shapes_overlap(o->pos, 0, gate_zone_part, p->pos, p->heading, p->gfx->shape)) {
                GATE_TARGET(o) = 0;
                snd(SND_GATE_MOVE, o);
            }
        }
        if (!closing || !obj_check_collision(o)) break;
        closing = false;                                               /* something in the way: open again */
        GATE_TARGET(o) = GATE_FULL;
        GATE_OPEN(o) = old;
    }
    if (!(*o->cell & 0xe000000u)) obj_destroy_now(o);
}

static int gate_destroy(const ObjClass *c, Obj *o)                    /* GateDestroy 0x423ec0 */
{
    (void)c;
    uint32_t *cell = o->cell;
    *cell = (*cell & ~0x3f80u) | ((uint32_t)GATE_BNO(o) << 7);         /* the BNO comes back */
    if (*cell & 0xe000000u) return 1;
    *cell = (*cell & 0xf3ffffffu) | 0x2000000u;                        /* destroyed while open: hp 1, then kill it */
    bno_damage(0xff0000, o, cell, &bno_defs[GATE_BNO(o)]);
    return 1;
}

static uint32_t gate_hit_static(Obj *o, uint32_t *cell, int bno) { (void)o; (void)cell; (void)bno; return 0; }   /* 0x423fa0 */

static int gate_damage(Obj *o, Obj *src, int32_t amount)              /* 0x423f50: hits count on the cell */
{
    if (cell_hp_damage(amount, src, o->cell, &bno_defs[GATE_BNO(o)])) { obj_mark_delete(o); return 1; }
    return 0;
}

const ObjClass class_gate = {
    13, OBJ_CLASS_NAME, gate_init, gate_destroy, gate_update, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, 0x32, gate_hit_static, NULL, gate_damage, NULL, 0
};

Obj *spawn_gate(uint32_t *cell, Obj *veh)                             /* SpawnGate 0x423fb0 */
{
    if (!(*cell & 0xe000000u)) return NULL;
    int32_t p[3];
    cell_to_pos(cell, p);                                              /* CellToPixelPos << 16 */
    int b = CELL_BNO(*cell);
    const Gfx *t = b == 0x2b ? &rgfx_4428a8 : b == 0x2c ? &rgfx_442d70 : NULL;
    if (!t) return NULL;
    Obj *g = obj_create(&class_gate, CELL_TEAM(*cell), p[0], p[1], 0, (void *)t);
    if (!g) return NULL;
    obj_set_parent(g, veh);
    GATE_BNO(g) = (uint8_t)b;
    *cell &= 0xffffc07fu;
    return g;
}
