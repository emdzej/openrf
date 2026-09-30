/* Collision against map cells: ObjCheckCollision 0x41dcf0, CollideWithCell 0x41dee0,
   CollideWithCellContents 0x41dac0, BNO touch handlers 0x41f260..0x41f6a0, water state 0x4185e0. */
#include "game.h"
#include "vehicle.h"
#include "effect.h"
#include "weapon.h"
#include "rules.h"
#include <stddef.h>
#include <string.h>

static int need_outside;   /* DAT_00457f50: the object reaches past the map edge */
static int second_pass;    /* DAT_00443e10: the outside pseudo-cell may be tested */
static int self_outside;   /* DAT_00457f4c: the object itself is in the outside cell */

int cell_index(const uint32_t *cell) { return (int)(cell - G.cell); }

void cell_to_pos(const uint32_t *cell, int32_t *pos)
{
    uint32_t i = (uint32_t)cell_index(cell);
    pos[0] = (int32_t)((i & 0x7f) * 0x200000 + 0x100000);
    pos[1] = (int32_t)((i & 0xffffff80u) * 0x4000 + 0x100000);
    pos[2] = 0;
}

uint32_t *cell_at(const int32_t *p)
{
    if (p[0] >= 0 && p[0] < WORLD_SIZE && p[1] >= 0 && p[1] < WORLD_SIZE)
        return &G.cell[(p[1] >> 21) * 128 + (p[0] >> 21)];
    return &G.outside;
}

/* FUN_0041f210: per-cell random offset of bushes/palms/rocks from the table built by FUN_0041f190. */
static void gfx_adjust(const Gfx *g, int32_t *p)
{
    if (g->adjust != GADJ_JITTER) return;
    uint32_t x = (uint32_t)p[0];
    /* byte index ((y & 0x1e00000) >> 17) * 4 + ((x & 0x1e00000) >> 19) = entry (y>>21 & 15)*16 + (x>>21 & 15) */
    const int8_t *j = G.jitter[(((uint32_t)p[1] >> 21) & 15) * 16 + ((x >> 21) & 15)];
    p[0] = j[0] * 0x10000 + (int32_t)x;
    p[1] = j[1] * 0x10000 + p[1];
}

void set_cell_static(int bno, uint32_t *c, int hp_bonus, int team)
{
    if (bno > 0 && bno < BNO_COUNT && bno_defs[bno].gfx) {
        const BnoDef *d = &bno_defs[bno];
        if (d->terrain != 0xff) {
            if (d->flags == 8) CELL_SET_TERRAIN(*c, d->terrain + team);
            else CELL_SET_TERRAIN(*c, d->terrain);
        }
        uint32_t w = (*c & ~0x3f80u) | ((uint32_t)bno << 7);
        int hp = 0;
        if (d->hp) { hp = hp_bonus + d->hp; if (hp > 15) hp = 15; if (hp < 1) hp = 1; }
        w = (w & ~0xe000000u) | (((uint32_t)hp << 25) & 0xe000000u);
        *c = (w & ~0xc000u) | (((uint32_t)team << 14) & 0xc000u);
        return;   /* RadarUpdateCell 0x422930 not ported */
    }
    *c &= 0xf1ff0000u;   /* terrain, object, team and hp cleared (chain bits kept) */
}

void bno_adjust_pos(const BnoDef *d, int32_t *p)
{
    if (d && d->gfx) gfx_adjust(d->gfx, p);
}

/* FUN_00417960: replace the cell's BNO by its destroyed variant (BNO +0x31) / terrain (+0x30 + variant). */
void bno_set_destroyed(uint32_t *c, const BnoDef *d, int team)
{
    int v = 0;
    if (d->flags & 4) v = rand_range(4) & 0xff;
    else if (d->flags & 2) v = rand_range(2) & 0xff;
    if (d->dest_bno == 0) {
        *c &= 0xffffc07fu;
        CELL_SET_TERRAIN(*c, d->dest_terrain + v);
    } else {
        set_cell_static(d->dest_bno, c, 0, team);
        if (d->dest_terrain) CELL_SET_TERRAIN(*c, v + d->dest_terrain);
    }
    /* RadarUpdateCell(c, 1): the radar image is rebuilt from the cells every frame (hud.c) */
}

static int use_crush_expl;       /* Bno_TouchBush swaps BNO +0x28 into +0x2c around the damage call */

/* BnoDestroy 0x417a00: explosion at the cell centre (+jitter, + model offset for flag 0x10); the explosion
   script applies the destroyed state (op 4) when it has one, otherwise it is applied at once. */
int bno_destroy(Obj *src, uint32_t *c, const BnoDef *d)
{
    (void)src;
    if (d == &bno_defs[0]) return 1;
    int team = CELL_TEAM(*c);
    int32_t p[3];
    cell_to_pos(c, p);
    if (d->gfx) gfx_adjust(d->gfx, p);
    if ((d->flags & 0x10) && d->gfx) {
        const ModelPart *m = model_part_by_addr(d->gfx->addr);
        if (m) { p[0] += m->off[0]; p[1] += m->off[1]; }
    }
    uint32_t e = use_crush_expl ? d->expl_crush : d->expl;
    Obj *x = e ? spawn_explosion(team, p[0], p[1], 0, e, c) : NULL;
    if (x) return 1;
    bno_set_destroyed(c, d, team);
    return 1;
}

int32_t tower_yaw = -1, tower_pitch;   /* DAT_0044ad30 / DAT_004588c8: pose of the turret that died (Turret port) */

/* TowerDestroyed 0x423510: the turret flies off as debris, then the default destroy. */
static int tower_destroyed(Obj *src, uint32_t *c, const BnoDef *d)
{
    int32_t p[3];
    cell_to_pos(c, p);
    if (tower_yaw < 0) {                                           /* FUN_004174b0: rest pose from the jitter */
        const int8_t *j = G.jitter[(cell_index(c) & 0xf) + ((cell_index(c) & 0x1e0) >> 1)];
        tower_yaw = ((uint8_t)j[3] & 0x3f) << 16;
        tower_pitch = (int32_t)((uint32_t)(uint8_t)j[0] << 14);
    }
    spawn_debris_burst(p, CELL_TEAM(*c), debris_set_tower, d->gfx ? model_part_by_addr(d->gfx->addr) : NULL,
                       (uint32_t)tower_yaw, tower_pitch, NULL);
    tower_yaw = -1;
    bno_destroy(src, c, d);
    return 1;
}

/* HideFlagInRandomBuilding 0x424060 */
int hide_flag_in_building(int team)
{
    Obj *f = G.flag_obj[team];
    int n = 0;
    for (int i = 0; i < G.nflags[team]; i++) if (CELL_BNO(*G.flag_sites[team][i]) == 22) n++;
    if (n == 0) {
        if (!f) return 0;
        uint32_t *c = G.flag_sites[team][rand_range(G.nflags[team])];
        cell_to_pos(c, f->pos);
        obj_link_cell(f, c);
        return 0;
    }
    int r = rand_range(n);
    for (int i = 0; i < G.nflags[team]; i++) {
        uint32_t *c = G.flag_sites[team][i];
        if (CELL_BNO(*c) != 22) continue;
        if (r < 1) {
            G.flag_cell[team] = c;
            if (f) { obj_mark_delete(f); G.flag_obj[team] = NULL; }
            return 1;
        }
        r--;
    }
    return 0;
}

/* FlagBuildingDestroyed 0x424170: once the building hiding the flag (or enough flag buildings) is gone,
   the Flag object appears there (the Flag class is a hook). */
static int flag_building_destroyed(Obj *src, uint32_t *c, const BnoDef *d)
{
    int team = CELL_TEAM(*c);
    int r = bno_destroy(src, c, d);
    if (G.flag_obj[team]) return r;
    int left = --G.flag_left[team];
    /* DAT_00440d40 (never set) == 0 */
    if (left >= 0 || G.flag_cell[team] != c) {
        if (G.flag_cell[team] != c) return r;
        uint32_t saved = *c;
        *c &= 0xffffc07fu;
        int moved = hide_flag_in_building(team);
        *c = saved;
        if (moved) return r;
    }
    int32_t p[3];
    cell_to_pos(c, p);
    if (!G.flag_obj[team] && game_hooks.flag_spawn) G.flag_obj[team] = game_hooks.flag_spawn(team, p);
    return r;
}

/* FUN_0041f9a0 (BNO 74/75 bridge decks): 2..8 deck planks fall as debris, then the default destroy. */
static int bridge_destroyed(Obj *src, uint32_t *c, const BnoDef *d)
{
    int n = rand_range(3) + 2 + rand_range(5);
    bool vert = (d - bno_defs) == 0x4b;
    int32_t p[3];
    cell_to_pos(c, p);
    p[2] = 0;
    if (!vert) p[0] -= 0x100000; else p[1] -= 0x100000;
    int room = 7;
    int32_t v[4][3];
    static const int32_t idx[4] = { 0, 1, 2, 3 };                     /* 0x44a118 */
    while (n-- > 0) {
        int step = room - n < 0 ? 0 : rand_range(room - n) + 1;
        room -= step;
        if (!vert) p[0] += step * 0x40000; else p[1] += step * 0x40000;
        int32_t w = rand_range(tun_bridge[5]) * 0x10000 + tun_bridge[6];
        int32_t o = (rand_range(0x20 - (w >> 16)) - 0x10) * 0x10000;
        memcpy(v, bridge_debris_verts[vert ? 0 : 1], sizeof v);
        if (!vert) { v[0][1] = o; v[1][1] = o + w; v[2][1] = o + w; v[3][1] = o; }
        else { v[0][0] = o; v[1][0] = o + w; v[2][0] = o + w; v[3][0] = o; }
        Obj *x = spawn_debris(src ? src->team : CELL_TEAM(*c), p, &debris_44a0a0, 0xa6, (const int32_t (*)[3])v, idx);
        if (!x) continue;
        if (src) src->heading = vert ? 0x100000 : 0;                   /* (the original writes the source's heading) */
        DebrisExt *e = &debris_ext[x->id & 0x1ff];
        int32_t k = rand_range(tun_bridge[4]) + rand_range(tun_bridge[3]);
        if (rand_range(2)) k = -k;
        e->vel[0] = k;
        k = rand_range(tun_bridge[4]) + rand_range(tun_bridge[3]);
        if (rand_range(2)) k = -k;
        e->vel[1] = k;
        e->vel[2] = rand_range(tun_bridge[0]) * 10 + tun_bridge[1];
        if (src && src->cls->id == 0) {                                /* a shell pushes the planks along (x/y swapped) */
            e->vel[0] += fix_mul(src->u68, tun_bridge[2]);
            e->vel[1] += fix_mul(src->u64, tun_bridge[2]);
        }
    }
    return bno_destroy(src, c, d);                                     /* + RadarUpdateCell */
}

static int bno_damage_ex(int32_t amount, Obj *src, uint32_t *c, const BnoDef *d, int crush)   /* BnoDamage 0x417c20 */
{
    const BnoDef *p = d ? d : &bno_defs[CELL_BNO(*c)];
    uint32_t hp = *c & 0xe000000u;
    int killed = 0;
    int32_t v;
    if (hp && (v = amount - p->armour) > 0) {
        if (p->mult) v *= p->mult;
        /* BNO +0x18 (custom damage function) is 0 for every BNO */
        v >>= 16;
        if (v < 1) v = 1;
        int32_t h = (int32_t)(hp >> 25);
        if (h <= v) { killed = 1; *c &= 0xf1ffffffu; }
        else {
            if (h > 1 && h - v < 2 && (p->flags & 0xff8000u) && game_hooks.spawn_men)
                game_hooks.spawn_men(c, p);                            /* SpawnMenFromBuilding 0x4071f0 */
            *c = (*c & ~0xe000000u) | (((uint32_t)(h - v) * 0x2000000u) & 0xe000000u);
        }
    }
    if (!killed) return -1;
    use_crush_expl = crush;
    switch (p->destroy) {
    case BDES_TOWER: tower_destroyed(src, c, p); break;
    case BDES_FLAG_BUILDING: flag_building_destroyed(src, c, p); break;
    case BDES_BRIDGE: bridge_destroyed(src, c, p); break;
    default: bno_destroy(src, c, p); break;             /* BDES_WALL uses BnoDestroy itself as the hook */
    }
    use_crush_expl = 0;
    return 1;
}

int bno_damage(int32_t amount, Obj *src, uint32_t *c, const BnoDef *d) { return bno_damage_ex(amount, src, c, d, 0); }

/* ---- BNO touch handlers (BNO +0x14), called as hit(obj, cell, bno). Bit 0 = block, bit 3 = ignore. ---- */
static uint32_t bno_touch(Obj *o, uint32_t *c, int bno)
{
    const BnoDef *d = &bno_defs[bno];
    bool veh = o->cls->id == 1;
    switch (d->hit) {
    case BHIT_ROCK:                              /* 0x41f260: only jeeps are stopped by rocks */
        if (!veh) return 0;
        return veh_type(o) == VT_JEEP ? 0 : 8;
    case BHIT_BUSH:                              /* 0x41f290 */
        if (!veh) return 0;
        if (g_hit_part_b && g_hit_part_b->mask == 4) return 8;
        if (veh_type(o) == VT_JEEP) return 0;
        if (o->speed > 0x8000) {
            /* crushed: BNO +0x28 (0x4545d0) swapped into +0x2c around the call; side bits 1 = crushed */
            bno_damage_ex(0x640000, o, c, d, 1);
            *c = (*c & 0xffff7fffu) | 0x4000;
            return 8;
        }
        return 1;
    case BHIT_PICKUP:                            /* 0x41f340: fuel/ammo dump or gate part */
        if (g_hit_part_b && (g_hit_part_b->b8 & 2) && veh) {
            VehState *s = veh_state(o);
            s->pickup_cell = c;
            s->pickup_part = g_hit_part_b;
            return 8;
        }
        return 0;
    case BHIT_TENT:                              /* 0x41f6a0 */
        if (!veh) return 0;
        if (o->speed > 0x8000) { bno_damage(0x640000, o, c, d); return 8; }
        return 1;
    case BHIT_DEST_FLAG:                         /* DestFlagOnTouch 0x4247e0: a jeep picks up the flag lying there */
        return (uint32_t)flag_touch_dest_building(o, c);
    default:
        return 0;
    }
}

/* CollideWithCellContents 0x41dac0: the static object of the cell (at cell centre `cp`), then the chain. */
static int collide_contents(uint32_t *c, const int32_t *cp, Obj *o)
{
    const ObjClass *cls = o->cls;
    if (c == &G.outside) {
        if (!second_pass) return 0;
    } else {
        int bno = CELL_BNO(*c);
        if (bno && bno_defs[bno].gfx) {
            const Gfx *g = bno_defs[bno].gfx;
            int32_t lp[3] = { cp[0], cp[1], cp[2] };
            const int32_t *pp = cp;
            if (g->adjust) { gfx_adjust(g, lp); pp = lp; }
            if (shapes_overlap(o->pos, o->heading, o->gfx->shape, pp, 0, g->shape)) {
                if (bno_defs[bno].hit != BHIT_NONE) {
                    uint32_t r = bno_touch(o, c, bno);
                    if (r & 1) return 1;
                    if (r & 8) goto dynamic;
                }
                if (!cls->hit_static) return 1;
                if (cls->hit_static(o, c, bno) & 1) return 1;
            }
        }
    }
dynamic:
    for (Obj *p = obj_from_slot(CELL_HEAD(*c)); p; p = p->cnext) {
        if (p == o || !p->gfx) continue;
        if (!shapes_overlap(o->pos, o->heading, o->gfx->shape, p->pos, p->heading, p->gfx->shape)) continue;
        const ObjClass *oc = p->cls;
        if (cls->prio < oc->prio) {
            if (!oc->hit_object) {
                if (!cls->hit_object) return 1;
                if (cls->hit_object(o, p) & 1) return 1;
            } else if (oc->hit_object(p, o) == 4) return 1;
        } else if (!cls->hit_object) {
            if (!oc->hit_object) return 1;
            if (oc->hit_object(p, o) == 4) return 1;
        } else if (cls->hit_object(o, p) & 1) return 1;
    }
    return 0;
}

/* CollideWithCell 0x41dee0: the cell and its west/east neighbours. */
static int collide_cell(uint32_t *c, Obj *o, const Gfx *g, int32_t *cp)
{
    if (collide_contents(c, cp, o)) return 1;
    int32_t fx = o->pos[0] & 0x1f0000;
    if ((fx - g->radius) < 0x100000 || g->radius > -1) {
        if (o->pos[0] < 0x200000) need_outside = 1;
        else {
            cp[0] -= 0x200000;
            uint32_t *n = self_outside ? cell_at(cp) : c - 1;
            if (collide_contents(n, cp, o)) return 1;
            cp[0] += 0x200000;
        }
    }
    if ((fx + g->radius) > 0xfffff || g->radius > -1) {
        if (o->pos[0] > 0xfdfffff) { need_outside = 1; return 0; }
        cp[0] += 0x200000;
        uint32_t *n = self_outside ? cell_at(cp) : c + 1;
        if (collide_contents(n, cp, o)) return 1;
        cp[0] -= 0x200000;
    }
    return 0;
}

int obj_check_collision(Obj *o)
{
    need_outside = 0;
    second_pass = 0;
    const Gfx *g = o->gfx;
    uint32_t *c = o->cell;
    self_outside = c == &G.outside;
    if (!g || !g->shape || (o->flags & OF_NOCOLLIDE)) return 0;
    if (g->shape->type == 4 && g_move_old_pos) shape_sweep_setup(g->shape, o->pos, g_move_old_pos);
    int32_t cp[3] = { (int32_t)(((uint32_t)o->pos[0] & 0xffe00000u) + 0x100000),
                      (int32_t)(((uint32_t)o->pos[1] & 0xffe00000u) + 0x100000), 0 };
    if (collide_cell(c, o, g, cp)) return 1;
    int32_t fy = o->pos[1] & 0x1f0000;
    if ((fy - g->radius) < 0x100000 || g->radius > 0) {
        if (o->pos[1] < 0x200001) need_outside = 1;
        else {
            cp[1] -= 0x200000;
            uint32_t *n = self_outside ? cell_at(cp) : c - 128;
            if (collide_cell(n, o, g, cp)) return 1;
            cp[1] += 0x200000;
        }
    }
    if ((fy + g->radius) > 0xfffff || g->radius > 0) {
        if (o->pos[1] < 0xfe00000) {
            cp[1] += 0x200000;
            uint32_t *n = self_outside ? cell_at(cp) : c + 128;
            if (collide_cell(n, o, g, cp)) return 1;
            cp[1] -= 0x200000;
        } else need_outside = 1;
    }
    second_pass = 1;
    if (need_outside && collide_contents(&G.outside, cp, o)) return 1;
    return 0;
}

/* ---- water ---- */
static int water_of_cell(const int32_t *pos, uint32_t heading, ShapePart *shape, uint32_t *c)
{
    uint32_t w = *c, bno = CELL_BNO(w), t = CELL_TERRAIN(w);
    if (t > 0x33 || bno == BNO_BRIDGE_H || bno == BNO_BRIDGE_V) return 0;
    if (t == 1) return 1;
    if (t == 2) return 2;
    if (t < 4) return 0;
    int32_t cp[3];
    cell_to_pos(c, cp);
    if (t < 0x18) {
        const ShoreShape *s = &shore_shapes[t];
        if (s->shape) {
            uint32_t hit = shape_part_overlap(pos, heading, shape, cp, (uint32_t)((int32_t)s->rot << 16), s->shape);
            if (hit == 0) { if (s->invert == 0) return 1; }
            else if (s->invert) return 1;
        }
        return 0;
    }
    if ((int)(t - 0x28) < 0) return 1;
    return point_in_box(pos, cp, shore_boxes[t - 0x28]) ? 1 : 2;
}

int water_state(Obj *o)
{
    if (o->pos[2] > 0x10000) return 0;
    ShapePart *s = (o->gfx && o->gfx->shape) ? o->gfx->shape : shape_default_point;
    return water_of_cell(o->pos, o->heading, s, o->cell);
}

int water_state_at(const int32_t *pos)
{
    if (pos[2] > 0x10000) return 0;
    return water_of_cell(pos, 0, shape_default_point, cell_at(pos));
}

int cell_water_type(uint32_t w)
{
    uint32_t t = CELL_TERRAIN(w);
    if (t > 0x33) return 0;
    if (t == 1) return 1;
    if (t == 2) return 2;
    return t > 3 ? 1 : 0;
}
