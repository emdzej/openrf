/* Projectiles: FireProjectile 0x431760 and the type table 0x450a78, classes Missle (0) / TRACER (7) /
   Death Missle (16) (ProjectileInit/Destroy/Update 0x430bb0..0x430dd0, 0x4311d0, 0x4313e0), Grenade (18,
   0x4319c0..0x431de0) and Mine (10, 0x434e40..0x434fd0). See docs/game.md §6. */
#pragma once
#include "game.h"
#include "vehicle.h"

typedef struct ProjType {
    const ObjClass *cls;        /* +0x00 */
    int32_t index;              /* +0x04 */
    uint32_t flags;             /* +0x08 bit0: launch offset rotated by yaw/pitch in x/y only;
                                           bit1: pitch matrix built with FUN_00408400 (angle in 1/0x1000000 turns) */
    int32_t speed;              /* +0x0c px/tick */
    uint32_t snd;               /* +0x18 launch sound event (VA) */
    int impact;                 /* +0x1c 0x431100: explosion from +0x34 by hit kind */
    int32_t damage;             /* +0x24 */
    int32_t pitch_rate;         /* +0x28 gravity: pitch turned towards 0 (or added, flag 2) per tick */
    const Gfx *gfx, *shadow;    /* +0x2c model (+swept shape), +0x30 shadow model */
    const uint32_t *expl;       /* +0x34 explosion descriptors by hit kind */
    uint8_t life, nframes, period, splash;  /* +0x38 ticks, +0x39/+0x3a animation, +0x3b shallow-water radius (px) */
} ProjType;
extern ProjType proj_types[12];                   /* game_tables.c, filled by exe_load() */
enum { HIT_GROUND, HIT_WATER, HIT_ROAD, HIT_OBJECT, HIT_STATIC };   /* obj+0x73 / grenade +0x58 */

/* FireProjectile 0x431760: offset rotated as the type says, ObjCreate(type->cls) with {owner, type,
   heading, pitch}. Returns the projectile (NULL for TRACER, whose init probes the line and frees itself). */
Obj *fire_projectile(const int32_t *pos, const int32_t *off, uint32_t heading, int32_t pitch, int type, int team, Obj *owner);
void proj_add_speed(Obj *p, int32_t ds);                                   /* FUN_00431880 */
extern int tracer_result;                                                  /* DAT_0046f3c4 */
Obj *throw_grenade(Obj *o, const int32_t *off, const int32_t *target);    /* ThrowGrenade 0x431c80 */
Obj *jeep_throw_grenade(Obj *o, const int32_t *off, const VehicleDef *def, VehState *s); /* FUN_00431de0 */
Obj *spawn_mine(int32_t x, int32_t y);                                    /* SpawnMine 0x434fd0 */
int water_state_box(const int32_t *pos, const int32_t *box, int want);   /* FUN_00418910 */
int32_t dist2d(const int32_t *a, const int32_t *b);                       /* FUN_0041ed20 */
int32_t dist3d(const int32_t *a, const int32_t *b);                       /* FUN_0041ece0 */

extern const ObjClass class_missle, class_tracer, class_death_missle, class_grenade, class_mine;

/* weapon geometry (game_tables.c) */
extern int32_t muzzle_tank[2][3], msv_muzzle_raw[16], muzzle_heli[2][6];
extern int32_t tun_msv_pitch_lob, tun_msv_pitch_flat, tun_dmissile_turn, tun_dmissile_pitch;
extern int32_t tun_gravity, tun_fall_max, tun_bridge[7], bridge_debris_verts[2][4][3];

/* shared helpers (fixmath.c) */
void mat_mul3(int32_t *out, const int32_t *a, const int32_t *b);          /* Mat_Mul3x3 0x438460 */
void mat_rot_x(int32_t *m, int32_t a);                                     /* FUN_00408400 into a zero matrix */
extern int32_t pitch_mat[64][9];                                           /* 0x48a7b0 */
extern int32_t recip_tab[256];                                             /* 0x482080: [k-1] = 1/k */
int32_t fix_div(int32_t a, int32_t b);                                     /* FixDiv 0x438390 */
int32_t fix_sqrt(int32_t a);                                               /* FixSqrt 0x438580 */
int32_t isqrt_fixed(int32_t a);                                            /* IsqrtFixed 0x438550 */
