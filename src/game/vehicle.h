/* Player vehicles: descriptors (0x44afc8), per-team vehicle state (0x459390 + team*0x140),
   Vehicle class (VehicleInit 0x428210 / VehicleDestroy 0x4283c0 / VehicleUpdate 0x428490),
   movement 0x428ca0 / 0x42a670 / 0x42abe0, lift (Storage) and wreck. See docs/game.md §5. */
#pragma once
#include "game.h"

enum { VT_TANK, VT_JEEP, VT_MSV, VT_HELI };

/* Control word (VehState.ctrl), built by ControllerDecodeDigital 0x41edf0 / ...Analog 0x41eee0. */
enum {
    CW_IDLE = 1, CW_FWD = 2, CW_BACK = 4, CW_RIGHT = 8, CW_LEFT = 0x10, CW_TURN = 0x18,
    CW_A_PRESS = 0x20, CW_A_HOLD = 0x40, CW_A_RELEASE = 0x80,
    CW_B_PRESS = 0x100, CW_B_HOLD = 0x200, CW_B_RELEASE = 0x400,
    CW_C_PRESS = 0x800, CW_C_HOLD = 0x1000, CW_C_RELEASE = 0x2000,
    CW_X4 = 0x4000, CW_X5 = 0x8000,
    /* bits 16..23 = left/right magnitude, 24..31 = up/down magnitude (raw input low 16 bits << 16) */
};

typedef struct VehState VehState;
typedef struct VehicleDef VehicleDef;
typedef int (*VehHookFn)(Obj *o, VehState *s);                                   /* state+0x0c */
typedef int32_t (*VehMoveFn)(int32_t *d, Obj *o, const VehicleDef *def, VehState *s); /* state+0x10 */
typedef void (*VehWaterFn)(Obj *o, const VehicleDef *def, VehState *s, int32_t moved); /* state+0x44 */
typedef int (*VehFireFn)(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits);
typedef int (*VehInitFn)(Obj *o, const VehicleDef *def);
typedef int (*VehDamageFn)(Obj *o, const VehicleDef *def, VehState *s, Obj *src);

/* Weapon / gauge slot (13 dwords at def+0x194 + i*0x34). Slot 2 is the fuel gauge (ammo = fuel units). */
typedef struct {
    int32_t proj;            /* [0] projectile type (table 0x450a78) */
    int32_t arg[3];          /* [1..3] launch params (muzzle z/offset...) */
    int32_t reload;          /* [4] ticks between shots */
    int32_t ammo;            /* [5] maximum / initial ammo */
    int32_t hud_kind;        /* [6] */
    int32_t hud_rect[4];     /* [7..10] */
    int32_t hud_scale;       /* [11] */
    int32_t w12;             /* [12] */
} WeaponDef;

/* Vehicle descriptor, 0xb8 dwords. [n] = dword index in the original; "->s+X" = copied to VehState. */
struct VehicleDef {
    int32_t type;                 /* [0x00] */
    const char *name;             /* [0x01] */
    VehHookFn launch_hook;        /* [0x05] ->s+0x0c first per-frame hook (lift exit / heli spin-up) */
    VehMoveFn move;               /* [0x06] ->s+0x10 (NULL = veh_move_ground 0x428ca0) */
    int32_t armour;               /* [0x09] ->s+0x1c damage threshold per hit */
    int32_t hp;                   /* [0x0a] ->s+0x20 hit points (16.16) */
    VehWaterFn water_hook;        /* [0x13] ->s+0x44 */
    int32_t cam_flags;            /* [0x31] ->s+0xbc camera-tracker node (renderer) */
    int32_t cam[24];              /* [0x3a..0x51] ->s+0xe0..0x13c camera / engine-pitch params */
    const Gfx *gfx;               /* [0x52] normal model + collision shape */
    const Gfx *gfx_splash;        /* [0x53] wading/splash model */
    const Gfx *gfx_sink;          /* [0x55] sinking model */
    int32_t sink_depth;           /* [0x56] z below which a sinking vehicle is lost */
    int32_t unk57;                /* [0x57] */
    const Gfx *gfx_wreck;         /* [0x58] */
    const Gfx *gfx_wreck2;        /* [0x59] */
    int32_t max_fwd;              /* [0x5a] top speed forward (px/tick 16.16) */
    int32_t max_rev;              /* [0x5b] top speed reverse (negative) */
    int32_t accel;                /* [0x5c] per tick */
    int32_t friction;             /* [0x5d] per tick when idle */
    int32_t turn;                 /* [0x5e] heading units per tick */
    VehFireFn fire[3];            /* [0x5f..0x61] buttons A (0xe0), B (0x700), C (0x3800) */
    int32_t fire_arg[3];          /* [0x62..0x64] */
    WeaponDef weapon[3];          /* [0x65..0x8b] */
    VehInitFn init_hook;          /* [0x8c] */
    VehHookFn death_hook;         /* [0x8d] if set, death runs this hook instead of spawning a wreck */
    VehDamageFn damage_hook;      /* [0x8e] called by the damage handler while hp > 0 */
    uint32_t snd_engine;          /* [0x90] sound event VA */
    int32_t snd_91, snd_92;       /* [0x91], [0x92] */
    uint32_t cam_attach;          /* [0x94] */
    int32_t dock_radius;          /* [0x95] max offset from the pad centre to dock */
    VehHookFn dock_hook;          /* [0x96] (NULL = DockVehicleInBunker, heli: landing sequence) */
    int32_t wreck_friction;       /* [0x97] */
    int32_t respawn_delay;        /* [0x98] ticks before returning to the bunker after death */
    int32_t radar_99;             /* [0x99] */
    uint32_t radar_icon[2];       /* [0x9a..0x9b] */
    int32_t hud9[5];              /* [0x9c..0xa0] */
    int32_t hud_icon[2];          /* [0xa1..0xa2] */
    int32_t unk_a3[3];            /* [0xa3..0xa5] */
    uint32_t arrow_enable;        /* [0xa6] non-zero: HUD arrow towards the enemy vehicle */
    int32_t unk_a7[4];            /* [0xa7..0xaa] */
    int32_t arrow_box[4];         /* [0xab..0xae] "enemy in view" box */
    uint32_t music;               /* [0xaf] byte0 track (<0 = none), byte1 priority, byte2 counter */
    int32_t tail[8];              /* [0xb0..0xb7] camera params */
};
extern VehicleDef vehicle_defs[4];                 /* game_tables.c, filled by exe_load() */

/* Per-team vehicle state (0x140 bytes at 0x459390 + team*0x140). */
struct VehState {
    const VehicleDef *def;        /* +0x00 */
    Obj *wreck;                   /* +0x04 */
    uint32_t ctrl;                /* +0x08 control word */
    VehHookFn hook;               /* +0x0c pre-update hook; returning non-zero skips the rest */
    VehMoveFn move;               /* +0x10 */
    int32_t fuel;                 /* +0x14 16.16 */
    void *hud_fuel;               /* +0x18 */
    int32_t armour;               /* +0x1c */
    int32_t hp;                   /* +0x20 */
    struct { int32_t _0, _4, next_fire, ammo; } slot[2]; /* +0x24 + i*0x10 */
    VehWaterFn water_hook;        /* +0x44 */
    int32_t water_anim;           /* +0x48 splash / sink animation value */
    int32_t _4c;
    int32_t elev;                 /* +0x50 tank cannon elevation (0 or 0x3b8e39) */
    int32_t elev_target;          /* +0x54 */
    int32_t turret;               /* +0x58 tank turret yaw / MSV launcher / heli rotor spin-up */
    union { int32_t turret_target; int32_t beacon; }; /* +0x5c tank turret target / jeep beacon strength */
    union { int32_t fire_pending; int32_t launch_tick; }; /* +0x60 */
    uint32_t *dock_cell;          /* heli: +0x5c during landing (alias, kept separate here) */
    uint32_t *pickup_cell;        /* +0x68 fuel/ammo dump or gate cell touched this frame */
    ShapePart *pickup_part;       /* +0x6c */
    int32_t water;                /* +0x70 GetObjWaterState of this frame */
    int32_t resupply_tick;        /* +0x74 */
    int32_t fuel_warn_tick;       /* +0x78 */
    int32_t boat;                 /* +0x80 jeep boat blend 0..0x10000 / heli rotor angle */
    int32_t boat_target;          /* +0x84 jeep boat mode / heli rotor speed */
    int32_t tilt;                 /* +0x88 heli bank */
    int32_t turn_vel;             /* +0x8c heli yaw rate */
    int32_t drift[2];             /* +0x90/+0x94 heli hover drift */
    int32_t vel[2];               /* +0x98/+0x9c heli velocity */
    int32_t hit_bno;              /* +0xa4 last BNO bumped into */
    uint32_t *hit_cell;           /* +0xa4 alias: cell of a turret bumped into */
    Obj *hit_obj;                 /* +0xa8 last object bumped into */
    uint32_t hit_obj_id;          /* +0xac */
    uint32_t target_heading;      /* +0xb0 analog stick heading */
    Obj *obj;                     /* +0xc8 */
    int32_t cam[24];              /* +0xe0..0x13c; cam[3] (+0xec) = lagged engine pitch */
};
extern VehState veh_states[2];

static inline VehState *veh_state(Obj *o) { return (VehState *)o->p60; }
static inline int veh_type(Obj *o) { return o->cls->id == 1 && o->p60 ? veh_state(o)->def->type : -1; }

/* Hooks referenced by the descriptor tables. */
int veh_tank_launch(Obj *o, VehState *s);      /* 0x429f20 */
int veh_msv_launch(Obj *o, VehState *s);       /* 0x42a250 */
int veh_jeep_launch(Obj *o, VehState *s);      /* 0x42a410 */
int veh_heli_spinup(Obj *o, VehState *s);      /* 0x42b3c0 */
int veh_heli_dock(Obj *o, VehState *s);        /* 0x42b600 */
int veh_jeep_dock(Obj *o, VehState *s);        /* 0x42ab90 */
int32_t veh_move_ground(int32_t *d, Obj *o, const VehicleDef *def, VehState *s); /* 0x428ca0 */
int32_t veh_move_jeep(int32_t *d, Obj *o, const VehicleDef *def, VehState *s);   /* 0x42a670 */
int32_t veh_move_heli(int32_t *d, Obj *o, const VehicleDef *def, VehState *s);   /* 0x42abe0 */
void veh_water_normal(Obj *o, const VehicleDef *def, VehState *s, int32_t moved); /* 0x4299e0 */
int veh_tank_fire(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits);   /* TankFireCannon 0x429d30 */
int veh_jeep_grenade(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits); /* 0x42aa00 */
int veh_jeep_boat(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits);   /* 0x42aae0 */
int veh_jeep_drop_flag(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits); /* 0x42ab70 */
int veh_msv_fire(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits);    /* 0x42a010 */
int veh_msv_mine(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits);    /* 0x42a310 */
int veh_heli_fire(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits);   /* 0x42b100 */
int veh_heli_missile(Obj *o, const VehicleDef *def, VehState *s, int32_t arg, uint32_t bits); /* 0x42b2a0 */
int veh_heli_init(Obj *o, const VehicleDef *def);                                            /* 0x42b5e0 */
int veh_heli_damaged(Obj *o, const VehicleDef *def, VehState *s, Obj *src);                  /* 0x42b330 */

uint32_t control_decode_digital(Obj *o);  /* 0x41edf0 */
uint32_t control_decode_analog(Obj *o);   /* 0x41eee0 */
int32_t terrain_speed_factor(Obj *o, const VehicleDef *def, VehState *s); /* FUN_00428e90 */
int veh_damage(Obj *o, Obj *src, int32_t amount);                         /* class +0x3c, 0x428f60 */

/* Misc tunables (game_tables.c). */
extern int32_t tun_launch_ticks, tun_idle_drone_ticks, tun_door_anim_rate, tun_heli_takeoff_dx;
extern int32_t tun_heli_turn_up, tun_heli_turn_down, tun_tank_long_pitch;
extern int32_t tun_pitch_rate[4][2], tun_resupply_slots[4], tun_road_flags[17];
extern int8_t tun_heli_lift_z[56], tun_heli_lift_tilt[56];
extern int32_t road_dir_flags;            /* _DAT_00471470, set by terrain_speed_factor */
