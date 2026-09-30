/* Projectiles, grenades and mines. Ported from ProjectileInit/Destroy/Update 0x430bb0..0x430dd0,
   the hit handlers 0x4310b0 / 0x431140, TracerInit 0x4311d0 (+0x431340 / 0x4313a0), DeathMissileUpdate
   0x4313e0 (+0x431700), FireProjectile 0x431760, FUN_00431880, GrenadeInit..ThrowGrenade 0x4319c0..0x431c80,
   FUN_00431de0 and the Mine class 0x434e40..0x434fd0. See docs/game.md §6. */
#include "weapon.h"
#include "effect.h"
#include <stddef.h>

extern Gfx gfx_449aa0, gfx_449ae8, gfx_449628, gfx_4495c0, gfx_4493b0, gfx_449478;

int tracer_result;                /* DAT_0046f3c4 */
static Obj *tracer_owner;         /* _DAT_00459684 */

static void snd(int cmd, uint32_t ev, Obj *o) { if (game_hooks.sound) game_hooks.sound(cmd, ev, o); }

int32_t dist2d(const int32_t *a, const int32_t *b)       /* FUN_0041ed20 */
{
    int32_t x = (b[0] - a[0]) >> 16, y = (b[1] - a[1]) >> 16;
    return isqrt_fixed(y * y + x * x) << 16;
}

int32_t dist3d(const int32_t *a, const int32_t *b)       /* FUN_0041ece0 */
{
    int32_t x = (b[0] - a[0]) >> 16, y = (b[1] - a[1]) >> 16, z = (b[2] - a[2]) >> 16;
    return isqrt_fixed(z * z + x * x + y * y) << 16;
}

/* FUN_00418910: water state of the four corners of box (x0, y0, x1, y1) around pos, preferring `want`. */
int water_state_box(const int32_t *pos, const int32_t *box, int want)
{
    int32_t p[3] = { box[0] + pos[0], box[1] + pos[1], pos[2] };
    int a = water_state_at(p);
    if (a == want) return a;
    for (int k = 0; k < 2; k++) {
        if (k == 0) p[0] = box[2] + pos[0]; else p[1] = box[3] + pos[1];
        int b = water_state_at(p);
        if (b != a) {
            if (b != want) {
                if (want == 1 && b != a) b = 1;
                else { bool one = b == 1; b = a; if (one) b = 1; }
            }
            a = b;
            if (b == want) return b;
        }
    }
    p[0] = box[0] + pos[0];
    int b = water_state_at(p);
    if (b != a) {
        if (b == want) return b;
        if (want == 1) return 1;
        if (b == 1) a = 1;
    }
    return a;
}

/* Hit kind when a projectile / grenade reaches z < 0 (shared tail of 0x430dd0 / 0x4313e0 / 0x431a10). */
static int ground_hit_kind(Obj *o, const ProjType *t)
{
    int w = water_state(o);
    if (w != 0) {
        if (w != 1) return HIT_WATER;
        int32_t r = (o->cls->id == 0 && t) ? (int32_t)t->splash << 16 : 0xc;
        if (r == 0) return HIT_WATER;
        int32_t box[4] = { -r, -r, r, r };
        if (water_state_box(o->pos, box, 0) != 0) return HIT_WATER;
    }
    uint32_t tr = CELL_TERRAIN(*o->cell);
    return (tr > 0x48 && tr < 0x54) ? HIT_ROAD : HIT_GROUND;
}

/* v = (0, -speed, 0) * (pitch ? P * Y : Y); the pitch matrix is FUN_00408400 when `rotx`. */
static void launch_vel(int32_t *v, int32_t speed, uint32_t heading, int32_t pitch, bool rotx)
{
    v[0] = 0; v[1] = -speed; v[2] = 0;
    const int32_t *M = rot_mat[((int32_t)heading >> 16) & 63];
    int32_t m[9], r[9];
    if (pitch) {
        if (rotx) { mat_rot_x(r, pitch); mat_mul3(m, r, M); }
        else mat_mul3(m, pitch_mat[(pitch >> 16) & 63], M);
        M = m;
    }
    vec_mul_mat3(v, v, M);
}

/* ------------------------------------------------------------------ Missle / Death Missle */
typedef struct { Obj *owner; const ProjType *type; uint32_t heading; int32_t pitch; } ProjArgs;   /* FireProjectile local_20.. */

static int proj_init(const ObjClass *c, Obj *o, void *arg)           /* ProjectileInit 0x430bb0 */
{
    const ProjArgs *a = arg;
    const ProjType *t = a->type;
    (void)c;
    /* (DAT_00459680: a debug recorder of the first shell's height every 8 ticks; never read) */
    o->gfx = t->gfx;
    o->p5c = t;
    o->speed = t->speed;
    o->heading = a->heading;
    o->i60 = a->pitch;
    o->b70[0] = t->life; o->b70[1] = 0; o->b70[2] = t->period; o->b70[3] = 0;
    o->u58 = 0;
    o->think = NULL;                                  /* type +0x10 is 0 for every type */
    if (a->pitch && t->pitch_rate) o->flags |= OF_NEW;   /* 0x2000000: ballistic */
    int32_t v[3];
    launch_vel(v, t->speed, o->heading, a->pitch, (t->flags & 2) != 0);
    o->u64 = v[0]; o->u68 = v[1]; o->u6c = v[2];
    if (a->owner) obj_set_parent(o, a->owner);
    if (t->snd) snd(1, t->snd, o);
    if (t->shadow) spawn_shadow(o, t->shadow);
    return 1;
}

static int proj_destroy(const ObjClass *c, Obj *o)                   /* ProjectileDestroy 0x430d70 */
{
    (void)c;
    const ProjType *t = o->p5c;
    if ((o->flags & OF_KILLED_BY_STORAGE) && t->impact) {   /* 0x200 = impact */
        if (o->pos[2] < 0) o->pos[2] = 0;
        int k = o->b70[3];                                  /* FUN_00431100 */
        spawn_explosion(o->team, o->pos[0], o->pos[1], o->pos[2], t->expl[k], NULL);
    }
    return 1;
}

static void proj_anim(Obj *o, const ProjType *t)
{
    if ((int8_t)t->period <= 0) return;
    /* the countdown +0x72 is read but never stored back: frames only advance when dt >= period */
    for (int32_t n = (int8_t)o->b70[2] - g_dt; n < 1; n += (int8_t)t->period) {
        int8_t f = (int8_t)(o->b70[1] + 1);
        o->b70[1] = (uint8_t)f;
        if ((int8_t)t->nframes <= f) o->b70[1] = 0;
    }
}

static void proj_update(const ObjClass *c, Obj *o)                   /* ProjectileUpdate 0x430dd0 */
{
    (void)c;
    const ProjType *t = o->p5c;
    bool ballistic = false;
    if (o->b70[0] <= (uint8_t)g_dt) { obj_destroy_now(o); return; }
    o->b70[0] = (uint8_t)(o->b70[0] - (uint8_t)g_dt);
    if (o->flags & OF_NEW) {
        int32_t v[3];
        if (!(t->flags & 2)) {
            int32_t old = o->i60;
            turn_towards((uint32_t *)&o->i60, 0, t->pitch_rate);
            if (old != 0) {
                launch_vel(v, o->speed, o->heading, o->i60, false);
                o->u64 = v[0]; o->u68 = v[1]; o->u6c = v[2];
            }
        } else {
            int32_t d = t->pitch_rate * g_dt;
            if (d != 0) {
                o->i60 = (int32_t)((uint32_t)(o->i60 + d) & ANG_MASK);
                launch_vel(v, o->speed, o->heading, o->i60, false);
                o->u64 = v[0]; o->u68 = v[1]; o->u6c = v[2];
            }
        }
        ballistic = true;
    }
    proj_anim(o, t);
    int32_t d[3] = { o->u64 * g_dt, o->u68 * g_dt, o->u6c * g_dt };
    if (d[2] + o->pos[2] > 0x370000) {                 /* ceiling 55 px */
        d[2] = 0x370000 - o->pos[2];
        if (!ballistic) turn_towards((uint32_t *)&o->i60, 0, t->pitch_rate);
    }
    obj_move(o, d);
    if (o->pos[2] >= 0) return;
    obj_mark_delete(o);
    o->flags |= OF_KILLED_BY_STORAGE;
    o->pos[2] = 0;
    o->b70[3] = (uint8_t)ground_hit_kind(o, t);
}

static uint32_t proj_hit_static(Obj *o, uint32_t *cell, int bno)     /* 0x4310b0 */
{
    const ProjType *t = o->p5c;
    bno_damage(t->damage, o, cell, &bno_defs[bno]);
    obj_mark_delete(o);
    o->flags |= OF_KILLED_BY_STORAGE;
    o->b70[3] = HIT_STATIC;
    return 2;
}

static uint32_t proj_hit_object(Obj *o, Obj *other)                  /* 0x431140 */
{
    const ProjType *t = o->p5c;
    uint32_t r = 0;
    if (o->parent == other) return 0;                  /* (type +0x14 filter is 0 for every type) */
    if (other->cls->damage) r = other->cls->damage(other, o, t->damage) ? 4 : 0;
    obj_mark_delete(o);
    o->flags |= OF_KILLED_BY_STORAGE;
    o->b70[3] = HIT_OBJECT;
    return r | 2;
}

static void dmissile_update(const ObjClass *c, Obj *o)               /* DeathMissileUpdate 0x4313e0 */
{
    (void)c;
    const ProjType *t = o->p5c;
    Obj *tg = o->parent;
    if (!tg) {
        obj_mark_delete(o);
        o->flags |= OF_KILLED_BY_STORAGE;
        o->b70[3] = HIT_OBJECT;
        return;
    }
    turn_towards(&o->heading, angle_between(o->pos, tg->pos), tun_dmissile_turn);
    uint32_t a = (uint32_t)(atan2_fixed(dist2d(o->pos, tg->pos), o->pos[2] - tg->pos[2]) >> 2), v = a;
    if ((int32_t)a < 0x355556 && (int32_t)a > 0xaaaaa) { v = 0x355556; if ((int32_t)a < 0x200001) v = 0xaaaaa; }
    int turning = !turn_towards((uint32_t *)&o->i60, v, tun_dmissile_pitch);
    int32_t w[3];
    launch_vel(w, o->speed, o->heading, o->i60, false);
    o->u64 = approach(o->u64, w[0], g_dt * 0x7ae);
    o->u68 = approach(o->u68, w[1], g_dt * 0x7ae);
    o->u6c = approach(o->u6c, w[2], g_dt * 0x1999);
    proj_anim(o, t);
    if (turning || rand_range(4) == 1) {                /* smoke trail */
        int32_t z = o->pos[2] + 0x50000;
        int32_t y = rand_range(4) * 0x10000 + o->pos[1];
        int32_t x = rand_range(4) * 0x10000 + o->pos[0];
        Obj *e = spawn_explosion(0, x, y, z, 0x454650, NULL);
        if (e) e->u68 = rand_range(0x1555) + 0x2aaa;
    }
    int32_t d[3] = { o->u64 * g_dt, o->u68 * g_dt, o->u6c * g_dt };
    obj_move(o, d);
    if (o->pos[2] >= 0) return;
    obj_mark_delete(o);
    o->flags |= OF_KILLED_BY_STORAGE;
    o->pos[2] = 0;
    o->b70[3] = (uint8_t)ground_hit_kind(o, t);
}

static uint32_t dmissile_hit_object(Obj *o, Obj *other)              /* 0x431700 */
{
    const ProjType *t = o->p5c;
    uint32_t r = 0;
    if (other->cls->id == 15) return 0;                 /* not the SUB that fired it */
    if (other->cls->damage) r = other->cls->damage(other, o, t->damage) ? 4 : 0;
    obj_mark_delete(o);
    o->flags |= OF_KILLED_BY_STORAGE;
    o->b70[3] = HIT_OBJECT;
    return r | 2;
}

const ObjClass class_missle = {
    0, OBJ_CLASS_NAME, proj_init, proj_destroy, proj_update, &gfx_4495c0, NULL, NULL,
    NULL, NULL, NULL, NULL, 0xc8, proj_hit_static, proj_hit_object, NULL, (void *)&gfx_449628, 0
};
const ObjClass class_death_missle = {
    16, OBJ_CLASS_NAME, proj_init, proj_destroy, dmissile_update, &gfx_4495c0, NULL, NULL,
    NULL, NULL, NULL, NULL, 0xc8, proj_hit_static, dmissile_hit_object, NULL, (void *)&gfx_449628, 0
};

/* ------------------------------------------------------------------ TRACER (line-of-fire probe) */
static int tracer_init(const ObjClass *c, Obj *o, void *arg)         /* TracerInit 0x4311d0 */
{
    const ProjArgs *a = arg;
    const ProjType *t = a->type;
    (void)c;
    tracer_result = 0;
    o->gfx = t->gfx;
    o->heading = a->heading;
    o->i60 = a->pitch;
    tracer_owner = a->owner;
    int32_t v[3];
    launch_vel(v, 0x200000, o->heading, o->i60, (t->flags & 2) != 0);   /* one cell per step */
    o->u64 = v[0]; o->u68 = v[1]; o->u6c = v[2];
    obj_link_cell(o, NULL);
    int n = (int)(((uint32_t)t->life * (uint32_t)t->speed) >> 21);
    do {
        if (n < 1) break;
        int32_t d[3] = { o->u64, o->u68, o->u6c };
        if (obj_move(o, d) || o->pos[2] < 0) break;
        n--;
    } while (tracer_result == 0);
    if (tracer_result < 0) tracer_result = 0;
    obj_unlink_cell(o);
    return 0;
}

static uint32_t tracer_hit_static(Obj *o, uint32_t *cell, int bno)   /* 0x4313a0 */
{
    (void)bno;
    if ((int)CELL_TEAM(*cell) == o->team) { tracer_result = 1; return 0; }
    if (tracer_result == 0) tracer_result = -1;
    return 0;
}

static uint32_t tracer_hit_object(Obj *o, Obj *other)                /* 0x431340 */
{
    if (other == tracer_owner) return 0;
    if (G.cur_vehicle[o->team & 1] == other) { tracer_result = 1; return 0; }   /* view[team] +0x6c */
    if (tracer_result == 0) tracer_result = -1;
    return 0;
}

const ObjClass class_tracer = {
    7, OBJ_CLASS_NAME, tracer_init, NULL, proj_update, &gfx_4495c0, NULL, NULL,
    NULL, NULL, NULL, NULL, 0x100, tracer_hit_static, tracer_hit_object, NULL, (void *)&gfx_449628, 0
};

/* ------------------------------------------------------------------ FireProjectile */
Obj *fire_projectile(const int32_t *pos, const int32_t *off, uint32_t heading, int32_t pitch, int type,
                     int team, Obj *owner)                           /* FireProjectile 0x431760 */
{
    if (type > 0xb) type = 0;
    const ProjType *t = &proj_types[type];
    ProjArgs a = { owner, t, heading, pitch };
    int32_t d[3];
    const int32_t *Y = rot_mat[((int32_t)heading >> 16) & 63];
    if (!(t->flags & 1)) vec_mul_mat3(d, off, Y);
    else {
        d[0] = off[0]; d[1] = off[1]; d[2] = 0;
        if (pitch == 0) vec_mul_mat3(d, d, Y);
        else {
            int32_t m[9];
            mat_mul3(m, pitch_mat[(pitch >> 16) & 63], Y);
            vec_mul_mat3(d, d, m);
        }
        d[2] += off[2];
    }
    return obj_create(t->cls, team, pos[0] + d[0], pos[1] + d[1], pos[2] + d[2], &a);
}

void proj_add_speed(Obj *p, int32_t ds)                              /* FUN_00431880 */
{
    if (!p) return;
    const ProjType *t = p->p5c;
    p->speed += ds;
    int32_t v[3];
    launch_vel(v, p->speed, p->heading, p->i60, (t->flags & 2) != 0);
    p->u64 = v[0]; p->u68 = v[1]; p->u6c = v[2];
}

/* ------------------------------------------------------------------ Grenade */
static int grenade_init(const ObjClass *c, Obj *o, void *a) { (void)c; (void)o; (void)a; return 1; }   /* 0x4319c0 */

static int grenade_destroy(const ObjClass *c, Obj *o)                /* GrenadeDestroy 0x4319d0 */
{
    (void)c;
    if (o->u58 >= 0 && o->u58 < 6) spawn_explosion(o->team, o->pos[0], o->pos[1], o->pos[2], expl_tab_450a60[o->u58], NULL);
    return 1;
}

/* GrenadeUpdate 0x431a10: closed-form ballistic arc from the throw point (+0x5c..+0x64) with tick
   count +0x68, direction +0x6c; +0x70 = tumble frame (12 frames, 0x2aaa per tick). */
static void grenade_update(const ObjClass *c, Obj *o)
{
    (void)c;
    int32_t t = o->u68 + g_dt;
    o->u68 = t;
    int32_t d[3];
    d[2] = (int32_t)(((uint32_t)fix_mul(o->speed, 0xec83) + (uint32_t)(t * -0x666)) * (uint32_t)t) - o->pos[2] + o->u64;
    int32_t r = fix_mul(o->speed, o->u68 * 0x61f7);
    int k = (o->u6c >> 16) & 63;
    d[0] = (fix_mul(dir_vec[k][0], r) - o->pos[0]) + (int32_t)o->v5c;
    d[1] = (fix_mul(dir_vec[k][1], r) - o->pos[1]) + o->i60;
    o->heading = (uint32_t)(g_dt * 0x8888 + (int32_t)o->heading) & ANG_MASK;
    uint32_t f = (uint32_t)(g_dt * 0x2aaa + o->u70);
    o->u70 = (int32_t)f;
    if ((int32_t)f > 0xbffff) o->u70 = (int32_t)(f % 0xc0000);
    obj_move(o, d);
    if (o->pos[2] >= 0) return;
    obj_mark_delete(o);
    o->u58 = ground_hit_kind(o, NULL);
}

static uint32_t grenade_hit_static(Obj *o, uint32_t *cell, int bno)  /* 0x431bd0 */
{
    bno_damage(0x18000, o, cell, &bno_defs[bno]);
    obj_mark_delete(o);
    o->flags |= OF_KILLED_BY_STORAGE;
    o->u58 = HIT_STATIC;
    return 2;
}

static uint32_t grenade_hit_object(Obj *o, Obj *other)               /* 0x431c20 */
{
    uint32_t r = 0;
    if (o->parent == other) return 0;
    if (other->cls->damage) r = other->cls->damage(other, o, 0x18000) ? 4 : 0;
    obj_mark_delete(o);
    o->flags |= OF_KILLED_BY_STORAGE;
    o->u58 = HIT_OBJECT;
    return r | 2;
}

const ObjClass class_grenade = {
    18, OBJ_CLASS_NAME, grenade_init, grenade_destroy, grenade_update, &gfx_449aa0, NULL, NULL,
    NULL, NULL, NULL, NULL, 0xc8, grenade_hit_static, grenade_hit_object, NULL, (void *)&gfx_449ae8, 0
};

Obj *throw_grenade(Obj *o, const int32_t *off, const int32_t *target)   /* ThrowGrenade 0x431c80 */
{
    int team = o->team;
    int32_t p[3] = { o->pos[0], o->pos[1], o->pos[2] };
    int32_t tg[3] = { target[0], target[1], target[2] };
    if (off) { p[0] += off[0]; p[1] += off[1]; p[2] += off[2]; }
    Obj *g = obj_create(&class_grenade, team, p[0], p[1], p[2], NULL);
    if (!g) return NULL;
    int32_t d = dist2d(p, tg) >> 16;
    g->speed = fix_sqrt(fix_div(d * 0xccc, 0xb504));     /* throw speed from the distance */
    g->heading = 0;
    g->u58 = -1;
    g->v5c = (uint32_t)p[0]; g->i60 = p[1]; g->u64 = p[2];
    g->u68 = 0;
    g->u6c = (int32_t)(((uint32_t)(atan2_fixed(p[0] - tg[0], p[1] - tg[1]) >> 2) - 0x100000) & ANG_MASK);
    g->u70 = 0;
    obj_set_parent(g, o);
    snd(1, snd_grenade_tab[rand_range(3)], g);
    spawn_shadow(g, (const Gfx *)class_grenade._40);
    return g;
}

Obj *jeep_throw_grenade(Obj *o, const int32_t *off, const VehicleDef *def, VehState *s)   /* FUN_00431de0 */
{
    (void)def;
    int32_t best = 0x7fffffff, tp[3];
    int team = o->team;
    const int32_t *tg = NULL;
    Obj *e = G.cur_vehicle[team ^ 1];
    if (e && e->pos[2] < 0x50000 && dist2d(o->pos, e->pos) < 0x3d3ab7) tg = e->pos;   /* enemy on the ground */
    else {
        uint32_t *c = s->hit_cell;
        if (c && (*c & 0xe000000)) {                      /* the damageable BNO last bumped into */
            cell_to_pos(c, tp);
            bno_adjust_pos(&bno_defs[CELL_BNO(*c)], tp);
            best = dist2d(o->pos, tp);
            if (best < 0x3d3ab7) tg = tp;
        }
        Obj *h = s->hit_obj;
        if (h && s->hit_obj_id == h->id && h->team != team) {
            int32_t dd = dist2d(o->pos, h->pos);
            if (dd < 0x3d3ab7 && dd <= best) tg = h->pos;
        }
        if (!tg) {                                        /* 48..62 px ahead, +-4 heading steps */
            tp[1] = o->pos[1]; tp[0] = o->pos[0]; tp[2] = o->pos[2];
            int32_t r = (rand_range(0xf) + 0x30) * 0x10000;
            int k = (rand_range(8) + ((int32_t)o->heading >> 16) - 4) & 0x3f;
            tp[0] += fix_mul(dir_vec[k][0], r);
            tp[1] += fix_mul(dir_vec[k][1], r);
            tg = tp;
        }
    }
    return throw_grenade(o, off, tg);
}

/* ------------------------------------------------------------------ Mine */
static int mine_init(const ObjClass *c, Obj *o, void *a)             /* MineInit 0x434e40 */
{
    (void)c; (void)a;
    o->team = 0;
    o->i60 = 0x50000;
    o->v5c = 0;
    snd(1, 0x43e9c0, o);
    return 1;
}

static void mine_update(const ObjClass *c, Obj *o)                   /* MineUpdate 0x434e70: blink, then arm */
{
    (void)c;
    int32_t t = g_dt * 0x2222 + o->i60;
    o->i60 = t;
    if (t > 0x19ffff) {
        o->gfx = &gfx_449478;
        obj_set_inactive(o);
        o->team = 1;
        return;
    }
    uint32_t n = o->v5c + (uint32_t)g_dt;
    o->v5c = n;
    if ((int32_t)n > 0x1d) o->v5c = n % 0x1e;
    int st = (int32_t)o->v5c < (t >> 16) ? 2 : 0;
    if (o->team != st) {
        if (st == 2) snd(1, 0x43e9c0, o);
        o->team = st;
    }
}

static int mine_destroy(const ObjClass *c, Obj *o)                   /* MineDestroy 0x434f20 */
{
    (void)c;
    if (o->flags & OF_KILLED_BY_STORAGE)
        spawn_explosion(o->team, o->pos[0], o->pos[1], o->pos[2], 0x454470, NULL);
    uint32_t *cell = o->cell;
    obj_unlink_cell(o);
    if (cell && cell != &G.outside) {                     /* FUN_00422e40: radar mine bit */
        bool any = false;
        for (Obj *p = obj_from_slot(CELL_HEAD(*cell)); p; p = p->cnext) if (p->cls->id == 10) any = true;
        if (any) *cell |= 0x80000000u; else *cell &= 0x7fffffffu;
    }
    return 1;
}

static uint32_t mine_hit_object(Obj *o, Obj *other)                  /* 0x434f70: vehicles set it off */
{
    if (other->cls->id == 1) { o->flags |= OF_KILLED_BY_STORAGE; obj_mark_delete(o); return 2; }
    return 0;
}

static int mine_damage(Obj *o, Obj *src, int32_t amount)             /* 0x434fa0 */
{
    (void)src;
    if (amount > 0x18000) { o->flags |= OF_KILLED_BY_STORAGE; obj_mark_delete(o); return 2; }
    return 0;
}

const ObjClass class_mine = {
    10, OBJ_CLASS_NAME, mine_init, mine_destroy, mine_update, &gfx_4493b0, NULL, NULL,
    NULL, NULL, NULL, NULL, 0x96, NULL, mine_hit_object, mine_damage, NULL, 0
};

Obj *spawn_mine(int32_t x, int32_t y)                                /* SpawnMine 0x434fd0 */
{
    int32_t p[3] = { x, y, 0 };
    if (water_state_at(p) == 2) return NULL;
    Obj *m = obj_create(&class_mine, 0, x, y, 0, NULL);
    if (m && m->cell) *m->cell |= 0x80000000u;             /* + RadarUpdateCell */
    return m;
}
