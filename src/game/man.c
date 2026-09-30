/* Soldiers: class MAN (14, descriptor 0x43fb10): ManInit 0x4062c0, ManDestroy 0x406310, ManUpdate 0x406320,
   hit_static 0x4064b0, hit_object 0x4064d0 (crushed by vehicles), damage 0x406550, the think states
   0x406cd0 (leave the building), 0x406bc0 (pick the nearest vehicle), 0x4065b0 (stand), 0x4065f0 (run away /
   dodge), 0x406a90 (throw a grenade), 0x406b90 (wait); SpawnMan 0x4071c0, SpawnMenFromBuilding 0x4071f0.
   (0x406e40, "walk away from a cell", is only reachable through dead code in 0x406cd0.) */
#include "rules.h"
#include "vehicle.h"
#include "weapon.h"
#include "effect.h"
#include <string.h>

ManExt man_ext[OBJ_POOL];
#define EXT(o) (&man_ext[(o)->id & 0x1ff])
enum { SND_MAN_CRUSH = 0x43eb58, OF_MAN_PASS = 0x1000000 };

static void snd(uint32_t ev, Obj *o) { if (game_hooks.sound) game_hooks.sound(1, ev, o); }

static int think_pick(Obj *o);
static int think_idle(Obj *o);
static int think_flee(Obj *o);
static int think_throw(Obj *o);
static int think_wait(Obj *o);

static int man_init(const ObjClass *c, Obj *o, void *arg)             /* ManInit 0x4062c0 */
{
    (void)c; (void)arg;
    MAN_FRAME(o) = 0x60000;
    MAN_DRAWN(o) = g_tick;
    MAN_GRENADES(o) = man_grenades[rand_range(0x10)];
    MAN_ANIM(o) = 0;
    MAN_WATER(o) = 0;
    o->heading = (uint32_t)((rand_range(8) - 4) & 0x3f) << 16;       /* roughly north (+-22 deg) */
    memset(EXT(o), 0, sizeof(ManExt));
    return 1;
}

static int man_destroy(const ObjClass *c, Obj *o) { (void)c; (void)o; return 1; }   /* 0x406310 */

static void man_update(const ObjClass *c, Obj *o)                     /* ManUpdate 0x406320 */
{
    (void)c;
    int r;
    do {
        if (!o->think) { obj_destroy_now(o); return; }
        r = o->think(o);
    } while (r != 0);
    if (g_tick > MAN_DRAWN(o) + 0x78) { obj_destroy_now(o); return; } /* not drawn for 120 ticks */
    int32_t v = MAN_FRAME(o), step = man_anim_rate * g_dt, u;
    switch (MAN_ANIM(o)) {
    case 0:                                                           /* walking: frames 0..5 */
        o->gfx = &rgfx_43f900;
        u = v + step;
        if (u > 0x5ffff) { MAN_FRAME(o) = (int32_t)((uint32_t)u % 0x60000u); return; }
        break;
    case 1:                                                           /* standing: up to frame 9 */
        o->gfx = &rgfx_43f900;
        u = v + step;
        if (u > 0x8ffff) { MAN_FRAME(o) = 0x90000; return; }
        break;
    case 2:                                                           /* throwing: towards frame 6 */
        o->gfx = &rgfx_43f900;
        if (v < 0x60000) { u = v + step; if (u > 0x60000) { MAN_FRAME(o) = 0x60000; return; } }
        else { u = v - step; if (u < 0x60000) { MAN_FRAME(o) = 0x60000; return; } }
        break;
    case 3:                                                           /* swimming: frames 10..17 */
        o->gfx = &rgfx_43f998;
        u = v + step;
        if (u > 0x11ffff) u -= (int32_t)((uint32_t)(u - 0xa0000) & 0xfff80000u);
        if (u < 0xa0000) { MAN_FRAME(o) = 0xa0000; return; }
        break;
    case 4:                                                           /* treading water: frames 18..19 */
        o->gfx = &rgfx_43f998;
        u = v + step;
        if (u > 0x13ffff) u -= (int32_t)((uint32_t)(u - 0x120000) & 0xfffe0000u);
        if (u < 0x120000) u = 0x120000;
        break;
    default:
        MAN_ANIM(o) = 0;
        o->gfx = &rgfx_43f900;
        MAN_FRAME(o) = 0;
        return;
    }
    MAN_FRAME(o) = u;
}

static uint32_t man_hit_static(Obj *o, uint32_t *cell, int bno)      /* 0x4064b0 */
{
    (void)bno;
    EXT(o)->cell = cell;
    return (o->flags & OF_MAN_PASS) == 0;
}

static void man_die(Obj *o)                                           /* shared tail of 0x4064d0 / 0x406550 */
{
    snd(SND_MAN_CRUSH, o);
    if (water_state(o) == 0)
        spawn_stay(rand_range(3) * 2 + o->team, o->pos, &rgfx_43fa30, 0x3c, 0x78);   /* body: sprite 806 + team */
    obj_mark_delete(o);
}

static uint32_t man_hit_object(Obj *o, Obj *other)                   /* 0x4064d0 */
{
    if (other->cls->id != 1) {
        EXT(o)->bumped = other;
        return (o->flags & OF_MAN_PASS) == 0;
    }
    man_die(o);                                                       /* run over by a vehicle */
    return 1;
}

static int man_damage(Obj *o, Obj *src, int32_t amount)              /* 0x406550 */
{
    (void)src; (void)amount;
    man_die(o);
    return 0;
}

/* FUN_00406a30: head `h` and step `spd` px; clears the bump records. Returns 1 when blocked. */
static int try_move(Obj *o, uint32_t h, int32_t spd)
{
    o->heading = h;
    int32_t d[3] = { fix_mul(spd, dir_vec[h >> 16][0]), fix_mul(spd, dir_vec[h >> 16][1]), 0 };
    EXT(o)->cell = NULL;
    MAN_ANGLE(o) = 0;
    EXT(o)->bumped = NULL;
    return obj_move(o, d);
}

static inline bool within_90(uint32_t h, uint32_t h2)
{
    uint32_t d = (h - h2) & ANG_MASK;
    return d > 0x300000 || d < 0x100000;
}

static int think_exit(Obj *o)                                         /* 0x406cd0: walk out of the building */
{
    ManExt *x = EXT(o);
    x->cell = NULL;
    x->bumped = NULL;
    obj_check_collision(o);
    uint32_t *c = x->cell;
    int32_t d[3] = { 0, 0, 0 };
    if (c) { d[0] = dir_vec[o->heading >> 16][0] << 2; d[1] = dir_vec[o->heading >> 16][1] << 2; }
    o->flags |= OF_MAN_PASS;                                          /* statics / men recorded, not blocking */
    for (int guard = 0; c && guard < 4096; guard++) {                 /* (the original has no guard) */
        x->cell = NULL;
        x->bumped = NULL;
        obj_move(o, d);
        c = x->cell;
    }
    o->flags &= ~OF_MAN_PASS;
    Obj *b = x->bumped;
    if (b) {
        if (b->cls->id != 14) { o->think = NULL; return 1; }          /* stuck in something: removed */
        int32_t rx = b->pos[0] - o->pos[0], ry = b->pos[1] - o->pos[1];
        int k;
        for (k = 0; k < 8; k++) {                                     /* try the spots around the other man */
            int32_t dd[3] = { man_unstick[k][0] + rx, man_unstick[k][1] + ry, 0 };
            if (!obj_move(o, dd)) break;
            x->cell = NULL;
            x->bumped = NULL;
            if (!obj_move(o, dd)) break;
        }
        if (k == 8) { o->think = NULL; return 1; }
    }
    o->think = think_pick;
    return 1;
}

static int think_pick(Obj *o)                                         /* 0x406bc0: react to the nearest vehicle */
{
    int32_t best = 0x7fffffff;
    Obj *tgt = NULL;
    for (int t = 0; t < 2; t++) {
        Obj *v = G.cur_vehicle[t];
        if (!v) {                                                     /* else the team's latest wreck (FUN_00406180) */
            v = G.man_focus[t];
            if (v && G.man_focus_id[t] != v->id) { G.man_focus[t] = NULL; v = NULL; }
        }
        if (!v) continue;
        int32_t d = dist2d(o->pos, v->pos);
        if (d < best) { best = d; tgt = v; }
    }
    obj_set_parent(o, tgt);
    int st = 0;
    if (best < 0x1000001) {                                           /* within 256 px */
        int ty = tgt->cls->id == 1 && tgt->p60 ? veh_state(tgt)->def->type : -1;
        st = (ty == VT_TANK || ty == VT_MSV) ? 2 : 1;                 /* PTR_FUN_0043faa0: both run away */
    }
    o->think = st ? think_flee : think_idle;
    MAN_TIMER(o) = g_tick + rand_range(0x5a) + 0x1e;
    if (st) MAN_ANGLE(o) = (int8_t)(rand_range(5) - 2);
    return 1;
}

static int think_idle(Obj *o)                                         /* 0x4065b0 */
{
    if (MAN_TIMER(o) < g_tick) { o->think = think_pick; return 1; }
    int w = water_state(o);
    MAN_WATER(o) = (uint8_t)w;
    MAN_ANIM(o) = w == 0 ? 1 : 4;
    return 0;
}

static int think_flee(Obj *o)                                         /* 0x4065f0 */
{
    Obj *t = o->parent;
    if (!t || MAN_TIMER(o) < g_tick) {
        if (t && t->team != o->team && MAN_GRENADES(o) > 0 && water_state(o) == 0 && rand_range(3) == 0) {
            int32_t d = dist2d(o->pos, t->pos);
            if (d < 0x600000 && man_grenade_min < d) { o->think = think_throw; return 1; }
        }
        o->think = think_pick;
        return 1;
    }
    uint32_t a = angle_between(o->pos, t->pos);
    int8_t off = MAN_ANGLE(o);
    int w = water_state(o);
    uint32_t h = ((uint32_t)(off + 0x20) * 0x10000u + a) & ANG_MASK;  /* away from it */
    MAN_WATER(o) = (uint8_t)w;
    int32_t spd = man_walk_speed * g_dt;
    int blocked = try_move(o, h, spd);
    ManExt *x = EXT(o);
    bool moved;
    if (!x->cell || !g_hit_part_b) {
        Obj *b = x->bumped;
        if (b) {                                                      /* step away from what was bumped */
            uint32_t h2 = (angle_between(o->pos, b->pos) + 0x200000) & ANG_MASK;
            if (within_90(h, h2) && !try_move(o, h2, spd)) { moved = true; goto done; }
            h2 = (h2 + 0x100000) & ANG_MASK;
            if (within_90(h, h2) && !try_move(o, h2, spd)) { moved = true; goto done; }
            h2 = (h2 + 0x200000) & ANG_MASK;
            if (within_90(h, h2) && !try_move(o, h2, spd)) { moved = true; goto done; }
        }
    } else {                                                          /* go round the static part */
        int32_t cp[3];
        cell_to_pos(x->cell, cp);
        cp[0] += g_hit_part_b->dx;
        cp[1] += g_hit_part_b->dy;
        if (o->pos[0] < g_hit_part_b->bbox[0] + cp[0] || g_hit_part_b->bbox[2] + cp[0] < o->pos[0]) {
            uint32_t hh = (h > 0x2fffff || h < 0x100000) ? 0 : 0x200000;
            if (!try_move(o, hh, spd)) { moved = true; goto done; }
        }
        if (o->pos[1] < g_hit_part_b->bbox[1] + cp[1] || g_hit_part_b->bbox[3] + cp[1] < o->pos[1]) {
            uint32_t hh = h < 0x200000 ? 0x100000 : 0x300000;
            if (!try_move(o, hh, spd)) { moved = true; goto done; }
        }
    }
    moved = blocked == 0;
done:
    if (moved) MAN_ANIM(o) = MAN_WATER(o) ? 3 : 0;
    else MAN_ANIM(o) = MAN_WATER(o) ? 4 : 1;
    if (!moved && t->team != o->team && MAN_GRENADES(o) > 0 && MAN_FRAME(o) == 0x90000 && MAN_ANIM(o) == 1 &&
        dist2d(o->pos, t->pos) < 0x600000) {
        o->think = think_throw;
        return 1;
    }
    return 0;
}

static int think_throw(Obj *o)                                        /* 0x406a90 */
{
    Obj *t = o->parent;
    if (!t) { o->think = think_pick; return 1; }
    MAN_ANIM(o) = 2;
    if (MAN_FRAME(o) < 0x60001) {
        MAN_GRENADES(o)--;
        int32_t r = rand_range(0x3c);
        MAN_FRAME(o) = 0x60000;
        o->think = think_wait;
        MAN_TIMER(o) = g_tick + r + 0x78;
        int32_t tp[3] = { t->pos[0], t->pos[1], t->pos[2] };
        int32_t d = dist2d(o->pos, t->pos);
        if (d > 0x600000) { o->think = think_pick; return 1; }
        int32_t k = d >> 18;                                          /* spread: distance / 4 px */
        int32_t a = rand_range(k), b = rand_range(k);
        tp[0] += (a - b) * 0x10000;
        a = rand_range(k); b = rand_range(k);
        tp[1] += (a - b) * 0x10000;
        throw_grenade(o, NULL, tp);
    }
    return 0;
}

static int think_wait(Obj *o)                                         /* 0x406b90 */
{
    MAN_ANIM(o) = 1;
    if (o->parent && g_tick <= MAN_TIMER(o)) return 0;
    o->think = think_pick;
    return 1;
}

const ObjClass class_man = {
    14, OBJ_CLASS_NAME, man_init, man_destroy, man_update, &rgfx_43f900, NULL, think_exit,
    NULL, NULL, NULL, NULL, 0x7d, man_hit_static, man_hit_object, man_damage, NULL, 0
};

Obj *spawn_man(int team, int32_t x, int32_t y)                        /* SpawnMan 0x4071c0 */
{
    return obj_create(&class_man, team, x, y, 0, NULL);
}

/* SpawnMenFromBuilding 0x4071f0 (BnoDamage when a building is nearly destroyed): BNO +0x0c bits 16-19 / 20-23
   give d = hi - lo, d + RandRange(d) men of the cell's side (prisons, flag 0x8000: the other side = freed
   prisoners) appear in the west half of the building's first shape part, on its centre line (the y
   RandRange of a 16.16 extent wraps to < 1 px, as in the original). Returns d. */
int spawn_men_from_building(uint32_t *cell, const BnoDef *d)
{
    int team = CELL_TEAM(*cell);
    uint32_t f = d->flags;
    if (f & 0x8000) team ^= 1;
    int n = (int)((f & 0xf00000) >> 20) - (int)((f & 0xf0000) >> 16);
    if (n < 1) return 0;
    int ret = n;
    n += rand_range(n);
    if (n < 1) return 0;
    int32_t p[3];
    cell_to_pos(cell, p);
    const ShapePart *s = d->gfx ? d->gfx->shape : NULL;
    if (s) {
        p[0] += s->dx;
        p[1] += s->dy;
        for (; n > 0; n--) {
            int32_t x0 = s->bbox[0];
            uint32_t a = (uint32_t)rand_range((s->bbox[2] - x0) >> 17);
            uint32_t b = (uint32_t)rand_range(-s->bbox[1]);
            obj_create(&class_man, team, p[0] + x0 + (int32_t)(a * 0x10000u), p[1] - (int32_t)(b & 0xffff0000u), 0, NULL);
        }
        return ret;
    }
    for (; n > 0; n--) obj_create(&class_man, team, p[0], p[1], 0, NULL);
    return ret;
}
