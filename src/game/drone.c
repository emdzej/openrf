/* The Drone (class 9, descriptor 0x455778): sent against a vehicle that stayed in one map cell for more
   than 360 ticks (VehicleUpdate idle rule, fewer than 3 enemy turrets in view). SpawnDrone 0x435950 puts
   it just outside the target's view box (FUN_00435c30) 50 px up; it flies with inertia (0x435620), banks
   and pitches with its velocity, turns towards the target and fires its twin guns (projectile type 11,
   TRACER line-of-fire probe first when the target is airborne) (0x435420). Without a target it flies on
   and vanishes once it has not been drawn for 60 ticks (0x435840). Records: 5 per team at 0x4597b8
   (FUN_00435040, count = VHCL pool or the level table 0x443880). One looping "Drone" hum (0x43e9a8)
   for all drones, positioned per listener at the nearest drone (SFXF_OWN_GAINS). See docs/game.md §9. */
#include "ai.h"
#include "weapon.h"
#include "effect.h"
#include "../exe.h"
#include <stddef.h>
#include <string.h>

extern Gfx gfx_455680, gfx_455730;

AiHooks ai_hooks;
int32_t drone_count[2];                       /* DAT_00459930 */
int32_t drone_total;                          /* DAT_0045993c */
static DroneRec drone_pool[2][5];             /* 0x4597b8, 0x20 bytes each */
static DroneRec *drone_free[2];               /* DAT_00459928 */
static DroneRec *rec_of[OBJ_POOL];            /* obj +0x6c */
static void *snd_inst;                        /* DAT_00459910 */
static uint32_t snd_handle;                   /* DAT_004597b0 */
static int32_t snd_pos[2][3];                 /* DAT_004598f8 / DAT_00459904: hum source per listener */
static int32_t snd_dist[2];                   /* DAT_00459918 / DAT_0045991c */
static int32_t enemy_dist[2];                 /* DAT_00455098 / DAT_0045509c: nearest enemy drone per side */
static int32_t enemy_pos[2][3];               /* DAT_0045af70 / DAT_0045af7c */
static int32_t near_tick = -1;                /* DAT_00459938 */
static Obj *dying;                            /* DAT_00459920 */

/* Parameters 0x455058 (obj +0x70), read from RFIRE.BIN: [0] velocity approach per tick, [1] turn rate with
   the target within +-90 deg, [2] turn rate behind (close), [3] behind (far), [4] "far" distance, [5] bank
   gain, [6] bank rate, [7] pitch gain, [8] pitch rate, [9] acceleration, [10] gun pitch rate, [11] gun pitch
   limit, [12] reload ticks, [13] min range, [14] max range. */
static int32_t P[15];
static int32_t muzzle[2][3];                  /* 0x4550a0 */
static void drone_tables_load(void)
{
    for (int i = 0; i < 15; i++) P[i] = exe_s32(0x455058 + 4 * (uint32_t)i);
    for (int i = 0; i < 6; i++) muzzle[i / 3][i % 3] = exe_s32(0x4550a0 + 4 * (uint32_t)i);
}
EXE_LOADER(drone_tables_load)
static const int32_t zero3[3];                /* 0x48b0e0 */

DroneRec *drone_rec(const Obj *o) { return rec_of[o->id & 0x1ff]; }

static void snd_stop_if_last(void)
{
    if (drone_total == 0 && snd_inst) {
        if (ai_hooks.snd_kill) ai_hooks.snd_kill(snd_inst, snd_handle);   /* Snd_QueueCommand(9, ...) */
        snd_inst = NULL;
    }
}

static DroneRec *rec_take(int team)
{
    DroneRec *r = drone_free[team];
    if (!r) return NULL;
    drone_free[team] = r->next;
    r->next = NULL;
    r->bumped = NULL;
    drone_total++;
    drone_count[team]++;
    return r;
}

static void rec_release(DroneRec *r, int team)
{
    int t = drone_total;
    if (--drone_total, t < 1) drone_total = 0;
    if (--drone_count[team] < 1) enemy_dist[team == 0] = 0x7fffffff;
    snd_stop_if_last();
    r->next = drone_free[(int)r->team];
    r->obj = NULL;
    r->exploded = 0;
    drone_free[(int)r->team] = r;
}

void drone_pool_init(int n)                   /* FUN_00435040 */
{
    if (n > 5) n = 5;
    memset(drone_pool, 0, sizeof drone_pool);
    for (int t = 0; t < 2; t++) {
        drone_free[t] = NULL;
        for (int i = 0; i < n; i++) {
            DroneRec *r = &drone_pool[t][i];
            r->next = drone_free[t];
            r->team = (int8_t)t;
            drone_free[t] = r;
        }
    }
    drone_count[0] = drone_count[1] = drone_total = 0;
    enemy_dist[0] = enemy_dist[1] = snd_dist[0] = snd_dist[1] = 0x7fffffff;
    near_tick = -1;
    snd_inst = NULL;
    memset(rec_of, 0, sizeof rec_of);
}

/* FUN_00435620: inertial flight at 50 px, bank / pitch from the sideslip, push-apart from a drone
   that touched this one. */
static void drone_move(Obj *o, DroneRec *r)
{
    int h = (int32_t)o->heading >> 16;
    int32_t step = P[0] * g_dt;
    DRONE_VX(o) = approach(DRONE_VX(o), fix_mul(o->speed, dir_vec[h][0]), step);
    DRONE_VY(o) = approach(DRONE_VY(o), fix_mul(o->speed, dir_vec[h][1]), step);
    int32_t d[3] = { DRONE_VX(o) * g_dt, DRONE_VY(o) * g_dt, 0 };
    if (o->pos[2] != 0x320000) {
        int32_t lim = g_dt * 0x4ccc;
        d[2] = 0x320000 - o->pos[2];
        if (d[2] < 1) { if (d[2] < -lim) d[2] = -lim; }
        else if (lim < d[2]) d[2] = lim;
    }
    if (obj_move(o, d) == 1 && d[2] != 0) { d[2] = 0; obj_move(o, d); }
    if (o->speed < 0x18000) o->speed += P[9] * g_dt;
    if (o->speed > 0x18000) o->speed = 0x18000;
    int32_t vel[2] = { DRONE_VX(o), DRONE_VY(o) };
    uint32_t k = (((uint32_t)o->heading - angle_between(zero3, vel)) & ANG_MASK) >> 16;
    int32_t vx = DRONE_VX(o) >> 8, vy = DRONE_VY(o) >> 8;
    int32_t v = isqrt_fixed(vy * vy + vx * vx) << 8;
    turn_towards((uint32_t *)&DRONE_ROLL(o), (uint32_t)fix_mul(v, (P[5] >> 16) * dir_vec[k][0]), P[6]);
    turn_towards((uint32_t *)&DRONE_PITCH(o), (uint32_t)fix_mul(v, (P[7] >> 16) * dir_vec[k][1]), P[8]);
    Obj *b = r->bumped;
    if (b) {
        r->bumped = NULL;
        int32_t push = 0x20000 - (dist2d(o->pos, b->pos) >> 4);
        if (push > 0) {
            int a = (int32_t)angle_between(b->pos, o->pos) >> 16;
            DRONE_VX(o) += fix_mul(dir_vec[a][0], push);
            DRONE_VY(o) += fix_mul(dir_vec[a][1], push);
        }
    }
}

static int think_leave(Obj *o)                /* FUN_00435840 */
{
    DroneRec *r = drone_rec(o);
    drone_move(o, r);
    if (r->drawn + 60 < g_tick) { o->think = NULL; return 1; }
    return 0;
}

static int think_attack(Obj *o)               /* FUN_00435420 (class default think) */
{
    Obj *tg = o->parent;
    if (!tg) { o->think = think_leave; return 1; }
    DroneRec *r = drone_rec(o);
    drone_move(o, r);
    uint32_t yaw = angle_between(o->pos, tg->pos);
    uint32_t dh = (yaw - o->heading) & ANG_MASK;
    int32_t d = dist2d(o->pos, tg->pos);
    int32_t rate = (dh < 0x100001 || dh > 0x2fffff) ? P[1] : (P[4] < d ? P[3] : P[2]);
    turn_towards(&o->heading, yaw, rate);
    int32_t pitch = atan2_fixed(d, o->pos[2] - tg->pos[2]) >> 2;
    uint32_t dp = ((uint32_t)pitch - (uint32_t)DRONE_PITCH(o)) & ANG_MASK;
    bool ok;
    if (dp < 0x200001) {
        ok = true;
        if (P[11] < (int32_t)dp) { ok = false; dp = (uint32_t)P[11]; }
    } else if ((int32_t)dp < 0x40 - P[11]) {  /* sic: 0x40, not 0x400000 -> never clamps downwards */
        ok = false;
        dp = (uint32_t)(0x40 - P[11]);
    } else ok = true;
    if (turn_towards(&r->gun_pitch, dp, P[10]) && ok && r->next_fire < g_tick && P[13] < d && d < P[14] &&
        (((o->heading - yaw) + 0xe38e) & ANG_MASK) < 0x1c71c) {          /* within +-5 deg */
        const int32_t *m = muzzle[(int8_t)r->muzzle & 1];
        if (tg->pos[2] > 0x20000) {                                       /* airborne target: probe first */
            fire_projectile(o->pos, m, yaw, pitch, 3, o->team, o);
            if (tracer_result != 0) { r->next_fire = P[12] + g_tick; return 0; }
        }
        if (fire_projectile(o->pos, m, yaw, pitch, 11, o->team, o)) {
            r->muzzle ^= 1;
            r->next_fire = P[12] + g_tick;
        }
    }
    return 0;
}

static int drone_init(const ObjClass *c, Obj *o, void *arg)              /* DroneInit 0x435140 */
{
    (void)c;
    DroneRec *r = arg;
    if (!r) r = rec_take(o->team & 1);
    if (!r) return 0;
    r->obj = o;
    o->speed = 0;
    DRONE_ROLL(o) = 0;
    DRONE_PITCH(o) = 0;
    rec_of[o->id & 0x1ff] = r;
    DRONE_VX(o) = 0;
    DRONE_VY(o) = 0;
    o->u64 = 0;
    return 1;
}

static void debris_add_vel(Obj *piece, int n)                            /* FUN_004352c0 */
{
    (void)n;
    DebrisExt *x = &debris_ext[piece->id & 0x1ff];
    x->vel[0] += DRONE_VX(dying);
    x->vel[1] += DRONE_VY(dying);
}

static int drone_destroy(const ObjClass *c, Obj *o)                      /* DroneDestroy 0x4351b0 */
{
    (void)c;
    DroneRec *r = drone_rec(o);
    if (r && r->exploded) {
        spawn_explosion(o->team, o->pos[0], o->pos[1], o->pos[2], 0x454098, NULL);
        dying = o;
        spawn_debris_burst(o->pos, o->team, debris_sets[1], model_part_by_addr(o->gfx->addr), o->heading,
                           DRONE_PITCH(o), debris_add_vel);                /* set 0x454e28 */
    }
    if (r && r->next == NULL) rec_release(r, o->team & 1);
    rec_of[o->id & 0x1ff] = NULL;
    return 1;
}

static void drone_update(const ObjClass *c, Obj *o)                      /* DroneUpdate 0x4352f0 */
{
    (void)c;
    do {
        if (!o->think) { obj_destroy_now(o); return; }
    } while (o->think(o) != 0);
    if (near_tick != g_tick) {
        near_tick = g_tick;
        enemy_dist[0] = enemy_dist[1] = snd_dist[0] = snd_dist[1] = 0x7fffffff;
    }
    const int32_t *l0 = ai_hooks.listener[0] ? ai_hooks.listener[0] : zero3;
    int32_t d = dist_sq_px(o->pos, l0);
    if (d < snd_dist[0]) {
        memcpy(snd_pos[0], o->pos, sizeof snd_pos[0]);
        snd_dist[0] = d;
    }
    if (o->team != 0 && d < enemy_dist[0]) {
        memcpy(enemy_pos[0], o->pos, sizeof enemy_pos[0]);
        enemy_dist[0] = d;
    }
    if (G.nplayers > 1) {
        const int32_t *l1 = ai_hooks.listener[1] ? ai_hooks.listener[1] : zero3;
        d = dist_sq_px(o->pos, l1);
        if (d < snd_dist[0]) {                /* sic: compared with listener 1's distance */
            memcpy(snd_pos[1], o->pos, sizeof snd_pos[1]);
            snd_dist[1] = d;
        }
        if (o->team != 1 && d < enemy_dist[1]) {
            memcpy(enemy_pos[1], o->pos, sizeof enemy_pos[1]);
            enemy_dist[1] = d;
        }
    }
}

static int drone_damage(Obj *o, Obj *src, int32_t amount)               /* 0x435880 */
{
    if (amount < 0x10000) return 0;
    if (src->cls->id == 0) {                                             /* a shell knocks it along */
        int h = (int32_t)src->heading >> 16;
        DRONE_VX(o) += fix_mul(dir_vec[h][0], src->speed) >> 1;
        DRONE_VY(o) += fix_mul(dir_vec[h][1], src->speed) >> 1;
    }
    DroneRec *r = drone_rec(o);
    if (r) { r->exploded = 1; r->kill_heading = src->heading; }
    obj_mark_delete(o);
    return 2;
}

static uint32_t drone_hit_object(Obj *o, Obj *other)                    /* 0x435900 */
{
    if (o->cls->id == 9 && drone_rec(o)) drone_rec(o)->bumped = other;
    if (other->cls->id == 9 && drone_rec(other)) drone_rec(other)->bumped = o;
    /* the outer 64x64 px proximity part 0x455600 only records the contact */
    if ((!g_hit_part_a || g_hit_part_a->addr != 0x455600) && (!g_hit_part_b || g_hit_part_b->addr != 0x455600)) return 5;
    return 0;
}

const ObjClass class_drone = {
    9, OBJ_CLASS_NAME, drone_init, drone_destroy, drone_update, &gfx_455680, NULL, think_attack,
    NULL, NULL, NULL, NULL, 0x6e, NULL, drone_hit_object, drone_damage, (void *)&gfx_455730, 0
};

/* FUN_00435c30: a point on side `side` (0 N, 1 E, 2 S, else W) of `box` around the target, rejected
   when it lies inside another team vehicle's box. */
static int drone_spawn_point(int side, int32_t *p, Obj *tg, const int32_t *box)
{
    p[0] = tg->pos[0];
    p[1] = tg->pos[1];
    int32_t w = box[2] - box[0] + 1, hgt = box[3] - box[1] + 1;
    if (side == 0) { p[0] += rand_range(w >> 16) * 0x10000 - (w >> 1); p[1] += box[1]; }
    else if (side == 1) { p[0] += box[2]; p[1] += rand_range(hgt >> 16) * 0x10000 - (hgt >> 1); }
    else if (side == 2) { p[0] += rand_range(w >> 16) * 0x10000 - (w >> 1); p[1] += box[3]; }
    else { p[0] += box[0]; p[1] += rand_range(hgt >> 16) * 0x10000 - (hgt >> 1); }
    for (int t = 0; t < 2; t++) {
        Obj *v = get_team_vehicle(t);
        if (!v || v == tg) continue;
        int32_t b[4] = { -0x2000000, -0x2000000, 0x2000000, 0x2000000 };
        if (v->cls->id == 1) {
            const VehicleDef *d = ((const VehState *)v->p60)->def;
            b[0] = d->hud9[3] * -0x100000; b[1] = d->hud9[4] * -0x100000;
            b[2] = d->hud9[3] * 0x100000;  b[3] = d->hud9[4] * 0x100000;
        }
        if (point_in_box(p, v->pos, b)) return 0;
    }
    return 1;
}

Obj *spawn_drone(Obj *tg)                                                 /* SpawnDrone 0x435950 */
{
    int side = rand_range(4);
    int32_t box[4] = { -0x2000000, -0x2000000, 0x2000000, 0x2000000 };
    if (tg->cls->id == 1) {                  /* def [0x9f]/[0xa0]: the view box in half cells */
        const VehicleDef *d = ((const VehState *)tg->p60)->def;
        box[0] = d->hud9[3] * -0x100000; box[1] = d->hud9[4] * -0x100000;
        box[2] = d->hud9[3] * 0x100000;  box[3] = d->hud9[4] * 0x100000;
    }
    for (int k = 0; k < 4; k++) {
        int32_t p[3];
        if (drone_spawn_point(side, p, tg, box)) {
            int team = (tg->team ^ 1) & 1;
            DroneRec *r = rec_take(team);
            if (!r) return NULL;
            Obj *o = obj_create(&class_drone, team, p[0], p[1], 0x320000, r);
            if (!o) {
                if (r->next == NULL) rec_release(r, team);
                return NULL;
            }
            if (!snd_inst && ai_hooks.snd_play) {                        /* one hum for all drones */
                snd_inst = ai_hooks.snd_play(0x43e9a8, &snd_handle);
                if (snd_inst) {
                    if (ai_hooks.snd_stereo)
                        ai_hooks.snd_stereo(snd_inst, snd_pos[0], G.nplayers < 2 ? snd_pos[0] : snd_pos[1]);
                    memcpy(snd_pos[0], o->pos, sizeof snd_pos[0]);
                    memcpy(snd_pos[1], o->pos, sizeof snd_pos[1]);
                    near_tick = g_tick;
                    enemy_dist[0] = enemy_dist[1] = snd_dist[0] = snd_dist[1] = 0x7fffffff;
                }
            }
            obj_set_parent(o, tg);
            spawn_shadow(o, &gfx_455730);                                 /* class +0x40 = 0x455730 */
            /* FUN_00422c70(o, 0x4557e0, 1000): radar blip (HUD movers, not ported) */
            return o;
        }
        if (++side > 3) side = 0;
    }
    return NULL;
}
