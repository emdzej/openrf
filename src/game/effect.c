/* Explosions, debris, remains and shadows. Ported from ExplInit/Update/Destroy 0x4204c0..0x420640 with the
   byte-code ops 0x41fc50..0x4203b0 (table 0x44a9c0), FUN_00420410 / SpawnExplosion 0x4209a0 / 0x4209d0,
   DebrisInit/Update 0x4344b0..0x434550, Obj_SetCel 0x4338c0, the debris steps 0x433bd0..0x4343a0
   (table 0x454ee8), SpawnDebris 0x434670, SpawnDebrisBurst 0x4347a0, Stay 0x435da0..0x435eb0 and
   Shadow 0x434cf0 / 0x434d70. See docs/game.md §6. */
#include "effect.h"
#include "../exe.h"
#include "vehicle.h"
#include "weapon.h"
#include <stddef.h>
#include <string.h>

extern Gfx gfx_44aa20;
extern const Gfx *const gfx_table[];
extern const int gfx_table_count;

ExplExt expl_ext[OBJ_POOL];
DebrisExt debris_ext[OBJ_POOL];
uint8_t obj_draw_flags[OBJ_POOL];      /* bit0: destroy (seen again after idling), bit1: LRU touch */
#define draw_flags obj_draw_flags

static void snd(int cmd, uint32_t ev, Obj *o) { if (game_hooks.sound) game_hooks.sound(cmd, ev, o); }
static inline int slot_of(const Obj *o) { return (int)(o->id & 0x1ff); }

/* ------------------------------------------------------------------ data image */
/* The explosion VM (ExplUpdate 0x420500) interprets descriptors, byte-code scripts, label tables and sprite
   geometry by VA, straight from the RFIRE.BIN image (exe.c). Only these .data ranges are visible; anything
   else reads as 0 (as the original port's raw copies of the two ranges did). */
static const uint32_t rf_ranges[2][2] = { { 0x44a600, 0x44aa00 }, { 0x452d00, 0x454800 } };
static bool rf_in(uint32_t va)
{
    for (int i = 0; i < 2; i++) if (va >= rf_ranges[i][0] && va < rf_ranges[i][1]) return true;
    return false;
}

static uint32_t rf_word(uint32_t va) { return rf_in(va) ? exe_u32(va) : 0; }     /* aligned */

int8_t rf_s8(uint32_t va) { return (int8_t)(rf_word(va & ~3u) >> ((va & 3) * 8)); }

uint32_t rf_u32(uint32_t va)
{
    if (!(va & 3)) return rf_word(va);
    return (uint8_t)rf_s8(va) | (uint32_t)(uint8_t)rf_s8(va + 1) << 8 | (uint32_t)(uint8_t)rf_s8(va + 2) << 16 |
           (uint32_t)(uint8_t)rf_s8(va + 3) << 24;
}

const int32_t *rf_ptr(uint32_t va)
{
    return rf_in(va) && !(va & 3) ? (const int32_t *)exe_ptr(va) : NULL;
}

const Gfx *gfx_by_addr(uint32_t addr)
{
    int lo = 0, hi = gfx_table_count - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (gfx_table[m]->addr == addr) return gfx_table[m];
        if (gfx_table[m]->addr < addr) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

const ModelPart *model_part_by_addr(uint32_t addr)
{
    int lo = 0, hi = MODEL_PART_COUNT - 1;
    while (lo <= hi) {                     /* models_data.c is sorted by address */
        int m = (lo + hi) / 2;
        if (model_parts[m].addr == addr) return &model_parts[m];
        if (model_parts[m].addr < addr) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

/* ------------------------------------------------------------------ Expl */
typedef uint32_t (*ExplOp)(Obj *o, uint32_t p, int skip);
static uint32_t run_op(Obj *o, uint32_t p, int skip);

static uint32_t *nb_cell(Obj *o, uint32_t p)        /* ops 5/18/19: cell + dx, row by the sign of dy */
{
    uint32_t *c = o->cell70 + rf_s8(p);
    int8_t dy = rf_s8(p + 1);
    if (dy < 0) c -= 128; else if (dy > 0) c += 128;
    if (c < G.cell || c >= G.cell + 128 * 128) return NULL;
    return c;
}

static uint32_t op_nop_back(Obj *o, uint32_t p, int s) { (void)o; (void)s; return p - 1; }   /* 0x41fc50 (op 0) */
static uint32_t op_yield(Obj *o, uint32_t p, int s) { (void)o; (void)s; return p; }         /* 0x41fc60 (op 1) */
static uint32_t op_wait(Obj *o, uint32_t p, int s) { (void)o; (void)s; return p + 1; }      /* 0x41fc70 (op 2) */
static uint32_t op_sound(Obj *o, uint32_t p, int s)                                          /* 0x41fc80 */
{
    if (!s) { int k = rf_s8(p); snd(1, k >= 0 && k < 40 ? snd_expl_tab[k] : 0, o); }
    return p + 1;
}
static uint32_t op_apply_dest(Obj *o, uint32_t p, int s)                                     /* 0x41fcc0 */
{
    if (!s && o->cell70) {
        uint32_t w = *o->cell70;
        bno_set_destroyed(o->cell70, &bno_defs[CELL_BNO(w)], CELL_TEAM(w));
    }
    return p;
}
static uint32_t op_set_nb(Obj *o, uint32_t p, int s)                                         /* 0x41fd10 */
{
    if (s || !o->cell70) return p + 4;
    uint32_t *c = nb_cell(o, p);
    if (c && (int)CELL_BNO(*c) == rf_s8(p + 2)) set_cell_static(rf_s8(p + 3), c, 0, CELL_TEAM(*c));
    return p + 4;
}
static uint32_t op_part(Obj *o, uint32_t p, int s)                                           /* 0x41fd90 */
{
    if (s) return p + 1;
    uint32_t va = 0x452dc8 + (uint32_t)(rf_s8(p) * 0x44);
    const Gfx *g = gfx_by_addr(va);
    if (o->u58) {                                  /* copy of the part sharing the damage box */
        ExplExt *x = &expl_ext[slot_of(o)];
        x->part_gfx = (Gfx){ va, &x->box, g ? g->radius : 0x100000, GADJ_NONE };
        o->gfx = &x->part_gfx;
        return p + 1;
    }
    o->gfx = g;
    return p + 1;
}
static uint32_t op_defgfx(Obj *o, uint32_t p, int s)                                         /* 0x41fe00 */
{
    if (!s) o->gfx = o->u58 ? &expl_ext[slot_of(o)].box_gfx : &gfx_44aa20;
    return p;
}
static uint32_t op_frame(Obj *o, uint32_t p, int s)                                          /* 0x41fe30 */
{
    if (!s) o->u64 = rf_s8(p) * 0x10000 + (int32_t)((uint32_t)o->u64 & 0xffff);
    return p + 1;
}
static uint32_t op_rskip(Obj *o, uint32_t p, int s)                                          /* 0x41fe60 */
{
    if (s) return p + 3;
    int n = rf_s8(p), thr = rf_s8(p + 1);
    int r = rand_range(rf_s8(p + 2));
    p += 3;
    if (r < thr)
        while (n >= 1) {
            n--;
            if (rf_s8(p) == 0) break;
            p = run_op(o, p, 1);
        }
    return p;
}
static uint32_t op_goto_even(Obj *o, uint32_t p, int s)                                      /* 0x41fee0 */
{
    if (s || (o->id & 1)) return p + 1;
    return rf_u32(expl_field(o->e60, ED_LABELS) + 4 * (uint32_t)rf_s8(p));
}
static uint32_t op_delete(Obj *o, uint32_t p, int s) { if (!s) obj_mark_delete(o); return p; }   /* 0x41ff10 */
static uint32_t op_gfx_label(Obj *o, uint32_t p, int s)                                      /* 0x41ff30 */
{
    if (!s && !o->u58) o->gfx = gfx_by_addr(rf_u32(expl_field(o->e60, ED_LABELS) + 4 * (uint32_t)rf_s8(p)));
    return p + 1;
}
static uint32_t op_box(Obj *o, uint32_t p, int s)                                            /* 0x41ff60 */
{
    if (s) return p + 5;                           /* (the skip path steps over 5 of the 6 argument bytes) */
    ExplExt *x = &expl_ext[slot_of(o)];
    if (!o->u58) { memset(x, 0, sizeof *x); x->used = true; o->u58 = 1; }
    x->box_gfx = (Gfx){ 0x44aa20, &x->box, gfx_44aa20.radius, GADJ_NONE };
    memset(&x->box, 0, sizeof x->box);
    x->box.type = 2;
    x->box.zlo = rf_s8(p) << 16;
    x->box.zhi = rf_s8(p + 1) << 16;
    x->box.bbox[0] = rf_s8(p + 2) * -0x8000;
    x->box.bbox[1] = rf_s8(p + 3) * -0x8000;
    x->box.bbox[2] = -x->box.bbox[0];
    x->box.bbox[3] = -x->box.bbox[1];
    x->box.solid = 0x20;
    x->box.mask = (uint8_t)rf_s8(p + 4);
    x->box.addr = 0x44aa4c;
    x->damage = rf_s8(p + 5) << 16;
    o->gfx = &x->box_gfx;
    return p + 6;
}
static uint32_t op_boxsize(Obj *o, uint32_t p, int s)                                        /* 0x420030 */
{
    if (!s && o->u58) {
        ShapePart *b = &expl_ext[slot_of(o)].box;
        int32_t a = rf_s8(p) * 0x10000;
        b->zlo = b->bbox[0] = b->bbox[1] = -a;
        b->zhi = b->bbox[2] = b->bbox[3] = a;
    }
    return p + 1;
}
static uint32_t op_nop2(Obj *o, uint32_t p, int s) { (void)o; (void)s; return p + 2; }      /* 0x420070 */
static uint32_t op_detach(Obj *o, uint32_t p, int s)                                         /* 0x420080 */
{
    if (!s && o->parent) obj_set_parent(o, NULL);
    return p;
}
static uint32_t op_debris(Obj *o, uint32_t p, int s)                                         /* 0x4200b0 */
{
    if (s) return p + 1;
    int k = rf_s8(p);
    if (!o->cell70) return p + 1;
    uint32_t bno = CELL_BNO(*o->cell70), part;
    if (k < 0) {
        uint32_t l = expl_field(o->e60, ED_LABELS) + (uint32_t)(-k) * 4;
        part = rf_u32(l - 4);
        k = (int32_t)rf_u32(l);
    } else {
        if (bno == 0) return p + 1;
        part = bno_defs[bno].gfx ? bno_defs[bno].gfx->addr : 0;
    }
    int32_t pos[3];
    cell_to_pos(o->cell70, pos);
    const Gfx *g = gfx_by_addr(part);
    if (g && g->adjust == GADJ_JITTER) {
        const int8_t *j = G.jitter[(((uint32_t)pos[1] >> 21) & 15) * 16 + (((uint32_t)pos[0] >> 21) & 15)];
        pos[0] += j[0] * 0x10000;
        pos[1] += j[1] * 0x10000;
    }
    if (k < 0 || k > 9) return p + 1;
    for (const ModelPart *m = model_part_by_addr(part); m; m = m->next)
        spawn_debris_burst(pos, o->team, debris_sets[k], m, 0, 0, NULL);
    return p + 1;
}
static uint32_t op_dmg_nb_bno(Obj *o, uint32_t p, int s)                                     /* 0x420170 */
{
    if (s || !o->cell70) return p + 3;
    uint32_t *c = nb_cell(o, p);
    if (c && (int)CELL_BNO(*c) == rf_s8(p + 2)) bno_damage(0xff0000, o, c, &bno_defs[CELL_BNO(*c)]);
    return p + 3;
}
static uint32_t op_dmg_nb_ter(Obj *o, uint32_t p, int s)                                     /* 0x4201f0 */
{
    if (s || !o->cell70) return p + 3;
    uint32_t *c = nb_cell(o, p);
    if (c && (int)CELL_TERRAIN(*c) == rf_s8(p + 2)) bno_damage(0xff0000, o, c, &bno_defs[CELL_BNO(*c)]);
    return p + 3;
}
static uint32_t op_rchoice(Obj *o, uint32_t p, int s)                                        /* 0x420270 */
{
    int n = rf_s8(p), done = 0;
    p++;
    if (!s) {
        int r = rand_range(n);
        for (int k = r; k > 0; k--) p = run_op(o, p, 1);
        if (r > 0) done = r;
        p = run_op(o, p, 0);
    }
    for (int k = n - done; k > 0; k--) p = run_op(o, p, 1);   /* (one op more than the n alternatives) */
    return p;
}
static int restore_bno(intptr_t a, intptr_t b, int32_t period)                               /* 0x420370 */
{
    (void)period;
    bno_set_destroyed((uint32_t *)b, &bno_defs[a & 0x7fff], (int)(a >> 16));
    return -1;
}
static uint32_t op_hide_restore(Obj *o, uint32_t p, int s)                                   /* 0x420310 */
{
    if (s || !o->cell70) return p + 1;
    uint32_t *c = o->cell70;
    timer_add(rf_s8(p), restore_bno, (intptr_t)(CELL_BNO(*c) + (CELL_TEAM(*c) << 16)), (intptr_t)c);
    *c &= 0xffffc07fu;
    return p + 1;
}
static uint32_t op_team_nb(Obj *o, uint32_t p, int s)                                        /* 0x4203b0 */
{
    if (s || !o->cell70) return p + 2;
    uint32_t w = *o->cell70;
    if ((int)CELL_BNO(w) == rf_s8(p)) *o->cell70 = (w & ~0xc000u) | (((uint32_t)rf_s8(p + 1) << 14) & 0xc000u);
    return p + 2;
}

static const ExplOp expl_ops[23] = {                 /* PTR_FUN_0044a9c0 */
    op_nop_back, op_yield, op_wait, op_sound, op_apply_dest, op_set_nb, op_part, op_defgfx, op_frame,
    op_rskip, op_goto_even, op_delete, op_gfx_label, op_box, op_boxsize, op_nop2, op_detach, op_debris,
    op_dmg_nb_bno, op_dmg_nb_ter, op_rchoice, op_hide_restore, op_team_nb,
};

static uint32_t run_op(Obj *o, uint32_t p, int skip)
{
    int op = rf_s8(p);
    if (op < 0 || op > 22) return p;               /* (the table has no entry: never in the data) */
    return expl_ops[op](o, p + 1, skip);
}

static int expl_init(const ObjClass *c, Obj *o, void *arg)       /* ExplInit 0x4204c0 */
{
    uint32_t d = (uint32_t)(uintptr_t)arg;
    (void)c;
    o->heading = 0;
    o->u58 = 0;
    o->speed = 0;
    o->b57 = 0;
    o->att[0] = o->att[1] = o->att[2] = 0;
    o->_50 = (int32_t)expl_field(d, ED_ZBIAS);
    o->e60 = d;
    o->u64 = 0;
    o->v5c = expl_field(d, ED_SCRIPT);
    o->u6c = (int32_t)expl_field(d, ED_SCALE);
    o->u68 = (int32_t)expl_field(d, ED_RATE);
    o->cell70 = NULL;
    o->think = NULL;
    return 1;
}

static int expl_destroy(const ObjClass *c, Obj *o)               /* ExplDestroy 0x4205c0 */
{
    (void)c;
    if (o->u58) { expl_ext[slot_of(o)].used = false; o->u58 = 0; }
    return 1;
}

/* FUN_00420410: keep an attached explosion at parent + offset (tank flashes follow the turret). */
static void expl_follow(Obj *o, Obj *parent)
{
    int32_t d[3] = { o->att[0] << 16, o->att[1] << 16, o->att[2] << 16 };
    o->heading = parent->heading;
    if (parent->cls->id == 1 && veh_state(parent)->def->type == VT_TANK)
        o->heading = ((uint32_t)veh_state(parent)->turret + parent->heading) & ANG_MASK;
    vec_mul_mat3(d, d, rot_mat[(int32_t)o->heading >> 16]);
    d[0] += parent->pos[0] - o->pos[0];
    d[1] += parent->pos[1] - o->pos[1];
    d[2] += parent->pos[2] - o->pos[2];
    obj_move(o, d);
}

static void expl_update(const ObjClass *c, Obj *o)               /* ExplUpdate 0x420500 */
{
    (void)c;
    int32_t frames = (int32_t)expl_field(o->e60, ED_FRAMES);
    if (frames <= o->u64) { obj_destroy_now(o); return; }
    if (!(o->flags & OF_ALIVE)) {
        o->u64 += o->u68 * g_dt;
        if (frames <= o->u64) { obj_destroy_now(o); return; }
    } else {
        o->u64 = 0x10000;
        o->flags &= ~OF_ALIVE;
    }
    uint32_t p = o->v5c;
    if (rf_s8(p) != 0) {
        for (;;) {
            int op = rf_s8(p);
            if (op == 0) break;
            if (op == 1) { p++; break; }
            if (op == 2) {
                if (o->u64 < rf_s8(p + 1) * 0x10000) break;
                p += 2;
            } else p = run_op(o, p, 0);
        }
        o->v5c = p;
    }
    if (o->parent) expl_follow(o, o->parent);
    if (o->u58) obj_check_collision(o);
}

static int32_t box_damage(Obj *o)
{
    int32_t d = expl_ext[slot_of(o)].damage;
    if (d < 0) d = -(d * g_dt);
    return d;
}

static uint32_t expl_hit_static(Obj *o, uint32_t *cell, int bno)  /* 0x4205f0 */
{
    if (!o->u58) return 0;
    bno_damage(box_damage(o), o, cell, &bno_defs[bno]);
    return 0;
}

static uint32_t expl_hit_object(Obj *o, Obj *other)              /* 0x420640 */
{
    if (!o->u58) return 0;
    if (other->cls->damage) other->cls->damage(other, o, box_damage(o));
    return 0;
}

const ObjClass class_expl = {
    11, OBJ_CLASS_NAME, expl_init, expl_destroy, expl_update, &gfx_44aa20, NULL, NULL,
    NULL, NULL, NULL, NULL, 0xc8, expl_hit_static, expl_hit_object, NULL, NULL, 0
};

Obj *spawn_explosion(int team, int32_t x, int32_t y, int32_t z, uint32_t desc, uint32_t *cell)   /* 0x4209a0 */
{
    if (!desc) return NULL;
    Obj *e = obj_create(&class_expl, team, x, y, z, (void *)(uintptr_t)desc);
    if (e) e->cell70 = cell;
    if (game_hooks.explosion) { int32_t p[3] = { x, y, z }; game_hooks.explosion(team, p, desc); }
    return e;
}

Obj *spawn_explosion_attached(Obj *parent, int team, const int32_t *off, uint32_t desc)          /* 0x4209d0 */
{
    Obj *e = obj_create(&class_expl, team, 0, 0, 0, (void *)(uintptr_t)desc);
    if (!e) return NULL;
    obj_set_parent(e, parent);
    e->att[0] = (int8_t)(off[0] >> 16);
    e->att[1] = (int8_t)(off[1] >> 16);
    e->att[2] = (int8_t)(off[2] >> 16);
    expl_follow(e, parent);
    return e;
}

void expl_set_pitch(Obj *e, int32_t pitch) { e->b57 = (int8_t)(pitch >> 16); }                    /* 0x420a40 */

/* ------------------------------------------------------------------ FWall (debris) */
static int32_t dmv[3];          /* DAT_004597a0..a8: this frame's move */
static int dmoved;              /* DAT_0045af88 */

typedef int (*DebrisFn)(Obj *o, const DebrisStep *st, int32_t *slot, int f);

static int ds_end(Obj *o, const DebrisStep *st, int32_t *slot, int f)          /* 0x433bd0 */
{
    (void)slot; (void)f;
    if (st->from <= o->u64) { obj_mark_delete(o); return 0; }
    return 1;
}
static int ds_colfade(Obj *o, const DebrisStep *st, int32_t *slot, int f)      /* 0x433c00 */
{
    /* Fades the private PLUT copy (3DO RGB555) towards a colour; the PC renderer ignores that PLUT for
       these CCBs, so nothing to do. */
    (void)o; (void)st; (void)slot; (void)f;
    return 1;
}
static int ds_speed(Obj *o, const DebrisStep *st, int32_t *slot, int f)        /* 0x433d80 */
{
    if (f < st->from || f > st->to) return 1;
    DebrisExt *x = &debris_ext[slot_of(o)];
    if (*slot == 0x7fffffff) {
        int32_t t = st->p14;
        if (st->p1c) t = rand_range((t - st->p1c) >> 4) * 0x10 + st->p1c;
        *slot = t;
        if (st->p10 != -1) o->speed = st->p10;
    }
    o->speed = approach(o->speed, *slot, st->p18 * g_dt);
    int32_t v = o->speed * g_dt;
    dmv[0] += fix_mul(x->dir[0], v);
    dmv[1] += fix_mul(x->dir[1], v);
    if (!(o->flags & 0x1000000)) dmv[2] += fix_mul(x->dir[2], v);
    dmoved = 1;
    return 1;
}
static void spin_target(const DebrisStep *st, int32_t *slot, int32_t *rate)
{
    if (st->p10 != -1) *rate = st->p10;
    int32_t t = st->p18;
    if (t != st->p1c) {
        int32_t k = ((st->p1c - t) + 0x20000) >> 9;
        int32_t a = rand_range(k), b = rand_range(k);
        *slot = (a + b) * 0x100 + t;
        if ((rand_range(4) & 1) == 0) return;
        t = -*slot;
    }
    *slot = t;
}
static int ds_spin(Obj *o, const DebrisStep *st, int32_t *slot, int f)         /* 0x433e70: yaw */
{
    if (f < st->from || f > st->to) return 1;
    if (*slot == 0x7fffffff) spin_target(st, slot, &o->u6c);
    o->u6c = approach(o->u6c, *slot, st->p14 * g_dt);
    o->heading = (uint32_t)(o->u6c * g_dt + (int32_t)o->heading) & ANG_MASK;
    return 1;
}
static int ds_pitchspin(Obj *o, const DebrisStep *st, int32_t *slot, int f)    /* 0x433f50: pitch (+0x68) */
{
    if (f < st->from || f > st->to) return 1;
    if (*slot == 0x7fffffff) spin_target(st, slot, &o->u70);
    o->u70 = approach(o->u70, *slot, st->p14 * g_dt);
    o->u68 = (int32_t)((uint32_t)(o->u70 * g_dt + o->u68) & ANG_MASK);
    return 1;
}
static int ds_anim(Obj *o, const DebrisStep *st, int32_t *slot, int f)         /* 0x434030 */
{
    (void)slot;
    if (f < st->from) return 1;
    DebrisExt *x = &debris_ext[slot_of(o)];
    int k = f - st->from;
    if (k < st->to) {
        k += st->p10;
        x->sprite = k;                    /* +0x40: the ART.CAR template, or the private copy (+0x48 = k) */
        if (st->p14) x->sprite_own = k;
        return 1;
    }
    x->sprite = x->sprite_own;            /* back to the private CCB */
    return 1;
}
static int ds_gravity(Obj *o, const DebrisStep *st, int32_t *slot, int f)      /* 0x434160 */
{
    if (f < st->from || f > st->to) return 1;
    if (slot && *slot == 0x7fffffff) {
        *slot = 0;
        if (st->p10 & 1) o->u58 = fix_mul(debris_ext[slot_of(o)].dir[2], o->speed);
        o->flags |= 0x1000000;
    }
    int32_t g = st->p14 ? st->p14 : tun_gravity, mn = st->p18 ? st->p18 : tun_fall_max;
    o->u58 -= g * g_dt;
    if (o->u58 < mn) o->u58 = mn;
    dmoved = 1;
    dmv[2] += o->u58 * g_dt;
    return 1;
}
static int ds_floor(Obj *o, const DebrisStep *st, int32_t *slot, int f)        /* 0x434210 */
{
    (void)slot; (void)f;
    if (st->p10 <= o->pos[2]) {
        dmv[2] = -o->pos[2];
        o->_50 = -0x1900000;
        obj_set_inactive(o);
        return 0;
    }
    return 1;
}
static int ds_damp(Obj *o, const DebrisStep *st, int32_t *slot, int f)         /* 0x434250 */
{
    (void)slot;
    if (f < st->from || f > st->to) return 1;
    DebrisExt *x = &debris_ext[slot_of(o)];
    if (st->p10) x->vel[0] = approach(x->vel[0], 0, g_dt * st->p10);
    if (st->p14) x->vel[1] = approach(x->vel[1], 0, g_dt * st->p14);
    if (st->p18) x->vel[2] = approach(x->vel[2], 0, g_dt * st->p18);
    return 1;
}
static int ds_grav_floor(Obj *o, const DebrisStep *st, int32_t *slot, int f)   /* 0x4342f0: squash on the ground */
{
    if (f < st->from || f > st->to) return 1;
    ds_gravity(o, st, slot, f);
    int32_t z = o->pos[2] + dmv[2], above = z, below = 0;
    if (z < 1) { above = 0; below = z; }
    int n = 0;
    DebrisExt *x = &debris_ext[slot_of(o)];
    for (int i = 0; i < 4; i++) {
        x->verts[i][2] += below;
        if (x->verts[i][2] + above < 0) { n++; x->verts[i][2] = -above; }
    }
    if (n > 3 && (st->p10 & 2)) { obj_mark_delete(o); return 0; }
    return 1;
}
static int ds_splash(Obj *o, const DebrisStep *st, int32_t *slot, int f)       /* 0x4343a0 */
{
    if (f < st->from || f > st->to) return 1;
    if (slot && *slot != 0x7fffffff) return 1;
    if (o->pos[2] > 0) return 1;
    if (slot) *slot = 0;
    int w = water_state_at(o->pos);
    if (st->p10 == 0 && w == 0) return 1;
    uint32_t d;
    if (w == 0) {
        uint32_t t = CELL_TERRAIN(*o->cell);
        d = (t < 0x49 || t > 0x53) ? 0x453b58 : 0x453d80;
    } else {
        DebrisExt *x = &debris_ext[slot_of(o)];
        int32_t sz = dist3d(x->verts[0], x->verts[2]);
        d = sz < 0x180000 ? 0x4539d0 : (sz < 0x280001 ? 0x453a00 : 0x453a30);
    }
    spawn_explosion(o->team, o->pos[0], o->pos[1], 0, d, NULL);
    obj_mark_delete(o);
    return 0;
}

static const DebrisFn debris_fns[11] = {            /* PTR_FUN_00454ee8 */
    ds_end, ds_colfade, ds_speed, ds_spin, ds_pitchspin, ds_anim, ds_gravity, ds_floor, ds_damp,
    ds_grav_floor, ds_splash,
};

/* The step loop shared by DebrisUpdate / SpawnDebris / SpawnDebrisBurst. */
static void debris_steps(Obj *o, const DebrisDef *d)
{
    if (!d->steps) return;
    DebrisExt *x = &debris_ext[slot_of(o)];
    dmoved = 0;
    dmv[0] = x->vel[0]; dmv[1] = x->vel[1]; dmv[2] = x->vel[2];
    int f = o->u64 >> 16;
    for (int i = 0; i < d->nsteps; i++) {
        const DebrisStep *st = &d->steps[i];
        int32_t *slot = i < 10 ? &x->slot[i] : NULL;
        if (st->op < 0 || st->op > 10 || !debris_fns[st->op](o, st, slot, f)) break;
    }
    if (dmoved) {
        if (dmv[2] + o->pos[2] < 0) dmv[2] = -o->pos[2];
        obj_move(o, dmv);
    }
}

/* FUN_00433820 with the reciprocal table: v / |v| (integer length) */
static void debris_dir(int32_t *out, const int32_t *v)
{
    static const int32_t zero[3] = { 0, 0, 0 };
    out[0] = out[1] = out[2] = 0;
    int32_t b = dist3d(zero, v);
    if (!b) return;
    int k = b >> 16;
    int32_t r = k >= 0 && k < 256 ? recip_tab[k] : 0;
    out[0] = (v[0] >> 16) * r;
    out[1] = (v[1] >> 16) * r;
    out[2] = (v[2] >> 16) * r;
}

/* Obj_SetCel 0x4338c0: the piece's CCB and its 4 corners (centred on their mean). */
static void debris_set_cel(DebrisExt *x, int sprite, const int32_t (*verts)[3], const int32_t *idx)
{
    x->sprite = x->sprite_own = sprite;
    x->centre[0] = x->centre[1] = x->centre[2] = 0;
    for (int i = 0; i < 4; i++) {
        const int32_t *v = verts[idx[i]];
        x->verts[i][0] = v[0]; x->verts[i][1] = v[1]; x->verts[i][2] = v[2];
        x->centre[0] += v[0]; x->centre[1] += v[1]; x->centre[2] += v[2];
    }
    x->centre[0] >>= 2; x->centre[1] >>= 2; x->centre[2] >>= 2;
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 3; k++) x->verts[i][k] -= x->centre[k];
    debris_dir(x->dir, x->centre);
}

static int fwall_init(const ObjClass *c, Obj *o, void *arg)      /* DebrisInit 0x4344b0 */
{
    const DebrisDef *d = arg;
    DebrisExt *x = &debris_ext[slot_of(o)];
    (void)c;
    o->heading = 0; o->speed = 0; o->u58 = 0;
    o->p5c = d;
    o->u64 = 0; o->u68 = 0; o->u6c = 0; o->u70 = 0;
    memset(x, 0, sizeof *x);
    x->rate = d->rate;
    for (int i = 0; i < 10; i++) x->slot[i] = 0x7fffffff;
    draw_flags[slot_of(o)] = 0;
    return 1;
}

static void fwall_update(const ObjClass *c, Obj *o)              /* DebrisUpdate 0x434550 */
{
    (void)c;
    const DebrisDef *d = o->p5c;
    if (!(o->flags & OF_ALIVE)) {
        o->u64 += debris_ext[slot_of(o)].rate * g_dt;
        if (d->frames * 0x10000 <= o->u64) { obj_destroy_now(o); return; }
    } else {
        o->u64 = 0x10000;
        o->flags &= ~OF_ALIVE;
    }
    debris_steps(o, d);
}

static int fwall_destroy(const ObjClass *c, Obj *o) { (void)c; (void)o; return 1; }   /* DebrisDestroy 0x434520 */

extern Gfx gfx_454f18;
const ObjClass class_fwall = {
    3, OBJ_CLASS_NAME, fwall_init, fwall_destroy, fwall_update, &gfx_454f18, NULL, NULL,
    NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0
};

int debris_frame(const Obj *o) { return o->u64 >> 16; }

Obj *spawn_debris(int team, const int32_t *pos, const DebrisDef *d, int sprite, const int32_t (*verts)[3],
                  const int32_t *idx)                                             /* SpawnDebris 0x434670 */
{
    if (!d) return NULL;
    Obj *o = obj_create(&class_fwall, team, pos[0], pos[1], pos[2], (void *)d);
    if (!o) return NULL;
    DebrisExt *x = &debris_ext[slot_of(o)];
    debris_set_cel(x, sprite, verts, idx);
    obj_move(o, x->centre);
    debris_steps(o, d);
    return o;
}

int spawn_debris_burst(const int32_t *pos, int team, const DebrisDef *const *set, const ModelPart *m,
                       uint32_t heading, int32_t pitch, DebrisCb cb)             /* SpawnDebrisBurst 0x4347a0 */
{
    int n = 0;
    if (!m || !m->verts || m->nverts > 64) return 0;
    int32_t p[3] = { m->off[0] + pos[0], pos[1] + m->off[1], pos[2] + m->off[2] };
    int32_t tv[64][3];
    const int32_t (*verts)[3] = m->verts;
    if (heading || pitch) {
        int32_t M[9];
        mat_mul3(M, pitch_mat[(pitch >> 16) & 63], rot_mat[((int32_t)heading >> 16) & 63]);
        mat_transform_verts(tv, m->verts, M, m->nverts);
        verts = (const int32_t (*)[3])tv;
    }
    const int8_t *order = NULL;
    int count = m->nfaces_field;
    if (count == 0) {
        order = m->orders[((heading & 0xfff9ffffu) >> 19) & 7];
        if (!order || order[0] < 0) return 0;
    }
    for (int k = 0;; k++) {
        const ModelFace *f;
        if (order) { if (order[k] < 0) break; f = &m->faces[order[k]]; }
        else { if (k >= count || k >= m->nfaces) break; f = &m->faces[k]; }
        int cls = (f->flags & 0x300) >> 8;
        if (!cls) continue;
        const DebrisDef *d = set[cls - 1];
        Obj *o = NULL;
        if (d && (o = obj_create(&class_fwall, team, p[0], p[1], p[2], (void *)d)) != NULL) {
            DebrisExt *x = &debris_ext[slot_of(o)];
            int32_t idx[4] = { f->c[0], f->c[1], f->c[2], f->c[3] };
            debris_set_cel(x, f->sprite + ((f->flags & 8) ? team : 0), verts, idx);
            obj_move(o, x->centre);
            debris_steps(o, d);
        }
        if (cb && o) cb(o, n++);
    }
    return n;
}

/* ------------------------------------------------------------------ Stay */
static int stay_init(const ObjClass *c, Obj *o, void *a) { (void)c; (void)o; (void)a; return 1; }   /* 0x435da0 */
static int stay_destroy(const ObjClass *c, Obj *o) { (void)c; (void)o; return 1; }                  /* 0x435db0 */
static void stay_update(const ObjClass *c, Obj *o) { (void)c; (void)o; }                            /* 0x435dc0 */

extern Gfx gfx_4557f0;
const ObjClass class_stay = {
    17, OBJ_CLASS_NAME, stay_init, stay_destroy, stay_update, &gfx_4557f0, NULL, NULL,
    NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, 2
};

Obj *spawn_stay(int team, const int32_t *pos, const Gfx *g, int32_t life, int32_t linger)   /* SpawnStay 0x435eb0 */
{
    Obj *o = obj_create(&class_stay, team, pos[0], pos[1], pos[2], NULL);
    if (!o) return NULL;
    o->heading = 0;
    o->v5c = 0;
    o->p60 = (void *)g;
    o->u68 = g_tick;
    o->u64 = life ? g_tick + life : 0;
    o->u6c = linger;
    draw_flags[slot_of(o)] = 0;
    return o;
}

/* The draw functions 0x433a60 (FWall) and 0x435e40 (Stay) destroy pieces that come back into view after
   idling, and Stay moves itself to the tail of the recycle list; the renderer reports that through
   obj_draw_flags and the frame loop applies it here. */
void effects_reap_drawn(void)
{
    for (int i = 1; i < OBJ_POOL; i++) {
        uint8_t f = draw_flags[i];
        if (!f) continue;
        draw_flags[i] = 0;
        Obj *o = &obj_pool[i];            /* (flagged by this frame's render: still live) */
        if (o->cls != &class_fwall && o->cls != &class_stay) continue;
        if (f & 1) obj_destroy_now(o);
        else if (f & 2) obj_touch_useless(o);
    }
}

/* ------------------------------------------------------------------ Shadow */
static const Gfx *shadow_gfx;    /* class+0x40 of the parent's class while it is being created */

static int shadow_init(const ObjClass *c, Obj *o, void *arg)     /* ShadowInit 0x434cf0 */
{
    Obj *p = arg;
    (void)c;
    if (!p || !shadow_gfx) return 0;
    o->gfx = shadow_gfx;
    obj_set_parent(o, p);
    obj_insert_after(p, o);
    if (p->flags & OF_INACTIVE_LIST) o->flags |= OF_INACTIVE_LIST;
    o->heading = p->heading;
    o->pos[0] = p->pos[0] + p->pos[2];
    o->pos[1] = p->pos[1] + p->pos[2];
    o->pos[2] = 0;
    o->flags |= OF_NOCOLLIDE;
    o->_50 = p->pos[2] - 0x10000;
    return 1;
}

static void shadow_update(const ObjClass *c, Obj *o)             /* ShadowUpdate 0x434d70 */
{
    (void)c;
    Obj *p = o->parent;
    if (!p) { obj_destroy_now(o); return; }
    o->heading = p->heading;
    o->_50 = p->pos[2] - 0x10000;
    int32_t d[3] = { ((p->pos[2] >> 8) * 0x55 - o->pos[0]) + p->pos[0],
                     (p->pos[1] - ((int32_t)((uint32_t)p->pos[2] & 0xffffff01u) >> 1)) - o->pos[1], 0 };
    obj_move(o, d);
}

static void shadow_detached(Obj *o, Obj *parent) { (void)parent; obj_mark_delete(o); }   /* +0x24 = ObjMarkForDelete */

const ObjClass class_shadow = {
    4, OBJ_CLASS_NAME, shadow_init, NULL, shadow_update, NULL, NULL, NULL,
    NULL, shadow_detached, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0
};

Obj *spawn_shadow(Obj *parent, const Gfx *g)
{
    shadow_gfx = g;
    Obj *s = obj_create(&class_shadow, parent->team, 0, 0, 0, parent);
    shadow_gfx = NULL;
    return s;
}
