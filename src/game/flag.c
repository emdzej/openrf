/* The flag: class Flag (12, descriptor 0x44ae30): FlagInit 0x424320, FlagDestroy 0x424350, FlagUpdate 0x424380,
   hit_object FlagOnTouch 0x424760; DestFlagOnTouch 0x4247e0 (BNO 63 touch handler), the jeep's C button
   FUN_004248a0 (drop / pick up), the Flag part of FlagBuildingDestroyed 0x424170 (the rest is in collide.c)
   and the Storage (lift) check of StorageUpdate 0x417fb0. The win check is JeepFlagCheck 0x42a480
   (vehicle.c): a jeep carrying the enemy flag on its own pad terrain. */
#include "rules.h"
#include "vehicle.h"
#include "weapon.h"
#include <string.h>

FlagExt flag_ext[OBJ_POOL];
int32_t flag_home[2][3];                    /* DAT_004588b0 + team*12 */
uint32_t rules_music_bits;                  /* _DAT_00480e3c (bits 0x100 / 0x200) */
RulesHooks rules_hooks;
#define EXT(o) (&flag_ext[(o)->id & 0x1ff])
enum { SND_DING = 0x43eab0 };               /* *PTR_PTR_0043ee08: the pickup sound */

static void snd(uint32_t ev, Obj *o) { if (game_hooks.sound) game_hooks.sound(1, ev, o); }

static int flag_init(const ObjClass *c, Obj *o, void *arg)             /* FlagInit 0x424320 */
{
    (void)c; (void)arg;
    FLAG_WAVE(o) = 0;
    FLAG_BLIP(o) = 0;
    EXT(o)->touch = NULL;
    FLAG_BLINK(o) = g_tick + 0xf;
    EXT(o)->dropped = NULL;
    o->heading = 0x100000;
    return 1;
}

static int flag_destroy(const ObjClass *c, Obj *o)                     /* FlagDestroy 0x424350 */
{
    (void)c;
    if (o->team >= 0 && o->team < 2 && G.flag_obj[o->team] == o) G.flag_obj[o->team] = NULL;
    return 1;
}

static bool carrying(const Obj *jeep)
{
    for (int k = 0; k < 2; k++)
        if (G.flag_obj[k] && G.flag_obj[k]->parent == jeep) return true;
    return false;
}

static int pick_up(Obj *f, Obj *jeep)
{
    if (carrying(jeep)) return 0;
    snd(SND_DING, jeep);
    obj_set_parent(f, jeep);
    obj_insert_after(jeep, f);                                           /* FUN_0040b5a0: updated right after it */
    return 1;
}

static bool is_jeep(const Obj *o) { return o->cls->id == 1 && o->p60 && veh_state((Obj *)o)->def->type == VT_JEEP; }

static uint32_t flag_hit_object(Obj *o, Obj *other)                   /* FlagOnTouch 0x424760 */
{
    if (is_jeep(other)) {
        EXT(o)->touch = other;
        if (!EXT(o)->dropped && !o->parent) pick_up(o, other);
    }
    return 0;
}

int flag_touch_dest_building(Obj *o, uint32_t *cell)                  /* DestFlagOnTouch 0x4247e0 */
{
    if (!is_jeep(o)) return 0;
    for (int k = 0; k < 2; k++) {
        Obj *f = G.flag_obj[k];
        if (!f || f->cell != cell || f->parent) continue;
        EXT(f)->touch = o;
        if (!EXT(f)->dropped && !f->parent) pick_up(f, o);
        return 0;
    }
    return 0;
}

static void flag_update(const ObjClass *c, Obj *o)                    /* FlagUpdate 0x424380 */
{
    (void)c;
    int team = o->team & 1;
    if (FLAG_BLINK(o) <= g_tick) {                                       /* radar blip 0x44e7a8 <-> 0x44e7b8 */
        FLAG_BLIP(o) = !FLAG_BLIP(o);
        FLAG_BLINK(o) = g_tick + 0xf;
    }
    Obj *j = o->parent;
    bool waving;
    if (!j) {
        if (water_state(o) == 0) {
            if (o->pos[2] != 0) { int32_t d[3] = { 0, 0, -o->pos[2] }; obj_move(o, d); }
            o->speed = 0;
            if (cell_water_type(*o->cell) == 0) memcpy(flag_home[team], o->pos, sizeof flag_home[team]);
        } else {                                                         /* in water: drifts back to the last dry spot */
            int k = (int)(angle_between(o->pos, flag_home[team]) >> 16);
            o->speed = approach(o->speed, 0x1999, g_dt * 0x36);
            int32_t d[3] = { fix_mul(dir_vec[k][0], o->speed) * g_dt, fix_mul(dir_vec[k][1], o->speed) * g_dt,
                             o->pos[2] ? -o->pos[2] : 0 };
            obj_move(o, d);
        }
        o->heading = 0;
        waving = true;
        o->gfx = &rgfx_44e740;
        FLAG_TARGET(o) = 0;
    } else {                                                             /* carried */
        if (cell_water_type(*o->cell) == 0) memcpy(flag_home[team], o->pos, sizeof flag_home[team]);
        if (o->team != j->team) rules_music_bits |= 0x200;               /* Flag Pickup music */
        int32_t h = (int32_t)j->heading;
        if (h < 0x471c7) h = 0x471c7;
        else if (h > 0x3b8e39) h = 0x3b8e39;
        if (h < 0x200000) { if (h > 0x1b8e38) h = 0x1b8e38; }
        else if (h < 0x2471c7) h = 0x2471c7;
        FLAG_TARGET(o) = h;                                              /* never quite north or south */
        waving = j->speed > 0;
        o->gfx = &rgfx_44e610;
        int32_t d[3];
        vec_mul_mat3(d, flag_carry_offset, rot_mat[j->heading >> 16]);
        d[0] += j->pos[0] - o->pos[0];
        d[1] += j->pos[1] - o->pos[1];
        d[2] += j->pos[2] - o->pos[2];
        obj_move(o, d);
    }
    /* heading towards the target at 0x4ccc per tick (inline TurnTowardsAngle) */
    uint32_t cur = o->heading, tgt = (uint32_t)FLAG_TARGET(o), diff = (tgt - cur) & ANG_MASK;
    if (diff) {
        uint32_t n;
        if (((0u - diff) & ANG_MASK) < diff) {
            n = (cur - (uint32_t)g_dt * 0x4ccc) & ANG_MASK;
            uint32_t r = (tgt - n) & ANG_MASK;
            if (r < ((0u - r) & ANG_MASK)) n = tgt;
        } else {
            n = (cur + (uint32_t)g_dt * 0x4ccc) & ANG_MASK;
            uint32_t r = (tgt - n) & ANG_MASK;
            if (((0u - r) & ANG_MASK) < r) n = tgt;
        }
        o->heading = n;
    }
    int32_t step = g_dt * 0x2aaa, w = FLAG_WAVE(o);                      /* cloth: 0..3 up, 4..9 loop, 10..12 down */
    if (waving) {
        if (w < 0xa0000) {
            w += step;
            FLAG_WAVE(o) = w;
            if (w > 0x9ffff) FLAG_WAVE(o) = w - (int32_t)(((uint32_t)(w - 0x40000) / 0x60000u) * 0x60000u);
        } else {
            w -= step;
            FLAG_WAVE(o) = w;
            if (w < 0x40000) FLAG_WAVE(o) = (int32_t)(((0x9ffffu - (uint32_t)w) / 0x60000u) * 0x60000u) + w;
        }
    } else if (w != 0) {
        w += step;
        FLAG_WAVE(o) = w > 0xcffff ? 0 : w;
    }
    FlagExt *x = EXT(o);
    if (x->dropped) {                                                    /* no pickup until that jeep has left */
        if (o->parent) { x->dropped = NULL; return; }
        x->touch = NULL;
        obj_check_collision(o);
        if (x->touch != x->dropped) x->dropped = NULL;
    }
}

const ObjClass class_flag = {
    12, OBJ_CLASS_NAME, flag_init, flag_destroy, flag_update, &rgfx_44e740, NULL, NULL,
    NULL, NULL, NULL, NULL, 0xc9, NULL, flag_hit_object, NULL, NULL, 0
};

/* FUN_004248a0 (jeep fire C, 0x42ab70): drop the carried flag, or pick up one being touched (own first). */
int flag_toggle(Obj *jeep)
{
    int t = jeep->team & 1;
    for (int k = 0; k < 2; k++) {
        Obj *f = G.flag_obj[k ? t ^ 1 : t];
        if (!f) { if (k) return 0; continue; }
        if (f->parent == jeep) {                                         /* Mus_ClearOwner(jeep) */
            obj_set_parent(f, NULL);
            EXT(f)->dropped = jeep;
            return -1;
        }
        if (f->parent) { if (k) return 0; continue; }
        EXT(f)->touch = NULL;
        obj_check_collision(f);
        if (EXT(f)->touch == jeep && pick_up(f, jeep)) return 1;
    }
    return 0;
}

/* StorageUpdate 0x417fb0: a flag in the 3x3 cells round a lift: own flag -> hidden again, enemy flag -> win. */
void flag_on_lift(Obj *lift, Obj *f)
{
    if (f->team == lift->team) {
        snd(SND_DING, NULL);
        hide_flag_in_building(f->team);
    } else rules_end_game(lift->team);
}

/* FlagBuildingDestroyed 0x424170, once the flag must show: the Flag object with its radar blip
   (FUN_00422c70(flag, 0x44e7a8, 10000)), its home position, the Flag Discovery music bit and, when the
   other side's vehicle is within 320 px, a 120-tick camera tracker on the flag for that side. */
Obj *flag_spawn(int team, const int32_t *pos)
{
    Obj *f = obj_create(&class_flag, team, pos[0], pos[1], 0, NULL);
    if (!f) return NULL;
    FLAG_BLIP(f) = 1;
    G.flag_obj[team] = f;
    memcpy(flag_home[team & 1], f->pos, sizeof flag_home[0]);
    rules_music_bits |= 0x100;
    int other = f->team == 0;
    Obj *v = get_team_vehicle(other);
    if (v && dist2d(v->pos, f->pos) < 0x1400000 && rules_hooks.flag_camera) rules_hooks.flag_camera(other, f);
    return f;
}

void rules_end_game(int winner)                                          /* TeamWins 0x427e80 -> EndGame 0x40f380 */
{
    G.winner = winner;
    if (game_hooks.end_game) game_hooks.end_game(winner);
}
