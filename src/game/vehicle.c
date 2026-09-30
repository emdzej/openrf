/* Vehicle class and per-type behaviour, ported from 0x41edf0..0x41f150 (controllers) and
   0x427cd0..0x42b8e0 (vehicles), plus the Storage (bunker lift) class 0x417f50..0x418470 and a
   Destroyed Vehicle (wreck) class 0x4292d0..0x429990 and the fire functions 0x429d30..0x42b2a0. */
#include "vehicle.h"
#include "weapon.h"
#include "effect.h"
#include "rules.h"
#include <stddef.h>
#include <string.h>

extern Gfx gfx_44cd10, gfx_4431f8, gfx_44ee68;   /* game_tables.c */

VehState veh_states[2];
int32_t road_dir_flags;
static uint8_t music_counter[4];      /* byte 2 of def[0xaf] (the original writes into the descriptor) */

/* Sound events (VA of the event record in the 0x43e978 table; names from the sample list). */
enum {
    SND_JEEP = 0x43e978 /* "jeep" */, SND_HELI = 0x43e990 /* "heli" */, SND_RESUPPLY = 0x43eab0 /* "Ding" */,
    SND_FUEL_WARN = 0x43eac8 /* "FuelWarn" */, SND_DOCK = 0x43ec18 /* "Raise" */, SND_SERVO = 0x43ec48 /* "Servo" */,
    SND_BEACON = 0x43ed68 /* "DumbDirect" */, SND_BOAT_OFF = 0x43ed80 /* "TireIn" */,
    SND_BOAT_ON = 0x43ed98 /* "TireOut" */, SND_EMPTY = 0x43edc8 /* "OutAmmo" */,
};
static void snd(int cmd, uint32_t ev, Obj *o) { if (game_hooks.sound) game_hooks.sound(cmd, ev, o); }
static void expl(int team, const int32_t *p, uint32_t d) { spawn_explosion(team, p[0], p[1], p[2], d, NULL); }
static void hud_dirty(Obj *o, uint32_t m) { if (game_hooks.hud_dirty) game_hooks.hud_dirty(o->team, m); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* ------------------------------------------------------------------ controllers */
static uint32_t decode_buttons(uint32_t w, uint32_t raw, uint32_t prv)
{
    if (raw & 0x200000) w |= CW_X4;
    if (raw & 0x400000) w |= CW_X5;
    if (!(raw & 0x8000000)) { if (prv & 0x8000000) w |= CW_A_RELEASE; }
    else w |= (prv & 0x8000000) ? CW_A_HOLD : CW_A_PRESS;
    if (!(raw & 0x4000000)) { if (prv & 0x4000000) w |= CW_B_RELEASE; }
    else w |= (prv & 0x4000000) ? CW_B_HOLD : CW_B_PRESS;
    if (raw & 0x2000000) return w | ((prv & 0x2000000) ? CW_C_HOLD : CW_C_PRESS);
    if (prv & 0x2000000) w |= CW_C_RELEASE;
    return w;
}

uint32_t control_decode_digital(Obj *o)
{
    uint32_t raw = *o->tm->in_cur, prv = *o->tm->in_prev;
    uint32_t w = raw << 16;
    if (raw & 0x40000000) w |= CW_FWD;
    else w |= (raw & 0x80000000u) ? CW_BACK : CW_IDLE;
    if (raw & 0x10000000) w |= CW_LEFT;
    if (raw & 0x20000000) w |= CW_RIGHT;
    return decode_buttons(w, raw, prv);
}

static int32_t isqrt64(uint32_t v) { int32_t r = 0; while ((int64_t)(r + 1) * (r + 1) <= v) r++; return r; } /* FUN_00438550 */

uint32_t control_decode_analog(Obj *o)
{
    uint32_t raw = *o->tm->in_cur, prv = *o->tm->in_prev;
    uint32_t h = o->heading, w;
    if (!(raw & 0x10000)) {                    /* 8-way pad: steer towards the pressed direction */
        w = 0xffff0000u;
        if (raw & 0x40000000) h = (raw & 0x20000000) ? 0x80000 : ((raw & 0x10000000) ? 0x380000 : 0);
        else if (raw & 0x80000000u) h = (raw & 0x20000000) ? 0x180000 : ((raw & 0x10000000) ? 0x280000 : 0x200000);
        else if (raw & 0x20000000) h = 0x100000;
        else if (raw & 0x10000000) h = 0x300000;
        else { w = 1; goto done; }
    } else {                                   /* analog stick with magnitudes in the low 16 bits */
        if (!(raw & 0xf0000000u)) { w = 1; goto done; }
        uint32_t b8 = (raw >> 8) & 0xff;
        int32_t y = (raw & 0x40000000) ? (int32_t)(b8 + 1) * 0x100 : (raw & 0x80000000u) ? (-1 - (int32_t)b8) * 0x100 : 0;
        int32_t x = (raw & 0x10000000) ? (int32_t)((raw & 0xff) + 1) << 8 : (raw & 0x20000000) ? (-1 - (int32_t)(raw & 0xff)) << 8 : 0;
        h = (uint32_t)((atan2_fixed(x, y) >> 2) - 0x100000) & ANG_MASK;
        int32_t m = isqrt64((uint32_t)((x >> 8) * (x >> 8) + (y >> 8) * (y >> 8)));
        w = m == 0 ? 0xff0001u : ((uint32_t)(m > 0xff ? 0xff : m) << 24) | 0xff0000u;
    }
    {
        uint32_t d = (h - o->heading) & 0x3f0000;
        if (d < 0x140001 || d > 0x2bffff) w |= CW_FWD;
        else {
            w |= CW_BACK;
            if (d > 0x180000 && d < 0x280000) h = o->heading;
        }
    }
done:
    if (o->heading != h) { w |= CW_TURN; veh_state(o)->target_heading = h; }
    if (w & 1) w &= 0xffffff;
    return decode_buttons(w, raw, prv);
}

static void veh_set_controller(Obj *o, int type)   /* VehicleSetController 0x41f150 (table 0x443e18) */
{
    uint8_t s = TEAM_SCHEME(o->tm, type);   /* schemes > 3 -> 0; 0/2 digital, 1 analog */
    o->think = s == 1 ? (ThinkFn)control_decode_analog : (ThinkFn)control_decode_digital;
}

/* ------------------------------------------------------------------ helpers */
/* Repeated in the original wherever a vehicle dies: wreck (or the descriptor's death hook). */
static Obj *veh_kill(Obj *o, VehState *s)
{
    const VehicleDef *def = s->def;
    if (def->death_hook) { s->hook = def->death_hook; s->hp = 0; return NULL; }
    if (o->flags & OF_DELETE) return NULL;
    Obj *w = obj_create(&class_wreck, o->team, o->pos[0], o->pos[1], o->pos[2], o);
    obj_mark_delete(o);
    return w;
}

static void burn_fuel(Obj *o, VehState *s)
{
    int32_t b = (iabs(o->speed) * g_dt) >> 5;
    if (!b) return;
    s->fuel -= b;
    if (s->fuel < 1) { s->fuel = 0; veh_kill(o, s); }
    /* HUD fuel gauge (s->hud_fuel) not ported */
}

/* FUN_00428e90: speed multiplier for the ground under the vehicle; also sets road_dir_flags. */
int32_t terrain_speed_factor(Obj *o, const VehicleDef *def, VehState *s)
{
    road_dir_flags = 0;
    if (o->pos[2] > 0xffff) return 0x10000;
    bool boat = def->type == VT_JEEP && s->boat == 0x10000;
    if (s->water) return boat ? 0x4000 : 0xc000;
    if (boat) return 0x28f;
    uint32_t t = CELL_TERRAIN(*o->cell);
    if (t > 0x48 && t < 0x5a) { road_dir_flags = tun_road_flags[t - 0x49]; return 0x13333; }
    uint32_t b = CELL_BNO(*o->cell);
    if (b == BNO_BRIDGE_H) { road_dir_flags = 0xc; return 0x13333; }
    if (b == BNO_BRIDGE_V) { road_dir_flags = 3; return 0x13333; }
    return 0x10000;
}

static void pitch_follow(Obj *o, const VehicleDef *def, VehState *s)
{
    int32_t t = fix_mul(o->speed, s->cam[2]);
    s->cam[3] = approach_dt(s->cam[3], t, tun_pitch_rate[def->type][0], tun_pitch_rate[def->type][1]);
}

static int32_t apply_friction(int32_t spd, int32_t fr)
{
    if (spd < 0) { spd += fr * g_dt; if (spd >= 1) spd = 0; }
    else { spd -= fr * g_dt; if (spd <= -1) spd = 0; }
    return spd;
}

static int32_t emit_move(int32_t *d, Obj *o, uint32_t h, int32_t spd)
{
    const int32_t *v = dir_vec[(int32_t)h >> 16];
    int32_t k = g_dt * spd;
    d[0] = fix_mul(k, v[0]);
    d[1] = fix_mul(k, v[1]);
    d[2] = 0;
    o->speed = spd;
    o->heading = h;
    return spd;
}

/* ------------------------------------------------------------------ movement */
/* FUN_00428ca0: tank / MSV (default movement). */
int32_t veh_move_ground(int32_t *d, Obj *o, const VehicleDef *def, VehState *s)
{
    uint32_t c = s->ctrl;
    if (!c) return 0;
    int32_t spd = o->speed;
    uint32_t h = o->heading;
    pitch_follow(o, def, s);
    uint32_t mag = (c >> 24) & 0xff;
    int32_t f = fix_mul(terrain_speed_factor(o, def, s), (int32_t)(mag + 1) * 0x100);
    if (c & CW_FWD) { spd += def->accel * g_dt; int32_t l = fix_mul(def->max_fwd, f); if (spd > l) spd = l; }
    if (c & CW_BACK) { spd -= def->accel * g_dt; int32_t l = fix_mul(def->max_rev, f); if (spd < l) spd = l; }
    switch (c & CW_TURN) {
    case CW_TURN: turn_towards(&h, s->target_heading, def->turn); break;
    case CW_LEFT: h = (h - (uint32_t)(def->turn * g_dt)) & ANG_MASK; break;
    case CW_RIGHT: h = (h + (uint32_t)(def->turn * g_dt)) & ANG_MASK; break;
    }
    if (c & CW_IDLE) spd = apply_friction(spd, def->friction);
    return emit_move(d, o, h, spd);
}

/* FUN_0042a670: jeep. Turning alone accelerates; steering follows roads/bridges when not turning. */
int32_t veh_move_jeep(int32_t *d, Obj *o, const VehicleDef *def, VehState *s)
{
    uint32_t c = s->ctrl;
    if (!c) return 0;
    int32_t spd = o->speed;
    uint32_t h = o->heading;
    pitch_follow(o, def, s);
    int32_t mud = (int32_t)(((c >> 24) & 0xff) + 1) * 0x100, mlr = (int32_t)(((c >> 16) & 0xff) + 1) * 0x100;
    if (mlr < 0x8000) c &= ~(uint32_t)CW_TURN;
    if (mud < mlr) mud = mlr;
    if (!(c & (CW_FWD | CW_BACK)) && (c & CW_TURN)) c = (c & ~1u) | CW_FWD;
    int32_t f = 0;
    if (!(c & CW_IDLE)) f = fix_mul(terrain_speed_factor(o, def, s), mud);
    if (c & CW_FWD) { spd += def->accel * g_dt; int32_t l = fix_mul(def->max_fwd, f); if (l < spd) spd = l; }
    if (c & CW_BACK) { spd -= def->accel * g_dt; int32_t l = fix_mul(def->max_rev, f); if (spd < l) spd = l; }
    uint32_t turn = c & CW_TURN;
    if (turn == 0) {
road:
        if (road_dir_flags && mud) {
            int32_t hh = (int32_t)o->heading;
            uint32_t t;
            if ((road_dir_flags & 1) && !(hh < 0x380001 && hh > 0x7ffff))
                t = (uint32_t)(-(((int32_t)(o->pos[0] & 0x1ffffc) - 0x100000) >> 2));
            else if ((road_dir_flags & 2) && !(hh < 0x180001 || hh > 0x27ffff))
                t = (uint32_t)((((int32_t)(o->pos[0] & 0x1ffffc) - 0x100000) >> 2) + 0x200000);
            else if ((road_dir_flags & 4) && !(hh < 0x80001 || hh > 0x17ffff))
                t = (uint32_t)(0x100000 - (((int32_t)(o->pos[1] & 0x1ffffc) - 0x100000) >> 2));
            else if ((road_dir_flags & 8) && !(hh < 0x280001 || hh > 0x37ffff))
                t = (uint32_t)((((int32_t)(o->pos[1] & 0x1ffffc) - 0x100000) >> 2) - 0x100000);
            else goto fric;
            turn_towards(&h, t & ANG_MASK, fix_mul(mud, def->turn));
        }
    } else if (turn == CW_TURN) {
        uint32_t diff = (h - s->target_heading) & ANG_MASK;
        if (road_dir_flags && diff > 0x3c0000 && diff < 0x40000) goto road;   /* never true (original) */
        turn_towards(&h, s->target_heading, def->turn);
    } else {
        int32_t r = (c & CW_LEFT) ? -def->turn : def->turn;
        h = ((uint32_t)(fix_mul(mud, r) * g_dt) + h) & ANG_MASK;
    }
fric:
    if ((c & CW_IDLE) || turn == 0) spd = apply_friction(spd, def->friction);
    return emit_move(d, o, h, spd);
}

/* FUN_0042abe0: helicopter flight. Velocity lags the commanded direction, yaw has inertia,
   strafing with the extra buttons, random hover drift, climbs to z = 0x320000 (50 px). */
int32_t veh_move_heli(int32_t *d, Obj *o, const VehicleDef *def, VehState *s)
{
    uint32_t c = s->ctrl;
    if (!c) return 0;
    int32_t spd = o->speed, strafe = 0, bank = 0, r;
    uint32_t h = o->heading;
    pitch_follow(o, def, s);
    if (o->cell == &G.outside && (o->pos[0] < -0x200000 || o->pos[1] < -0x200000 || o->pos[0] > 0x10200000 || o->pos[1] > 0x10200000))
        if (game_hooks.sub) game_hooks.sub();            /* SpawnSub: the submarine hunts helis off the map */
    int32_t mud = (int32_t)(((c >> 24) & 0xff) + 1) * 0x100, mlr = (int32_t)(((c >> 16) & 0xff) + 1) * 0x100;
    if (c & CW_FWD) { spd += def->accel * g_dt; int32_t l = fix_mul(def->max_fwd, mud); if (l < spd) spd = l; }
    if (c & CW_BACK) { spd -= def->accel * g_dt; int32_t l = fix_mul(def->max_rev, mud); if (spd < l) spd = l; }
    if (c & CW_IDLE) {
        if (spd < 0) { spd += def->friction * g_dt; if (spd > 0) spd = 0; }
        else { spd -= def->friction * g_dt; if (spd < 0) spd = 0; }
    }
    if ((c & CW_TURN) == CW_TURN) {
        bank = 0x30000;
        r = fix_mul(mlr, def->turn);
        s->turn_vel = approach_dt(s->turn_vel, r, tun_heli_turn_up, tun_heli_turn_down);
        if (s->turn_vel && turn_towards(&h, s->target_heading, s->turn_vel)) s->turn_vel = 0;
    } else {
        if ((c & CW_TURN) == CW_LEFT) { r = fix_mul(mlr, -def->turn); bank = 0x30000; }
        else if (!(c & CW_RIGHT)) {
            r = 0;
            if (c & CW_X4) { strafe = -0x100000; bank = 0x30000; }
            else if (c & CW_X5) { strafe = 0x100000; bank = -0x30000; }
        } else { r = fix_mul(mlr, def->turn); bank = -0x30000; }
        int32_t rate = ((r >= 0 && s->turn_vel < 1) || (r < 1 && s->turn_vel > 0)) ? tun_heli_turn_down : tun_heli_turn_up;
        s->turn_vel = approach(s->turn_vel, r, rate);                 /* not scaled by dt in the original */
        if (s->turn_vel) h = ((uint32_t)(g_dt * s->turn_vel) + h) & ANG_MASK;
    }
    const int32_t *v = dir_vec[(int32_t)h >> 16];
    int32_t vx = fix_mul(spd, v[0]), vy = fix_mul(spd, v[1]);
    if (strafe) {
        const int32_t *sv = dir_vec[((o->heading + (uint32_t)strafe) & 0x3f0000) >> 16];
        vx += fix_mul(sv[0], 0xcccc);
        vy += fix_mul(sv[1], 0xcccc);
    }
    if (vx == 0 && vy == 0) {
        vx = s->drift[0];
        int32_t t = vx - 0x147 + rand_range(0x28f);
        s->drift[0] = t < -0x7ae ? -0x7ae : t > 0x7ae ? 0x7ae : t;
        vy = s->drift[1];
        t = vy - 0x147 + rand_range(0x28f);
        s->drift[1] = t < -0x7ae ? -0x7ae : t > 0x7ae ? 0x7ae : t;
    } else s->drift[0] = s->drift[1] = 0;
    s->vel[0] = approach(s->vel[0], vx, g_dt * 0x7ae);
    s->vel[1] = approach(s->vel[1], vy, g_dt * 0x7ae);
    if (bank && !strafe && spd > -0x10000 && spd < 0x10000) bank = fix_mul(spd, bank);
    if (bank) s->tilt = approach(s->tilt, bank, g_dt * 0x147a);
    else s->tilt = approach(s->tilt, 0, g_dt * 0x28f4);
    o->u70 = fix_mul(spd, 0x18000);
    d[0] = s->vel[0] * g_dt;
    d[1] = s->vel[1] * g_dt;
    if (o->pos[2] < 0x320000) {
        d[2] = g_dt * 0x8000;
        if (d[2] + o->pos[2] > 0x320000) d[2] = 0x320000 - o->pos[2];
    } else d[2] = 0;
    o->speed = spd;
    o->heading = h;
    return 1;
}

/* FUN_0042b730: final descent onto the pad (heading and offset scale with the remaining height). */
static int32_t heli_move_descend(int32_t *d, Obj *o, const VehicleDef *def, VehState *s);
static int heli_spindown(Obj *o, VehState *s);

/* FUN_0042b640: brake, then compute the descent rates towards the pad centre. */
static int32_t heli_move_land(int32_t *d, Obj *o, const VehicleDef *def, VehState *s)
{
    s->ctrl = CW_IDLE;
    s->drift[0] = s->drift[1] = 0;
    int32_t r = veh_move_heli(d, o, def, s);
    if (s->vel[0] == 0 && s->vel[1] == 0) {
        uint32_t u = (0x180000u - o->heading) & ANG_MASK;
        s->turn_vel = u < 0x200001 ? fix_mul(-(int32_t)u, 0x51e) : -fix_mul((int32_t)u - 0x400000, 0x51e);
        int32_t cp[3];
        cell_to_pos(s->dock_cell, cp);
        s->drift[0] = fix_mul(o->pos[0] - cp[0], 0x51e);
        s->drift[1] = fix_mul(o->pos[1] - cp[1], 0x51e);
        s->move = heli_move_descend;
        o->speed = 1;
    }
    return r;
}

static int32_t heli_move_descend(int32_t *d, Obj *o, const VehicleDef *def, VehState *s)
{
    int32_t cp[3];
    cell_to_pos(s->dock_cell, cp);
    int32_t z = o->pos[2] - g_dt * 0x8000;
    if (z < 0) { s->hook = heli_spindown; z = 0; }
    int32_t k = z >> 16;
    o->heading = ((uint32_t)(s->turn_vel * k) + 0x180000u) & ANG_MASK;
    d[0] = (s->drift[0] * k - o->pos[0]) + cp[0];
    d[1] = (s->drift[1] * k - o->pos[1]) + cp[1];
    d[2] = z - o->pos[2];
    o->speed = 1;
    (void)def;
    return 1;
}

/* ------------------------------------------------------------------ per-type hooks (state+0x0c) */
static int tank_turret(Obj *o, VehState *s);
static int msv_launcher(Obj *o, VehState *s);
static int jeep_launch2(Obj *o, VehState *s);
static int jeep_think(Obj *o, VehState *s);
static int heli_spinup2(Obj *o, VehState *s);
static int heli_takeoff(Obj *o, VehState *s);
static int heli_rotor(Obj *o, VehState *s);
static int heli_spindown2(Obj *o, VehState *s);

int veh_tank_launch(Obj *o, VehState *s)          /* 0x429f20: drive forward off the lift */
{
    s->ctrl = 0xffff0002u;
    o->u68 += g_dt;
    if (o->u68 >= tun_launch_ticks) s->hook = tank_turret;
    return 0;
}

static int tank_turret(Obj *o, VehState *s)       /* 0x429f50: Q/E rotate the turret, C recentres */
{
    uint32_t b = s->ctrl & 0xd000;
    if (b) {
        if (b == CW_X4) s->turret_target = (int32_t)((uint32_t)(s->turret - g_dt * 0x4ccc) & ANG_MASK);
        else if (b == CW_X5) s->turret_target = (int32_t)((uint32_t)(s->turret + g_dt * 0x4ccc) & ANG_MASK);
        else s->turret_target = 0;
    }
    turn_towards((uint32_t *)&s->turret, (uint32_t)s->turret_target, 0x4ccc);
    turn_towards((uint32_t *)&s->elev, (uint32_t)s->elev_target, 0x4ccc);
    if (s->fire_pending && s->elev_target == s->elev)
        veh_tank_fire(o, s->def, s, s->elev == 0 ? 0 : -1, 0x20);
    return 0;
}

int veh_msv_launch(Obj *o, VehState *s)           /* 0x42a250 */
{
    s->ctrl = 0xffff0002u;
    o->u68 += g_dt;
    if (o->u68 >= tun_launch_ticks) s->hook = msv_launcher;
    return 0;
}

static int msv_launcher(Obj *o, VehState *s)      /* 0x42a280 */
{
    if (s->turret < 0 && s->slot[0].ammo != 0) {
        s->turret += g_dt * 0x2666;
        if (s->turret > 0) s->turret = 0;
    }
    turn_towards((uint32_t *)&s->elev, (uint32_t)s->elev_target, 0x4ccc);
    if (s->turret_target /* +0x5c = pending shot for the MSV */ && s->elev_target == s->elev)
        veh_msv_fire(o, s->def, s, s->elev == 0 ? 0 : -1, 0x20);
    return 0;
}

int veh_jeep_launch(Obj *o, VehState *s)          /* 0x42a410 */
{
    s->hook = jeep_launch2;
    s->launch_tick = g_tick + 0x62;
    (void)o;
    return 1;
}

static int jeep_launch2(Obj *o, VehState *s)      /* 0x42a430: engine start, then roll off at half speed */
{
    if (s->launch_tick <= g_tick) {
        snd(1, SND_JEEP, o);
        s->hook = jeep_think;
        o->speed = s->def->max_fwd >> 1;
        s->launch_tick = 0;
    }
    return 1;
}

static int beacon_level(Obj *o, const int32_t *target)
{
    uint32_t u = angle_between(o->pos, target) - o->heading, v = u & ANG_MASK;
    if (!(u & 0x3e0000)) return 0x10;
    if (v > 0x1fffff) v = (0u - v) & ANG_MASK;
    if (v >= 0x100000) return -1;
    int32_t k = (0x10000 - ((int32_t)(v << 2) >> 8)) >> 12;
    return k > 15 ? 15 : k;
}

static int jeep_think(Obj *o, VehState *s)        /* JeepFlagCheck 0x42a480: boat morph, win check, beacon */
{
    if (s->boat != s->boat_target) {
        s->ctrl = (s->ctrl & 0xffffffe1u) | CW_IDLE;
        s->boat = approach(s->boat, s->boat_target, g_dt * 0x444);
    }
    int team = o->team, k;
    Obj *ef = G.flag_obj[team == 0];
    if (ef) {
        if (ef->parent == o) {
            if (CELL_TERRAIN(*o->cell) == o->tm->pad_terrain) {
                G.winner = team;
                if (game_hooks.end_game) game_hooks.end_game(team);
                return 0;
            }
        } else if ((k = beacon_level(o, ef->pos)) >= 0) { k = -k; goto set; }
    }
    {
        int32_t home[3] = { o->tm->pad->x, o->tm->pad->y, 0 };
        k = beacon_level(o, home);
        if (k < 0) k = 0;
    }
set:
    if (k == -0x10) {
        if (s->launch_tick == 0) snd(1, SND_BEACON, NULL);
        s->launch_tick = 1;
    } else s->launch_tick = 0;
    if (s->beacon != k) s->beacon = k;       /* HudMarkDirty(0x200) */
    return 0;
}

int veh_jeep_dock(Obj *o, VehState *s)            /* 0x42ab90 */
{
    Obj *f = G.flag_obj[o->team];
    if (f && f->parent == o) { snd(1, SND_RESUPPLY, NULL); hide_flag_in_building(o->team); }   /* own flag home */
    dock_vehicle(o);
    (void)s;
    return 0;
}

int veh_heli_spinup(Obj *o, VehState *s)          /* 0x42b3c0 */
{
    s->turret_target = 0;
    s->turret += g_dt * 0x49b;
    if (s->turret > 0x10000) {
        snd(1, SND_HELI, o);
        o->gfx = s->def->gfx;
        s->turret = 0x10000;
        s->hook = heli_spinup2;
    }
    return 1;
}

static int heli_spinup2(Obj *o, VehState *s)      /* 0x42b430 */
{
    s->boat_target += g_dt * 0x666;
    if (s->boat_target > 0x3ffff) { o->gfx = s->def->gfx; s->hook = heli_takeoff; s->boat_target = 0x40000; }
    snd(5, 0, o);
    s->boat = (s->boat_target * g_dt + s->boat) & ANG_MASK;
    return 1;
}

static int heli_takeoff(Obj *o, VehState *s)      /* 0x42b4c0 */
{
    int32_t d[3] = { tun_heli_takeoff_dx * g_dt, 0, g_dt * 0x8000 };
    if (d[2] + o->pos[2] > 0x31ffff) d[2] = 0x320000 - o->pos[2];
    obj_move(o, d);
    if (o->pos[2] < 0x320000) {
        if (o->pos[2] < 0) o->pos[2] = 0;
        o->u70 = tun_heli_lift_z[o->pos[2] >> 16] << 15;
        s->tilt = tun_heli_lift_tilt[o->pos[2] >> 16] << 16;
    } else {
        o->pos[2] = 0x320000;
        o->u70 = 0;
        s->hook = heli_rotor;
        s->tilt = 0;
    }
    o->heading = (((o->id & 1) == 0 ? 0xffff8000u : 0) + 0x4000u + o->heading) & ANG_MASK;
    s->boat = (s->boat_target * g_dt + s->boat) & ANG_MASK;
    return 1;
}

static int heli_rotor(Obj *o, VehState *s)        /* 0x42b5b0 */
{
    s->boat = (s->boat_target * g_dt + s->boat) & ANG_MASK;
    (void)o;
    return 0;
}

int veh_heli_dock(Obj *o, VehState *s)            /* 0x42b600: fire on the pad starts the landing */
{
    s->dock_cell = o->cell;
    s->move = heli_move_land;
    s->hook = heli_rotor;
    s->boat = (s->boat_target * g_dt + s->boat) & ANG_MASK;
    return 0;
}

static int heli_spindown(Obj *o, VehState *s)     /* 0x42b7d0 */
{
    int32_t rs = s->boat_target - g_dt * 0x666;
    s->boat_target = rs;
    if (rs < 0x8001) {
        s->boat_target = 0x8000;
        if (g_dt * 0x8000 < 0x200000) {
            int32_t a = (int32_t)((0u - (uint32_t)s->boat) & ANG_MASK); if (a > 0x1fffff) a -= 0x400000;
            s->boat = (g_dt * 0x8000 + s->boat) & ANG_MASK;
            int32_t b = (int32_t)((0u - (uint32_t)s->boat) & ANG_MASK); if (b > 0x1fffff) b -= 0x400000;
            if ((a > 0 || b < 0) && (a < 0 || b > 0)) goto out;    /* rotor has not reached rest angle */
        }
        snd(1, SND_SERVO, o);
        s->boat = 0;
        o->gfx = s->def->gfx;
        s->hook = heli_spindown2;
        s->boat_target = 0;
    } else s->boat = (g_dt * rs + s->boat) & ANG_MASK;
out:
    snd(5, 0, o);
    return 1;
}

static int heli_spindown2(Obj *o, VehState *s)    /* 0x42b8e0 */
{
    s->turret -= g_dt * 0x49b;
    snd(5, 0, o);
    if (s->turret < 0) {
        o->gfx = s->def->gfx;
        s->turret = 0;
        Team *t = o->tm;
        if (t->door_cell) { CELL_SET_TERRAIN(*t->door_cell, t->pad_terrain); t->door_cell = NULL; }
        dock_vehicle(o);
    }
    return 1;
}

int veh_heli_init(Obj *o, const VehicleDef *def) { (void)o; (void)def; return 1; } /* 0x42b5e0: Shadow object (render) */

int veh_heli_damaged(Obj *o, const VehicleDef *def, VehState *s, Obj *src)   /* 0x42b330: knocked around */
{
    (void)o; (void)def;
    s->turn_vel = (rand_range(0x180) + rand_range(0x180) - 0x180) * 0x80;
    if (src) {
        int32_t k = (rand_range(0x100) + 0x80) * 0x80;
        const int32_t *v = dir_vec[(int32_t)src->heading >> 16];
        s->vel[0] = fix_mul(v[0], k);
        s->vel[1] = fix_mul(v[1], k);
    }
    return 0;
}

/* ------------------------------------------------------------------ water (state+0x44) */
static void water_sinking(Obj *o, const VehicleDef *def, VehState *s, int32_t moved);
static void water_splash(Obj *o, const VehicleDef *def, VehState *s, int32_t moved);
static bool boat_mode(const VehicleDef *def, const VehState *s) { return def->type == VT_JEEP && s->boat_target == 0x10000; }

void veh_water_normal(Obj *o, const VehicleDef *def, VehState *s, int32_t moved)   /* 0x4299e0 */
{
    if (!s->water) return;
    if (s->water == 2 && !boat_mode(def, s)) {
        s->water_hook = water_sinking; s->water_anim = 0; o->gfx = def->gfx_sink;
        return;
    }
    if (moved && (fix_mul(terrain_speed_factor(o, def, s), def->max_fwd) >> 1) <= o->speed) {
        s->water_hook = water_splash; s->water_anim = 0x40000; o->gfx = def->gfx_splash;
    }
}

static int bunker_after_death(intptr_t a, intptr_t b, int32_t period);
static void to_normal(Obj *o, const VehicleDef *def, VehState *s)
{
    s->water_hook = veh_water_normal; s->water_anim = 0; o->gfx = def->gfx; o->pos[2] = 0;
}

static void water_sinking(Obj *o, const VehicleDef *def, VehState *s, int32_t moved)   /* 0x429a80 */
{
    if (s->water == 0) { to_normal(o, def, s); return; }
    if (s->water == 1 || boat_mode(def, s)) {
        o->pos[2] += g_dt * 0x6666;
        if (o->pos[2] >= 0) {
            o->pos[2] = 0;
            int32_t l = fix_mul(terrain_speed_factor(o, def, s), def->max_fwd);
            if (((uint8_t)moved & ((l >> 1) <= o->speed)) != 0) {     /* byte-truncated flag (original) */
                s->water_hook = water_splash; s->water_anim = 0x40000; o->gfx = def->gfx_splash;
                return;
            }
            to_normal(o, def, s);
            return;
        }
    } else {
        o->pos[2] -= g_dt * 0x6666;
        if ((o->pos[2] >> 16) <= -(def->sink_depth >> 16)) {
            o->pos[2] = 0x10000 - (int32_t)((uint32_t)def->sink_depth & 0xffff0000u);
            obj_mark_delete(o);
            if (o->tm && o->tm->view) timer_add(def->respawn_delay, bunker_after_death, (intptr_t)o->team << 16, (intptr_t)o->tm->view);
            int32_t p[3] = { o->pos[0], o->pos[1], 0 };
            expl(o->team, p, 0x454300);
        }
    }
    s->water_anim += g_dt * 0x3333;
    if (s->water_anim > 0x5ffff) s->water_anim -= 0x60000;
}

static void water_splash(Obj *o, const VehicleDef *def, VehState *s, int32_t moved)   /* 0x429c40 */
{
    int32_t w = s->water, old = s->water_anim;
    s->water_anim = g_dt * 0x3333 + old;
    if (w == 2 && !boat_mode(def, s)) {
        s->water_hook = water_sinking; s->water_anim = 0; o->pos[2] = 0; o->gfx = def->gfx_sink;
    }
    bool fast = w == 1 && moved && o->speed >= 1;
    int32_t a = s->water_anim;
    if (a < 0xd0000) {
        if (!fast && (int32_t)((uint32_t)old & 0xffff0000u) < 0x30001 && (int32_t)((uint32_t)a & 0xffff0000u) > 0x30000) {
            s->water_hook = veh_water_normal; s->water_anim = 0; o->gfx = def->gfx;
        }
        return;
    }
    s->water_anim = a - (fast ? 0x50000 : 0xd0000);
}

/* FUN_00429040: fuel/ammo dumps (touched parts with kind 1/2) and own gates (kind 3). */
static void resupply(Obj *o, const VehicleDef *def, VehState *s, int moving)
{
    ShapePart *p = s->pickup_part;
    uint8_t kind = p ? p->kind : 0;
    if (kind == 0) { s->pickup_cell = NULL; return; }
    if (kind < 3) {
        if (moving) return;
        int32_t cp[3];
        cell_to_pos(s->pickup_cell, cp);
        if (!shapes_overlap(o->pos, o->heading, o->gfx->shape, cp, 0, p)) { s->pickup_cell = NULL; return; }
        if (kind == 1) {
            s->fuel += g_dt * 0x8000;
            if (s->fuel > def->weapon[2].ammo * 0x10000) s->fuel = def->weapon[2].ammo * 0x10000;
            else if (s->resupply_tick <= g_tick) { s->resupply_tick = g_tick + 0x28; snd(1, SND_RESUPPLY, o); }
            burn_fuel(o, s);
            return;
        }
        for (int k = 0; k < tun_resupply_slots[def->type]; k++) {
            int32_t inc = (g_dt << 15) >> 16;
            if (inc < 1) inc = 1;
            s->slot[k].ammo += inc;
            if (s->slot[k].ammo < def->weapon[k].ammo) {
                if (s->resupply_tick <= g_tick) { s->resupply_tick = g_tick + 0x28; snd(1, SND_RESUPPLY, o); }
                return;
            }
            s->slot[k].ammo = def->weapon[k].ammo;
        }
        return;
    }
    if (kind == 3 && (int)CELL_TEAM(*s->pickup_cell) == o->team) {
        spawn_gate(s->pickup_cell, o);                               /* SpawnGate 0x423fb0: own gate opens */
    }
    s->pickup_cell = NULL;
}

/* ------------------------------------------------------------------ weapons */
/* FUN_0042f030: n points = src rotated about x by pitch*4 (FUN_00408400) [+ add]. */
static void muzzle_points(int32_t *out, const int32_t *src, const int32_t *add, int n, int32_t pitch)
{
    if (pitch == 0) {
        for (int k = 0; k < 3 * n; k++) out[k] = src[k] + (add ? add[k] : 0);
        return;
    }
    int32_t m[9];
    mat_rot_x(m, pitch << 2);
    mat_transform_verts((int32_t (*)[3])out, (const int32_t (*)[3])src, m, n);
    if (add) for (int k = 0; k < 3 * n; k++) out[k] += add[k];
}

int veh_tank_fire(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits)   /* TankFireCannon 0x429d30 */
{
    if (!(bits & 0x60)) return 0;
    uint32_t tgt;
    if (arg < 0) {                          /* button B: lob (raised barrel) */
        arg = -1 - arg;
        if (s->elev_target != 0) goto check;
        s->elev_target = 0x3b8e39;
        if (s->elev != 0) goto check;
        tgt = 0x3b8e39;
    } else {
        s->elev_target = 0;
        if (s->elev != 0x3b8e39) goto check;
        tgt = 0;
    }
    turn_towards((uint32_t *)&s->elev, tgt, 0x4ccc);
check:;
    int32_t pitch;
    if (s->elev == 0) pitch = 0;
    else if (s->elev != 0x3b8e39) { s->fire_pending = 1; return 0; }
    else pitch = tun_tank_long_pitch;
    const WeaponDef *w = &def->weapon[arg];
    if (g_tick <= s->slot[arg].next_fire) return 0;
    if (s->slot[arg].ammo < 1) {
        s->fire_pending = 0;
        snd(1, SND_EMPTY, o);
        s->slot[arg].next_fire = w->reload + g_tick;
        return 0;
    }
    int32_t off[3];
    muzzle_points(off, muzzle_tank[0], muzzle_tank[1], 1, pitch);      /* barrel tip, raised, + turret pivot */
    uint32_t h = ((uint32_t)s->turret + o->heading) & ANG_MASK;
    Obj *p = fire_projectile(o->pos, off, h, pitch, w->proj, o->team, o);
    if (!p) return 0;
    s->fire_pending = 0;
    uint32_t saved = o->heading;
    o->heading = h;                                                     /* muzzle flash follows the turret */
    Obj *f = spawn_explosion_attached(o, o->team, off, 0x454550);
    o->heading = saved;
    if (f) expl_set_pitch(f, pitch);
    s->slot[arg].next_fire = w->reload + g_tick;
    s->slot[arg].ammo--;
    hud_dirty(o, arg == 0 ? 0x40 : 0x80);
    return 1;
}

int veh_jeep_grenade(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits)   /* 0x42aa00 */
{
    if (!(bits & 0x20)) return 0;
    const WeaponDef *w = &def->weapon[arg];
    if (g_tick <= s->slot[arg].next_fire) return 0;
    if (s->slot[arg].ammo < 1) {
        snd(1, SND_EMPTY, o);
        s->slot[arg].next_fire = w->reload + g_tick;
        return 0;
    }
    Obj *g = jeep_throw_grenade(o, w->arg, def, s);                     /* FUN_00431de0 */
    if (!g) return 0;
    s->slot[arg].next_fire = w->reload + g_tick;
    s->slot[arg].ammo--;
    hud_dirty(o, arg == 0 ? 0x40 : 0x80);
    return 1;
}

int veh_jeep_boat(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits)   /* 0x42aae0 */
{
    (void)def; (void)arg;
    if (!(bits & 0x20)) return 0;
    int w = water_state(o);
    if (w != 0 && s->boat_target == 0) { snd(1, SND_BOAT_ON, o); s->boat_target = 0x10000; return 0; }
    if (w != 2 && s->boat_target == 0x10000) { snd(1, SND_BOAT_OFF, o); s->boat_target = 0; }
    return 0;
}

int veh_jeep_drop_flag(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits) /* 0x42ab70 */
{
    (void)def; (void)s; (void)arg;
    if (bits & 0x20) flag_toggle(o);                     /* FUN_004248a0: drop / pick up */
    return 0;
}

int veh_msv_fire(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits)   /* Msv_Fire 0x42a010 */
{
    if (!(bits & 0x60)) return 0;
    if (arg < 0) {
        arg = -1 - arg;
        if (s->elev_target == 0) {
            s->elev_target = 0x3b8e39;
            if (s->elev == 0) turn_towards((uint32_t *)&s->elev, 0x3b8e39, 0x4ccc);
        }
    } else {
        s->elev_target = 0;
        if (s->elev == 0x3b8e39) turn_towards((uint32_t *)&s->elev, 0, 0x4ccc);
    }
    int32_t pitch = tun_msv_pitch_flat;
    if (s->elev != 0) {
        pitch = tun_msv_pitch_lob;
        if (s->elev != 0x3b8e39) { s->turret_target = 1; return 0; }   /* +0x5c: fire when raised */
    }
    const WeaponDef *w = &def->weapon[arg];
    if (s->slot[arg].ammo < 1 && s->slot[arg].next_fire < g_tick) {
        s->turret_target = 0;
        snd(1, SND_EMPTY, o);
        s->slot[arg].next_fire = w->reload + g_tick;
        return 0;
    }
    if (g_tick <= s->slot[arg].next_fire || s->turret < 0) return 0;   /* +0x58 < 0: launcher reloading */
    int32_t off[6];                                    /* rocket start, flash position */
    muzzle_points(off, &msv_muzzle_raw[10], &msv_muzzle_raw[s->turret * 3], 2, pitch);   /* 0x44d0b0 + tube 0x44d088[t] */
    Obj *p = fire_projectile(o->pos, off, o->heading, pitch, (tun_msv_pitch_lob == pitch) + 8, o->team, o);
    if (!p) return 0;
    s->turret_target = 0;
    Obj *f = spawn_explosion_attached(o, o->team, off + 3, 0x4544f0);
    if (f) expl_set_pitch(f, pitch);
    s->slot[arg].ammo--;
    s->slot[arg].next_fire = w->reload + g_tick;
    if (s->slot[arg].ammo < 1) { s->slot[arg].ammo = 0; s->turret = (int32_t)0xfffa0000; }
    else if (++s->turret >= 3) { snd(1, 0x43ea98, o); s->turret = (int32_t)0xfffa0000; }   /* reload the 3 tubes */
    hud_dirty(o, arg == 0 ? 0x40 : 0x80);
    return 1;
}

int veh_msv_mine(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits)   /* Msv_Mine 0x42a310 */
{
    if (!(bits & 0x60) || G.nplayers < 2) return 0;    /* (mines only in the 2-player game) */
    const WeaponDef *w = &def->weapon[arg];
    if (s->slot[arg].next_fire < g_tick && s->slot[arg].ammo > 0 && s->water != 2) {
        const int32_t *v = dir_vec[(int32_t)o->heading >> 16];
        Obj *m = spawn_mine(fix_mul(v[0], w->arg[0]) + o->pos[0], fix_mul(v[1], w->arg[1]) + o->pos[1]);
        if (m) {
            hud_dirty(o, arg == 0 ? 0x40 : 0x80);
            s->slot[arg].ammo--;
            s->slot[arg].next_fire = w->reload + g_tick;
            return 1;
        }
    }
    return 0;
}

int veh_heli_fire(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits)  /* Heli_Fire 0x42b100 */
{
    if (!(bits & 0x60)) return 0;
    uint32_t f = o->flags;
    int k = (int)((f & 0x10000000) >> 28);              /* selected weapon: guns / missiles (button C) */
    const WeaponDef *w = &def->weapon[k];
    if (!(s->slot[k].next_fire < g_tick)) return 0;
    if (s->slot[k].ammo < 1) {
        snd(1, SND_EMPTY, o);
        s->slot[k].next_fire = w->reload + g_tick;
        return 0;
    }
    const int32_t *mz = (f & 0x8000000) ? muzzle_heli[0] : muzzle_heli[1];   /* alternate sides */
    uint32_t h = o->heading;
    if (arg != 0) h = (uint32_t)(((f & 0x8000000) ? 0 : 0xfffc0000u) + 0x20000 + o->heading) & ANG_MASK;
    o->flags = f ^ 0x8000000;
    int32_t pitch = arg;
    if (w->proj == 7 && arg != 0) pitch = 0x71c71;
    Obj *p = fire_projectile(o->pos, mz, h, pitch, w->proj, o->team, o);
    if (!p) return 0;
    if (o->speed > 0) proj_add_speed(p, o->speed);
    if (w->proj == 6) {
        Obj *e = spawn_explosion_attached(o, o->team, mz + 3, 0x454580);
        if (e) expl_set_pitch(e, pitch);
    }
    hud_dirty(o, k == 0 ? 0x40 : 0x80);
    s->slot[k].ammo--;
    s->slot[k].next_fire = w->reload + g_tick;
    return 1;
}

int veh_heli_missile(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits) /* Heli_Missile 0x42b2a0 */
{
    (void)def; (void)s; (void)arg;
    if (bits & 0x20) {                                   /* toggle guns / missiles */
        snd(1, 0x43edb0, o);
        o->flags ^= 0x10000000;
        /* HUD block +0xb8 / +0xd0 (selected weapon highlight) */
        hud_dirty(o, 0x1c0);
    }
    return 0;
}

/* ------------------------------------------------------------------ Vehicle class */
typedef struct { int32_t type; uint32_t heading; Team *tm; } VehArgs;   /* SpawnVehicle local_10.. */

static int vehicle_init(const ObjClass *c, Obj *o, void *argp)   /* VehicleInit 0x428210 */
{
    VehArgs *a = argp;
    VehState *s = &veh_states[o->team & 1];
    if (a->type > 3) a->type = 0;
    const VehicleDef *def = &vehicle_defs[a->type];
    memset(s, 0, sizeof *s);
    s->def = def;
    s->hook = def->launch_hook;
    s->move = def->move ? def->move : veh_move_ground;
    s->armour = def->armour;
    s->hp = def->hp;
    s->water_hook = def->water_hook;
    memcpy(s->cam, def->cam, sizeof s->cam);
    o->gfx = def->gfx;
    o->speed = 0;
    o->heading = a->heading;
    o->p60 = s;
    o->tm = a->tm;
    o->u70 = 0;
    o->u68 = 0;
    if (def->init_hook && !def->init_hook(o, def)) return 0;
    veh_set_controller(o, a->type);
    o->flags |= OF_NEW;
    G.cur_vehicle[o->team] = o;
    G.cur_vehicle_id[o->team] = o->id;
    uint8_t *st = &TEAM_STOCK(a->tm, a->type);
    if (*st != 0xff && *st != 0) (*st)--;
    s->obj = o;
    s->cam[3] = def->max_fwd / s->cam[16];
    if (a->tm->view) a->tm->view->last_type = def->type;         /* state+0xec = def[0x5a] / state+0x120 */
    if (game_hooks.hud) game_hooks.hud(GHUD_VEH_INIT, o->team, o);   /* FUN_00422c70 radar blip */
    (void)c;
    return 1;
}

static int vehicle_destroy(const ObjClass *c, Obj *o)       /* VehicleDestroy 0x4283c0 */
{
    if (G.cur_vehicle_id[o->team] == o->id) G.cur_vehicle[o->team] = NULL;
    Team *t = o->tm;
    if (t->door_cell) { CELL_SET_TERRAIN(*t->door_cell, t->pad_terrain); }
    t->door_cell = NULL;
    if (game_hooks.hud) game_hooks.hud(GHUD_VEH_GONE, o->team, o);   /* HUD slots 5-9 released */
    (void)c;
    return 1;
}

static uint32_t vehicle_hit_static(Obj *o, uint32_t *cell, int bno)   /* 0x428c40: state+0xa4 = cell */
{
    veh_state(o)->hit_cell = cell;
    veh_state(o)->hit_bno = bno;
    return 1;
}

static uint32_t vehicle_hit_object(Obj *o, Obj *other)      /* 0x428c60 */
{
    VehState *s = veh_state(o);
    s->hit_obj = other;
    s->hit_obj_id = other->id;
    if (other->cls->id == 2) s->hit_cell = other->cell;
    return 5;
}

int veh_damage(Obj *o, Obj *src, int32_t amount)            /* 0x428f60 */
{
    VehState *s = veh_state(o);
    const VehicleDef *def = s->def;
    if (amount - s->armour < 1) return 0;
    s->_4c = g_tick + 10;
    s->hp -= amount - s->armour;
    if (s->hp > 0) {
        if (!def->damage_hook) return 0;
        if (!def->damage_hook(o, def, s, src)) return 0;
    }
    Obj *w = veh_kill(o, s);
    if (w && s->hp < -0x280000) w->flags |= OF_NEW;
    return 2;
}

static void vehicle_update(const ObjClass *cls, Obj *o)     /* VehicleUpdate 0x428490 */
{
    VehState *s = veh_state(o);
    const VehicleDef *def = s->def;
    Team *tm = o->tm;
    (void)cls;
    if (o->flags & OF_NEW) {
        o->flags &= ~OF_NEW;
        tm->door_cell = NULL;
        if (def->snd_engine) { snd(1, def->snd_engine, o); o->flags |= OF_SOUND; }
        s->slot[0].ammo = def->weapon[0].ammo;
        s->slot[1].ammo = def->weapon[1].ammo;
        if (def->type == VT_MSV) {
            if (TEAM_MINES(tm) < s->slot[1].ammo) s->slot[1].ammo = TEAM_MINES(tm);
            TEAM_MINES(tm) -= (uint8_t)s->slot[1].ammo;
        }
        s->fuel = def->weapon[2].ammo << 16;
        if (game_hooks.hud) game_hooks.hud(GHUD_VEH_START, o->team, o);  /* HudSetSlot 5..9 */
        tm->cell_tick = g_tick;
        int8_t track = (int8_t)(def->music & 0xff);
        if (track < 0) music_counter[def->type] -= (uint8_t)(rand_range(0x32) + 0x19);
        else if (game_hooks.music && game_hooks.music(track, (def->music >> 8) & 0xff, o) >= 0) music_counter[def->type] = 100;
        burn_fuel(o, s);
    }
    s->ctrl = o->think(o);
    if (s->hook) {
        if (s->hook(o, s)) { tm->cell_tick = g_tick; return; }
        if (!G.running) return;
    }
    uint32_t old_h = o->heading;
    int32_t old_speed = o->speed;
    uint32_t *old_cell = o->cell;
    int32_t d[3];
    int32_t moved = s->move(d, o, def, s);
    o->flags &= ~OF_BLOCKED;
    if (moved == 0) {
        if (o->heading != old_h && obj_check_collision(o)) { o->heading = old_h; goto blocked; }
    } else {
        burn_fuel(o, s);
        if (obj_move(o, d)) {
            if (o->heading != old_h) {        /* retry without the turn */
                o->heading = old_h;
                if (!obj_move(o, d) || !obj_check_collision(o)) goto done;
                o->heading = old_h;
            }
            moved = 0;
            o->speed = -o->speed >> 2;        /* bounce back at a quarter speed */
blocked:
            o->flags |= OF_BLOCKED;
        }
    }
done:
    if (o->cell == old_cell) {
        if (tm->cell_tick + tun_idle_drone_ticks < g_tick) {
            tm->cell_tick = g_tick;
            if (G.drones[tm->index] < 3 && game_hooks.drone) game_hooks.drone(o);
        }
    } else tm->cell_tick = g_tick;
    if (s->fuel < def->weapon[2].ammo * 0x2000 && s->fuel_warn_tick < g_tick) {
        s->fuel_warn_tick = g_tick + 0x78;
        snd(1, SND_FUEL_WARN, o);
    }
    bool still = o->speed == 0 && ((o->heading ^ old_h) & 0xfffffff0u) == 0;
    if (tm->door_cell) { CELL_SET_TERRAIN(*tm->door_cell, tm->pad_terrain); tm->door_cell = NULL; }
    if (s->pickup_cell) resupply(o, def, s, !still);
    if ((*tm->in_cur & 0xe000000) == 0xe000000) {     /* all three buttons: self-destruct */
        Obj *w = veh_kill(o, s);
        if (w) w->flags |= 0xa000000;
        return;
    }
    if (still && CELL_TERRAIN(*o->cell) == tm->pad_terrain) {
        int32_t fx = (o->pos[0] & 0x1f0000) - 0x100000, fy = (o->pos[1] & 0x1f0000) - 0x100000, r = def->dock_radius;
        if (-r <= fx && fx <= r && -r <= fy && fy <= r) {
            if (!(s->ctrl & (CW_A_PRESS | CW_B_PRESS | CW_C_PRESS))) bunker_door_animate(o->team);
            else if (!def->dock_hook) { if (def->type != VT_HELI) dock_vehicle(o); }
            else s->hook = def->dock_hook;
            goto after_fire;
        }
    }
    if ((s->ctrl & 0xe0) && def->fire[0]) def->fire[0](o, def, s, def->fire_arg[0], s->ctrl);
    if ((s->ctrl & 0x700) && def->fire[1]) def->fire[1](o, def, s, def->fire_arg[1], s->ctrl >> 3);
    if ((s->ctrl & 0x3800) && def->fire[2]) def->fire[2](o, def, s, def->fire_arg[2], s->ctrl >> 6);
after_fire:
    if (s->water_hook) {
        s->water = water_state(o);
        s->water_hook(o, def, s, moved);
    }
    if (o->speed == old_speed) {
        if (o->flags & OF_SOUND) { snd(5, 0, o); o->flags &= ~OF_SOUND; }
    } else { o->flags |= OF_SOUND; snd(5, 0, o); }
    if (def->arrow_enable && tm->arrow_tick < g_tick) {
        Obj *e = G.cur_vehicle[o->team ^ 1];
        if (!e) tm->arrow_dir = -1;
        else if (!point_in_box(e->pos, o->pos, def->arrow_box)) {
            uint32_t v = ((angle_between(o->pos, e->pos) + 0x40000) & 0x380000) >> 19;
            tm->arrow_dir = (int16_t)(v > 7 ? 7 : v);
        } else tm->arrow_dir = 8;
        tm->arrow_tick = g_tick + 0x3c;
    }
}

const ObjClass class_vehicle = {
    1, OBJ_CLASS_NAME, vehicle_init, vehicle_destroy, vehicle_update, &gfx_44cd10, NULL, NULL,
    NULL, NULL, NULL, NULL, 0x64, vehicle_hit_static, vehicle_hit_object, veh_damage, NULL, 0
};

/* ------------------------------------------------------------------ Destroyed Vehicle */
static int wreck_think_expl(Obj *w);
static int wreck_think_wait(Obj *w);
static int wreck_think_coast(Obj *w);
static int bunker_flyback_timer(intptr_t a, intptr_t b, int32_t period);

static int wreck_init(const ObjClass *c, Obj *w, void *arg)   /* WreckInit 0x4292d0 */
{
    Obj *v = arg;
    VehState *vs = veh_state(v);
    const VehicleDef *def = vs->def;
    w->heading = v->heading;
    w->u70 = v->u70;
    w->gfx = def->gfx;
    w->u58 = 0;
    w->speed = v->speed;
    if (vs->fuel < 1) w->flags |= OF_SOUND;               /* out of fuel: no explosion */
    w->tm = v->tm; w->p60 = v->p60; w->u64 = v->u64; w->u68 = v->u68; w->u6c = v->u6c; w->u70 = v->u70;
    w->u6c = 0xc0000;
    w->u64 = g_tick;
    vs->wreck = w;
    if (w->tm) {
        w->tm->lift_or_wreck = w;
        if (w->tm->view)
            timer_add(def->respawn_delay, (w->flags & OF_SOUND) ? bunker_after_death : bunker_flyback_timer,
                      (intptr_t)w->team << 16, (intptr_t)w->tm->view);
    }
    w->think = wreck_think_expl;
    if (w->team < 2) { G.man_focus[w->team] = w; G.man_focus_id[w->team] = w->id; }   /* FUN_00406180 */
    (void)c;
    return 1;
}

static int wreck_think_expl(Obj *w)          /* 0x429740 */
{
    if (!(w->flags & OF_SOUND)) expl(w->team, w->pos, expl_tab_vehicle[veh_state(w)->def->type & 3]);   /* 0x44b0c8 */
    w->think = wreck_think_wait;
    w->u68 = 0;
    return 0;
}

static int wreck_think_wait(Obj *w)          /* 0x429790 */
{
    w->u68 += g_dt;
    if (w->u68 > 7) {
        const VehicleDef *def = veh_state(w)->def;
        w->gfx = (w->flags & OF_SOUND) ? def->gfx : def->gfx_wreck;
        if (w->gfx) { w->u68 = 0; w->think = wreck_think_coast; return 1; }
        w->think = NULL;
    }
    return 1;
}

static int wreck_think_coast(Obj *w)         /* Wreck_ThinkCoast 0x4297f0 */
{
    const VehicleDef *def = veh_state(w)->def;
    if (w->speed) w->speed = approach(w->speed, 0, def->wreck_friction * g_dt);
    if ((uint32_t)w->u64 + 0x2d0u < (uint32_t)g_tick) w->think = NULL;
    if (w->flags & OF_BLOCKED) {
        if (!w->gfx->shape) {                            /* burnt hulk at rest: becomes a Stay */
            Obj *st = spawn_stay(w->team, w->pos, w->gfx, 0x3c, 0x2d0);
            if (st) {
                st->heading = w->heading;
                w->think = NULL;
                obj_mark_delete(w);
                return 1;
            }
        } else if ((w->flags & OF_SOUND) && game_hooks.spawn_man) {   /* out of fuel: the crew bails out */
            Obj *m = game_hooks.spawn_man(w->team, w->pos[0], w->pos[1]);
            if (m) {
                if (m->gfx && m->gfx->shape) {
                    int32_t p[3] = { m->pos[0], m->pos[1], m->pos[2] };
                    int k = (int)(((w->heading - 0x100000) & 0x3f0000) >> 16);
                    while (shapes_overlap(w->pos, w->heading, w->gfx->shape, p, m->heading, m->gfx->shape)) {
                        p[0] += dir_vec[k][0] * 3;
                        p[1] += dir_vec[k][1] * 3;
                    }
                    int32_t d[3] = { p[0] - m->pos[0], p[1] - m->pos[1], 0 };
                    obj_move(m, d);
                }
                w->flags &= ~OF_SOUND;
            }
        }
    }
    return 1;
}

static Obj *wreck_bursting;                  /* DAT_00459618 */
static void wreck_debris_cb(Obj *piece, int n)   /* FUN_00429670: pieces pushed along the hulk's heading */
{
    (void)n;
    if (!wreck_bursting) return;
    DebrisExt *x = &debris_ext[piece->id & 0x1ff];
    const int32_t *v = dir_vec[(int32_t)wreck_bursting->heading >> 16];
    x->vel[0] += fix_mul(v[0], piece->speed);
    x->vel[1] += fix_mul(v[1], piece->speed);
}

static void wreck_update(const ObjClass *c, Obj *w)   /* WreckUpdate 0x4293e0 */
{
    (void)c;
    int ws = water_state(w);                             /* DAT_00459614 */
    const VehicleDef *def = veh_state(w)->def;
    if (!w->think) { obj_destroy_now(w); return; }
    w->think(w);
    if (w->speed == 0 && w->pos[2] < 1 && ws != 2) { w->flags |= OF_BLOCKED; return; }
    int32_t d[3];
    w->u58 -= tun_gravity * g_dt;                        /* FUN_004337d0: fall speed in +0x58 */
    if (w->u58 < tun_fall_max) w->u58 = tun_fall_max;
    d[2] = w->u58 * g_dt;
    int32_t k = w->speed * g_dt;
    const int32_t *v = dir_vec[(int32_t)w->heading >> 16];
    d[0] = fix_mul(k, v[0]);
    d[1] = fix_mul(k, v[1]);
    if (w->pos[2] < 1) { if (ws != 2) d[2] = 0; }
    else if (w->pos[2] <= -d[2]) { d[2] = -w->pos[2]; w->flags |= OF_NEW; }   /* a falling hulk hits the ground */
    const Gfx *keep = w->gfx;
    if (!w->gfx->shape) w->gfx = def->gfx;               /* collide with the live hull */
    obj_move(w, d);
    if (def) {
        if (w->pos[2] < 0) keep = def->gfx_sink;
        if ((w->pos[2] >> 16) <= -(def->sink_depth >> 16)) {
            w->pos[2] = 0x10000 - (int32_t)((uint32_t)def->sink_depth & 0xffff0000u);
            obj_mark_delete(w);
            spawn_explosion(w->team, w->pos[0], w->pos[1], 0, 0x454300, NULL);
            return;
        }
    }
    if (ws == 2 && water_state(w) != 2 && g_move_old_pos) {  /* keep sinking where the water is deep */
        g_move_old_pos[2] = w->pos[2];
        obj_revert_move(w);
    }
    if (w->flags & OF_NEW) {                             /* explode into debris */
        spawn_explosion(w->team, w->pos[0], w->pos[1], w->pos[2], expl_tab_vehicle[def->type & 3], NULL);
        const DebrisDef *const *set = w->pos[2] < 0xa0001 ? debris_sets[5] : debris_sets[1];   /* 0x454e68 / 0x454e28 */
        wreck_bursting = w;
        spawn_debris_burst(w->pos, w->team, set, model_part_by_addr(w->gfx->addr), w->heading, w->u68, wreck_debris_cb);
        wreck_bursting = NULL;
        if (ws != 0) {
            spawn_explosion(w->team, w->pos[0], w->pos[1], 0, 0x453a30, NULL);
            obj_mark_delete(w);
            w->gfx = keep;
            return;
        }
        if (w->pos[2] > 0) { obj_mark_delete(w); w->gfx = keep; return; }
        if (def->gfx_wreck2) { w->flags &= ~OF_NEW; w->gfx = def->gfx_wreck2; return; }
        obj_mark_delete(w);
    }
    w->gfx = keep;
}

static int wreck_destroy(const ObjClass *c, Obj *w) { (void)c; (void)w; return 1; }
static uint32_t wreck_hit_static(Obj *w, uint32_t *cell, int bno)     /* 0x429990 */
{
    bno_damage(0x220000, w, cell, &bno_defs[bno]);
    if (w->pos[2] > 0x10000) w->flags |= OF_NEW;
    return 1;
}
static int wreck_damage(Obj *w, Obj *src, int32_t amount)             /* 0x4296d0 */
{
    w->u6c -= amount;
    if (w->u6c > 0) return 0;
    if (src && src->cls->id == 5) { expl(w->team, w->pos, expl_tab_vehicle[veh_state(w)->def->type & 3]); obj_mark_delete(w); return 2; }
    w->flags |= OF_NEW;
    return 2;
}

const ObjClass class_wreck = {
    6, OBJ_CLASS_NAME, wreck_init, wreck_destroy, wreck_update, &gfx_44cd10, NULL, NULL,
    NULL, NULL, NULL, NULL, 0x68, wreck_hit_static, NULL, wreck_damage, NULL, 0
};

/* ------------------------------------------------------------------ Storage (bunker lift) */
typedef struct { const Gfx *gfx; int32_t type; uint32_t heading; Team *tm; ThinkFn think; int32_t ox, oy; } StorageArgs;

static int st_wait_clear(Obj *o);
static int st_wait_release(Obj *o);
static int st_rise(Obj *o);
static int st_lower2(Obj *o);

static int storage_init(const ObjClass *c, Obj *o, void *argp)    /* StorageInit 0x417f50 */
{
    StorageArgs *a = argp;
    o->speed = 0;
    o->p60 = (void *)a->gfx;
    o->u68 = 0;
    o->tm = a->tm;
    o->u6c = a->type;
    o->heading = a->heading;
    o->u58 = (a->ox & 0xff) | ((a->oy & 0xff) << 8);
    a->tm->lift_or_wreck = NULL;
    o->u70 = 1;
    if (a->think) o->think = a->think;
    (void)c;
    return 1;
}

static int st_think_cam(Obj *o) { o->think = st_wait_clear; return 0; }          /* 0x4181b0 (camera) */
static int st_wait_clear(Obj *o)                                                  /* 0x418200 */
{
    if (!G.storage_blocked) o->think = st_wait_release;
    return 0;
}
static int st_wait_release(Obj *o)                                                /* 0x418250 */
{
    if (o->u70) return 0;
    CELL_SET_TERRAIN(*o->cell, TERR_LIFT);
    o->pos[2] = -0x200000;
    o->think = st_rise;
    return 0;
}
static int st_rise(Obj *o)                                                        /* 0x418280 */
{
    if (o->pos[2] >= 0) { o->think = NULL; return 1; }
    o->pos[2] += g_dt * 0x4ccc;
    if (o->pos[2] > 0) o->pos[2] = 0;
    return 1;
}
static int st_lower(Obj *o)                                                       /* 0x418320 */
{
    if (o->pos[2] > -0x100001) {
        o->pos[2] -= g_dt * 0x4ccc;
        o->u68 -= g_dt; if (o->u68 < 0) o->u68 = 0;
        return 0;
    }
    if (o->tm && o->tm->view) o->tm->view->mode = BV_FADE_TO_SELECT;
    o->think = st_lower2;
    return st_lower2(o);
}
static int st_lower2(Obj *o)                                                      /* 0x418400 */
{
    o->pos[2] -= g_dt * 0x4ccc;
    o->u68 -= g_dt; if (o->u68 < 0) o->u68 = 0;
    if (o->pos[2] < -0x1fffff && o->u68 < 1) { o->pos[2] = -0x200000; obj_mark_delete(o); }
    return 0;
}

static void storage_update(const ObjClass *c, Obj *o)             /* StorageUpdate 0x417fb0 */
{
    (void)c;
    static const int nb[9] = { -129, -128, -127, -1, 0, 1, 127, 128, 129 };   /* the 3x3 neighbourhood of a cell (0x4436d8) */
    G.storage_blocked = 0;
    obj_check_collision(o);
    if (o->cell != &G.outside)
        for (int i = 0; i < 9; i++) {
            int ci = cell_index(o->cell) + nb[i];
            if (ci < 0 || ci >= 128 * 128) continue;
            for (Obj *p = obj_from_slot(CELL_HEAD(G.cell[ci])); p; p = p->cnext) {
                if (p->cls->id == 10 && dist_sq_px(o->pos, p->pos) < 400) {    /* mines on the lift */
                    p->flags |= OF_KILLED_BY_STORAGE; obj_mark_delete(p); G.storage_blocked = 1;
                }
                else if (p->cls->id == 12) flag_on_lift(o, p);             /* own flag re-hidden / enemy flag: TeamWins */
                else if (p->cls->id == 17 && p->gfx) {                     /* remains on the lift are blown away */
                    int32_t r = p->gfx->radius >> 16, lim = r + 16;
                    if (dist_sq_px(o->pos, p->pos) < lim * lim) {
                        obj_mark_delete(p);
                        spawn_explosion(p->team, p->pos[0], p->pos[1], 0, r < 16 ? 0x453b58 : 0x453c58, NULL);
                    }
                }
            }
        }
    if (!o->think) {
        CELL_SET_TERRAIN(*o->cell, o->team == 0 ? TERR_PAD0 : TERR_PAD1);
        spawn_vehicle(o->team, o->pos, o->heading, o->u6c);
        obj_destroy_now(o);
    } else if (o->think(o) == 1) o->u68 += g_dt;
}

static int storage_destroy(const ObjClass *c, Obj *o)             /* StorageDestroy 0x418180 */
{
    (void)c;
    CELL_SET_TERRAIN(*o->cell, o->team == 0 ? TERR_PAD0 : TERR_PAD1);
    return 1;
}

static uint32_t storage_hit_object(Obj *o, Obj *other)            /* 0x4182e0: crush whatever is on the lift */
{
    if (other->cls->damage) other->cls->damage(other, o, 0x40000);
    else obj_mark_delete(other);
    G.storage_blocked = 1;
    return 0;
}

const ObjClass class_storage_up = {
    5, OBJ_CLASS_NAME, storage_init, storage_destroy, storage_update, &gfx_4431f8, NULL, st_think_cam,
    NULL, NULL, NULL, NULL, 0x6a, NULL, storage_hit_object, NULL, NULL, 0
};
const ObjClass class_storage_down = {
    5, OBJ_CLASS_NAME, storage_init, storage_destroy, storage_update, &gfx_4431f8, NULL, st_lower,
    NULL, NULL, NULL, NULL, 0x6a, NULL, storage_hit_object, NULL, NULL, 0
};

static const Gfx *lift_display(int type) { return type == VT_HELI ? &gfx_44ee68 : vehicle_defs[type].gfx; }

Obj *spawn_lift_vehicle(int team, int type)                        /* SpawnLiftVehicle 0x428020 */
{
    Team *t = &G.teams[team];
    StorageArgs a = { lift_display(type), type, type == VT_HELI ? 0x180000u : 0x200000u, t, NULL, 0, 0 };
    return obj_create(&class_storage_up, team, t->pad->x, t->pad->y, -0x200000, &a);
}

Obj *spawn_vehicle(int team, const int32_t *pos, uint32_t heading, int type)   /* SpawnVehicle 0x427cd0 */
{
    if (type < 0) return NULL;
    if (type > 3) type = 0;
    VehArgs a = { type, heading, &G.teams[team] };
    return obj_create(&class_vehicle, team, pos[0], pos[1], 0, &a);   /* + ViewAddTracker (camera) */
}

Obj *dock_vehicle(Obj *v)                                          /* DockVehicleInBunker 0x418470 */
{
    VehState *s = veh_state(v);
    const VehicleDef *def = s->def;
    Team *t = v->tm;
    StorageArgs a = { lift_display(def->type), def->type, v->heading, t, NULL,
                      (int32_t)((v->pos[0] & 0x1f0000) >> 16) - 16, (int32_t)((v->pos[1] & 0x1f0000) >> 16) - 16 };
    if (def->type == VT_MSV) TEAM_MINES(t) += (uint8_t)s->slot[1].ammo;
    uint8_t *st = &TEAM_STOCK(t, def->type);
    if (*st != 0xff) (*st)++;
    int32_t cp[3];
    cell_to_pos(v->cell, cp);
    Obj *l = obj_create(&class_storage_down, v->team, cp[0], cp[1], 0, &a);
    if (!l) return NULL;
    l->pos[2] = 0;
    obj_mark_delete(v);
    CELL_SET_TERRAIN(*l->cell, TERR_LIFT);
    l->think = st_lower;
    l->u68 = 0x46;
    snd(1, SND_DOCK, l);
    return l;
}

void bunker_door_animate(int team)                                 /* BunkerDoorAnimate 0x427f10 */
{
    G.door_anim_active = 1;
    G.door_anim_acc[team] += g_dt * tun_door_anim_rate;
    while (G.door_anim_acc[team] > 0xffff) G.door_anim_acc[team] -= 0x10000;   /* rotates the door sprite colours */
    PadRec *p = G.teams[team].pad;
    if (p && p->cell) *p->cell = (*p->cell & 0xffffecffu) | 0x2c80;             /* BNO_STORAGE_LIGHT (89) */
}

void bunker_door_reset(void)                                       /* FUN_00427fe0 */
{
    if (!G.door_anim_active) return;
    for (int t = 0; t < (G.nplayers > 1 ? 2 : 1); t++)
        if (G.teams[t].pad && G.teams[t].pad->cell) *G.teams[t].pad->cell &= 0xffffc07fu;
    G.door_anim_active = 0;
}

Obj *get_team_vehicle(int team)                                    /* GetTeamVehicle 0x427ed0 */
{
    if (team > 2 || team < 0 || team > 1) return NULL;
    Obj *v = G.cur_vehicle[team];
    return v && G.cur_vehicle_id[team] == v->id ? v : NULL;
}

/* FUN_00427d70: 3 = can still win (jeeps left or driving one), 0 = lost (1P), 1/2 = 2P spectate. */
int team_status(int team)
{
    int t = team != 0;
    int n = TEAM_STOCK(&G.teams[t], VT_JEEP);
    Obj *v = get_team_vehicle(t);
    if (v && v->cls->id == 1 && veh_type(v) == VT_JEEP) n++;
    if (n) return 3;
    if (G.nplayers < 2) return 0;
    t = team == 0;
    n = TEAM_STOCK(&G.teams[t], VT_JEEP);
    v = get_team_vehicle(t);
    if (v && v->cls->id == 1 && veh_type(v) == VT_JEEP) n++;
    if (!n) return 0;
    Team *o = &G.teams[team != 0];
    for (int k = 0; k < 4; k++) if (TEAM_STOCK(o, k)) return 2;
    return 1;
}

/* FUN_00404ee0: camera flies back to the bunker (both death timers end here). */
static void bunker_start_flyback(BunkerView *v)
{
    v->mode = BV_FLYBACK; v->flyback_until = g_tick + 0x1e; v->fly_stage = 0;
    v->cam_d0 = 0; v->cam_d8 = 0; v->cam_d4 = 0x28f;
}

/* FUN_004280d0 (timer): back to the bunker after running out of fuel / sinking. */
static int bunker_after_death(intptr_t a, intptr_t b, int32_t period)
{
    BunkerView *v = (BunkerView *)b;
    (void)a; (void)period;
    int st = team_status(v->team);
    if (st > 1 && v->last_type != VT_JEEP) { v->mode = BV_FADE_TO_SELECT; return -1; }
    bunker_start_flyback(v);
    return -1;
}

/* FUN_004280b0 (timer): destroyed in combat -> camera flies back to the bunker (FUN_00404ee0). */
static int bunker_flyback_timer(intptr_t a, intptr_t b, int32_t period)
{
    BunkerView *v = (BunkerView *)b;
    (void)a; (void)period;
    bunker_start_flyback(v);
    return -1;
}

/* ------------------------------------------------------------------ bunker view (vehicle select) */
void bunker_enter_select(BunkerView *v)                            /* ViewEnterBunkerSelect 0x404d30 */
{
    v->sel = 0xff;
    for (int i = 0; i < 4; i++) if (TEAM_STOCK(v->tm, i)) { v->sel = i; break; }
    if (v->sel == 0xff) v->sel = 0;
    v->mode = BV_SELECT;
    v->cam_d0 = 0x6e0000;
    v->fade = 0; v->cam_d8 = 0; v->cam_d4 = 0;
    v->step = NULL;
    if (game_hooks.hud) game_hooks.hud(GHUD_SELECT, v->team, NULL);
}

static int nav_pick(const Team *t, int cur, int dir, int alt0, int alt1)
{
    int n = bunker_menu_nav[cur][dir];
    if (TEAM_STOCK(t, n) == 0) {
        int m = bunker_menu_nav[n][alt0];
        if (m == n) m = bunker_menu_nav[n][alt1];
        n = TEAM_STOCK(t, m) ? m : -1;
    }
    return n;
}

static int run_step(BunkerView *v, const BunkerStep *s)            /* zoom-script steps 0x4042b0..0x404530 */
{
    int32_t v1;
    switch (s->fn) {
    case 0x4042b0:                                                  /* sound (PreRaise/Raise with pitch ramp) */
        if (game_hooks.sound_step) game_hooks.sound_step(s->a, s->b, s->c);
        return 1;
    case 0x4043a0:                                                  /* HUD: dashboard slides in */
        if (game_hooks.hud) game_hooks.hud(GHUD_DASH, v->team, NULL);
        return 1;
    case 0x404530:
        v->cam_d0 -= s->a * g_dt;
        if (v->cam_d0 <= s->b) { v->cam_d0 = s->b; return 1; }
        return 0;
    case 0x4044e0:
        v1 = g_dt * s->a + v->cam_d4;
        if (s->a < 0) { if (v1 < s->b) return 1; }
        else if (s->b < v1) return 1;
        v->cam_d4 = v1;
        return 0;
    case 0x404490: {
        int32_t st = s->a * g_dt, n = v->cam_d0 - st;
        if (n <= s->b) st = v->cam_d0 - s->b;
        v->cam_d8 -= st; v->cam_d0 -= st;
        return n <= s->b;
    }
    case 0x404430:
        v->cam_d0 -= s->a * g_dt; v->cam_d8 -= s->a * g_dt;
        v->fade -= s->b * g_dt;
        if (v->fade > 0) return 0;
        v->fade = 0;
        if (v->lift) v->lift->u70 = 0;                              /* release the lift */
        return 1;
    }
    return 1;
}

void bunker_view_update(BunkerView *v)
{
    uint32_t raw = *v->tm->in_cur, prv = *v->tm->in_prev;
    switch (v->mode) {
    case BV_SELECT: {                                               /* ViewModeBunkerSelect 0x404570 */
        if (v->fade < 0x10000 && (v->fade += g_dt * 0x11eb) > 0x10000) v->fade = 0x10000;
        int cur = v->sel, n = cur;
        if (raw & 0xc0000000u) {                                    /* repeats every frame while held */
            n = nav_pick(v->tm, cur, (raw & 0x40000000) ? 0 : 1, 2, 3);
            if (n < 0) n = cur;
            v->sel = n;
        }
        if (raw & 0x30000000) {
            int m = nav_pick(v->tm, v->sel, (raw & 0x10000000) ? 2 : 3, 0, 1);
            if (m < 0) m = n;
            v->sel = m;
        }
        if (v->sel != cur && game_hooks.sound) game_hooks.sound(1, 0x43ea80, NULL);
        /* B/C with full fade open the map/status screen (FUN_00404b70): not ported */
        if ((raw & 0x8000000) && !(prv & 0x8000000) && v->fade > 0xffff) {
            v->mode = BV_LAUNCH;
            v->step = bunker_scripts[v->sel];
            v->lift = spawn_lift_vehicle(v->team, v->sel);
        }
        break;
    }
    case BV_LAUNCH:                                                 /* FUN_00404ae0 */
        while (v->step->fn) {
            const BunkerStep *s = v->step;
            if (!run_step(v, s)) return;
            v->step = s + 1;
            if (!s->cont) return;
        }
        v->mode = BV_ZOOM_IN;
        v->fade = 0;
        break;
    case BV_ZOOM_IN:                                                /* ViewModeZoomIn 0x404e80 */
        v->fade += g_dt * 0x11eb;
        if (v->fade > 0x10000) { v->mode = BV_PLAYING; v->fade = 0x10000; }
        break;
    case BV_PLAYING: break;                                         /* RenderWorldView */
    case BV_FADE_TO_SELECT:                                         /* FUN_00404e30 */
        v->fade -= g_dt * 0x11eb;
        if (v->fade < 1) bunker_enter_select(v);
        break;
    case BV_FLYBACK:                                                /* 0x404f70 -> 0x4052d0 -> 0x405360 -> 0x405440 */
        if (v->fly_stage == 0) { if ((uint32_t)v->flyback_until < (uint32_t)g_tick) v->fly_stage = 1; }
        else if (v->fly_stage == 1) {
            v->fade -= g_dt * 0x51e;
            if (v->fade < 0) v->fade = 0;
            if (v->fade < 1) {
                v->fade = 0; v->fly_stage = 2;
                if (game_hooks.sound) game_hooks.sound(1, 0x43ebd0, NULL);   /* Laugh (0x4052d0) */
            }
        } else if (v->fly_stage == 2) {
            v->cam_d8 += 0x4ccc * g_dt;
            if (v->cam_d8 > 0x3bffff) {
                int st = team_status(v->team);
                if (st == 0) { G.winner = -1; if (game_hooks.end_game) game_hooks.end_game(-1); v->fly_stage = 9; }
                else if (st == 1) {                                 /* 2P: nothing left, the other side has jeeps */
                    v->cam_d8 = 0x3bffff; v->fade = 0x10000; v->sel = -0x10000;
                    v->mode = BV_SPECTATE; v->fly_stage = 0;        /* FUN_004054f0 */
                }
                else { v->cam_d8 = 0; v->fade = 0x10000; v->fly_stage = 3; }
            }
        } else if (v->fly_stage == 3) {
            v->fade -= g_dt * 0x11eb;
            if (v->fade < 1) { v->fade = 0; bunker_enter_select(v); }
        }
        break;
    case BV_SPECTATE:                                               /* 0x4054f0 -> 0x405590 -> 0x405710 */
        if (v->fly_stage == 0) {                                    /* +0xcc counts up from -1.0 (icon fade) */
            v->sel += g_dt * 0x51e;
            if (v->sel > -1) { v->sel = 0; v->last_type = 0; v->fly_stage = 1; v->redraw = 2; v->fade = 0; }
        } else if (v->fly_stage == 1) {                             /* the fade cel lightens to 1.0 */
            v->fade += g_dt * 0x51e;
            if (v->fade > 0xffff) { v->fade = 0x10000; v->redraw = 2; v->fly_stage = 2; }
        }                                                           /* 0x405710: static until the game ends */
        break;
    }
}
