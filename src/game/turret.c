/* Tower turrets. A tower cell (BNO 49 BNO_TOWER / 50 BNO_LARGE_TOWER) with hit points is static until
   it is queued for drawing in a view whose vehicle belongs to the other side (Model_TowerHook 0x4174e0);
   then ActivateTurret 0x423c10 turns it into a live "Turret Gun" object (class 2, 0x44ad40 / large
   0x44ad90) and the cell into BNO 90 BNO_ACTIVE_TOWER (hidden graphic, no shape). The object aims at
   the vehicle (think 0x423780), probes the line of fire with a TRACER and shoots type-4 shells
   (0x4239e0), rotates back to rest when the target is gone (0x423ac0) and turns back into the static
   tower when it has not been drawn for 300 ticks (TurretUpdate 0x423620 / TurretDestroy 0x423660).
   Damage goes to the cell hit points (0x423710 -> FUN_00417b50); at 0 the tower is destroyed with the
   turret flying off in its live pose (TowerDestroyed 0x423510). See docs/game.md §9. */
#include "ai.h"
#include "weapon.h"
#include "../exe.h"
#include <stddef.h>

extern Gfx gfx_441840, gfx_4418a0;

int32_t turret_count[2];                     /* DAT_00472048 */
int32_t turret_near_dist[2] = { 0x7fffffff, 0x7fffffff };   /* DAT_0044ad38 / DAT_0044ad3c */
int32_t turret_near_pos[2][3];               /* DAT_00471820 (team * 0xc) */
static int32_t near_tick = -1;               /* DAT_004588a8 */

/* Muzzle offsets (obj +0x70): 0x441308 (normal) / 0x441580 (large); [2] is also the pitch pivot. */
static int32_t muzzle_normal[3], muzzle_large[3];
static void turret_tables_load(void)
{
    for (int i = 0; i < 3; i++) {
        muzzle_normal[i] = exe_s32(0x441308 + 4 * (uint32_t)i);
        muzzle_large[i] = exe_s32(0x441580 + 4 * (uint32_t)i);
    }
}
EXE_LOADER(turret_tables_load)

#define TURRET_BNO(o)    ((o)->i60 & 0xff)            /* +0x60 byte */
#define TURRET_HP(o)     (((o)->i60 >> 8) & 0xff)     /* +0x61 byte */
#define TURRET_WAKE(o)   ((o)->u64)                   /* +0x64 */
#define TURRET_NEXT(o)   ((o)->u68)                   /* +0x68 */
#define TURRET_REST(o)   ((o)->u6c)                   /* +0x6c rest yaw */
static const int32_t *turret_muzzle(const Obj *o) { return o->cls == &class_turret_large ? muzzle_large : muzzle_normal; }

static int think_aim(Obj *o);
static int think_fire(Obj *o);
static int think_rest(Obj *o);

/* FUN_00417b50: BnoDamage without the destroy step (only the cell hit points). 1 = HP reached 0. */
int bno_damage_hp(int32_t amount, Obj *src, uint32_t *c, const BnoDef *d)
{
    (void)src;
    if (!d) d = &bno_defs[CELL_BNO(*c)];
    uint32_t hp = *c & 0xe000000u;
    if (!hp) return 0;
    int32_t v = amount - d->armour;
    if (v < 1) return 0;
    if (d->mult) v *= d->mult;
    /* BNO +0x18 (custom damage function) is 0 for every BNO */
    v >>= 16;
    if (v < 1) v = 1;
    int32_t h = (int32_t)(hp >> 25);
    if (h <= v) { *c &= 0xf1ffffffu; return 1; }
    if (h > 1 && h - v < 2 && (d->flags & 0xff8000u) && game_hooks.spawn_men) game_hooks.spawn_men(c, d);
    *c = (*c & ~0xe000000u) | (((uint32_t)(h - v) * 0x2000000u) & 0xe000000u);
    return 0;
}

/* FUN_004174b0: the static pose of a tower cell from its jitter entry (CellJitterEntry 0x417820). */
static void tower_rest_pose(const uint32_t *c, uint32_t *yaw, uint32_t *pitch)
{
    int ci = cell_index(c);
    const int8_t *j = G.jitter[(ci & 0xf) + ((ci & 0x1e0) >> 1)];
    *yaw = ((uint32_t)(uint8_t)j[3] & 0x3f) << 16;
    *pitch = (uint32_t)(uint8_t)j[0] << 14;
}

/* The per-tick "nearest turret to each side's vehicle" record shared by 0x423780 / 0x4239e0 (written
   only; nothing in the retail code reads DAT_00471820). */
static int32_t note_nearest(Obj *o, Obj *tg)
{
    if (near_tick != g_tick) {
        near_tick = g_tick;
        turret_near_dist[0] = turret_near_dist[1] = 0x7fffffff;
    }
    int32_t d = dist2d(o->pos, tg->pos);
    int t = tg->team & 1;
    if (d < turret_near_dist[t]) {
        turret_near_dist[t] = d;
        turret_near_pos[t][0] = o->pos[0]; turret_near_pos[t][1] = o->pos[1]; turret_near_pos[t][2] = o->pos[2];
    }
    return d;
}

static int think_wait(Obj *o)                                   /* FUN_00423760: reaction delay */
{
    if (TURRET_WAKE(o) < g_tick) o->think = think_aim;
    return 1;
}

static int think_aim(Obj *o)                                    /* FUN_00423780 */
{
    Obj *tg = o->parent;
    if (!tg) { o->think = think_rest; return 1; }
    bool in_range = false;
    uint32_t pitch_to = 0;                                      /* local_8 */
    uint32_t yaw_to = angle_between(o->pos, tg->pos);
    int32_t d = note_nearest(o, tg);
    if (turn_towards(&o->heading, yaw_to, 0x3333)) {            /* 1.13 deg per tick */
        if (d < 0xc00000) {                                     /* 192 px */
            int32_t p = atan2_fixed(d, turret_muzzle(o)[2] - tg->pos[2]) >> 2;
            pitch_to = (uint32_t)p;
            if (p < 0x3a0000 && p > 0x60000) pitch_to = p < 0x200001 ? 0x60000 : 0x3a0000;   /* +-33.75 deg */
            in_range = true;
        }
    }
    turn_towards(&TURRET_PITCH(o), pitch_to, 0xa3d);
    if (in_range && TURRET_NEXT(o) < g_tick) o->think = think_fire;
    return 1;
}

static int think_fire(Obj *o)                                   /* FUN_004239e0 */
{
    Obj *tg = o->parent;
    if (!tg) { o->think = think_aim; return 1; }
    note_nearest(o, tg);
    fire_projectile(o->pos, turret_muzzle(o), o->heading, (int32_t)TURRET_PITCH(o), 3, o->team, o);   /* TRACER */
    if (tracer_result == 0) {                                   /* nothing of ours in the line of fire */
        fire_projectile(o->pos, turret_muzzle(o), o->heading, (int32_t)TURRET_PITCH(o), 4, o->team, o);
        TURRET_NEXT(o) = g_tick + 60;
    } else TURRET_NEXT(o) = g_tick + 30;
    o->think = think_aim;
    return 1;
}

static int think_rest(Obj *o)                                   /* FUN_00423ac0: back to the rest pose */
{
    if (!turn_towards(&o->heading, (uint32_t)TURRET_REST(o), 0x3333)) return 1;
    if (!turn_towards(&TURRET_PITCH(o), 0, 0xa3d)) return 1;
    o->think = NULL;                                            /* TurretUpdate destroys it */
    return 1;
}

static int turret_init(const ObjClass *c, Obj *o, void *arg)   /* TurretInit 0x4235a0 */
{
    if (arg) obj_set_parent(o, arg);
    TURRET_UNSEEN(o) = 0;
    TURRET_WAKE(o) = (rand_range(300) & 0xff) + g_tick;
    TURRET_NEXT(o) = 0;
    (void)c;                                                    /* +0x70 muzzle: by class graphic (turret_muzzle) */
    turret_count[o->team & 1]++;
    return 1;
}

static void turret_update(const ObjClass *c, Obj *o)            /* TurretUpdate 0x423620 */
{
    (void)c;
    if (o->think && TURRET_HP(o) > 0) {
        int16_t n = (int16_t)(TURRET_UNSEEN(o) + g_dt);
        TURRET_UNSEEN(o) = n;
        if (n < 0x12d) { o->think(o); return; }
    }
    obj_destroy_now(o);
}

static int turret_destroy(const ObjClass *c, Obj *o)            /* TurretDestroy 0x423660 */
{
    (void)c;
    int t = o->team & 1;
    if (--turret_count[t] < 1) {
        turret_count[t] = 0;
        turret_near_dist[t == 0] = 0x7fffffff;
    }
    uint32_t *cell = o->cell;
    if (!cell) return 1;
    int bno = TURRET_BNO(o);
    *cell = (*cell & ~0x3f80u) | ((uint32_t)bno << 7);          /* the static tower again */
    if (*cell & 0xe000000u) return 1;
    tower_yaw = (int32_t)o->heading;                            /* shot down: debris in the live pose */
    tower_pitch = (int32_t)TURRET_PITCH(o);
    *cell = (*cell & 0xf3ffffffu) | 0x2000000u;                 /* HP 1, then destroy it */
    bno_damage(0xff0000, o, cell, &bno_defs[bno]);
    return 1;
}

static int turret_damage(Obj *o, Obj *src, int32_t amount)      /* 0x423710 */
{
    if (bno_damage_hp(amount, src, o->cell, &bno_defs[TURRET_BNO(o)])) {
        obj_mark_delete(o);
        return 1;
    }
    return 0;
}

const ObjClass class_turret = {
    2, OBJ_CLASS_NAME, turret_init, turret_destroy, turret_update, &gfx_441840, NULL, think_wait,
    NULL, NULL, NULL, NULL, 0x32, NULL, NULL, turret_damage, NULL, 0
};
const ObjClass class_turret_large = {
    2, OBJ_CLASS_NAME, turret_init, turret_destroy, turret_update, &gfx_4418a0, NULL, think_wait,
    NULL, NULL, NULL, NULL, 0x32, NULL, NULL, turret_damage, NULL, 0
};

Obj *activate_turret(uint32_t *c, Obj *target)                   /* ActivateTurret 0x423c10 */
{
    if (!(*c & 0xe000000u)) return NULL;
    int ci = cell_index(c);
    int32_t px = (ci & 0x7f) * 0x20 + 0x10, py = (ci >> 7) * 0x20 + 0x10;   /* CellToPixelPos 0x4177b0 */
    const ObjClass *cls = CELL_BNO(*c) == 50 ? &class_turret_large : &class_turret;
    Obj *o = obj_create(cls, CELL_TEAM(*c), px << 16, py << 16, 0, target);
    if (!o) return NULL;
    o->i60 = (int32_t)(((*c >> 7) & 0x7f) | (((*c >> 25) & 7) << 8));   /* +0x60 BNO, +0x61 HP */
    *c = (*c & 0xffffed7fu) | 0x2d00u;                          /* BNO 90 BNO_ACTIVE_TOWER */
    tower_rest_pose(c, &o->heading, &TURRET_PITCH(o));
    TURRET_REST(o) = (int32_t)o->heading;
    return o;
}

Obj *turret_tower_hook(uint32_t *c, int view_team)              /* Model_TowerHook 0x4174e0, sim part */
{
    if (!(*c & 0xe000000u)) return NULL;
    Obj *v = get_team_vehicle(view_team);                       /* view +0x6c */
    if (v && v->team != (int)CELL_TEAM(*c) && v->cls->id == 1) return activate_turret(c, v);
    return NULL;
}
