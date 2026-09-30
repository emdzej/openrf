/* Computer-controlled enemies: tower turrets (class 2 "Turret Gun" 0x44ad40 / 0x44ad90, ActivateTurret
   0x423c10 .. 0x423ac0), the idle-punishment Drone (class 9 0x455778, 0x435040 .. 0x435c30) and the
   submarine that hunts helicopters off the map (class 15 "SUB" 0x43fc10, 0x407320 .. 0x4076f0).
   See docs/game.md §9. Files: turret.c, drone.c, sub.c. */
#pragma once
#include "game.h"

/* Sound / radar services these classes need beyond game_hooks.sound (all optional). */
typedef struct {
    /* Snd_QueueCommand(1, ev, 0, 1) returning the instance + handle (unowned looping drone hum). */
    void *(*snd_play)(uint32_t event_va, uint32_t *handle);
    /* instance +0x4c / +0x50: per-listener source positions (SFXF_OWN_GAINS). */
    void (*snd_stereo)(void *inst, const int32_t *pos_l, const int32_t *pos_r);
    /* Snd_QueueCommand(9, handle, inst, 1) */
    void (*snd_kill)(void *inst, uint32_t handle);
    /* listener positions (view +0x18: 0x48bda8 / 0x48bff4); NULL = not known (headless) */
    const int32_t *listener[2];
} AiHooks;
extern AiHooks ai_hooks;

/* Per-level reset (StartNewGame: FUN_00435040(pool) + the turret counters) and hook install
   (game_hooks.drone = SpawnDrone, game_hooks.sub = SpawnSub). Call after game_load. */
void ai_level_start(void);
/* End-of-frame part of Mus_Director 0x41a1c0 driven by these classes (flag 0x1000 of DAT_00480e3c:
   a live SUB -> MUS_SUB, prio 0x8a). Call once per frame after game_frame. */
void ai_frame_end(void);
extern uint32_t mus_director_flags;          /* _DAT_00480e3c (this module sets 0x1000) */

/* ---- turrets (turret.c) ---- */
extern const ObjClass class_turret, class_turret_large;
extern int32_t turret_count[2];              /* DAT_00472048: live turrets per team */
extern int32_t turret_near_dist[2];          /* DAT_0044ad38: nearest turret to a team's vehicle this tick */
extern int32_t turret_near_pos[2][3];        /* DAT_00471820 */
extern int32_t tower_yaw, tower_pitch;       /* DAT_0044ad30 / DAT_004588c8 (collide.c TowerDestroyed) */
/* The sim side of Model_TowerHook 0x4174e0 for a tower cell being queued for drawing in view `team`:
   the view's vehicle (view +0x6c) of the other side, class Vehicle -> ActivateTurret. Returns the new
   turret (the static tower is then skipped, DAT_0045f398 = -1) or NULL. */
Obj *turret_tower_hook(uint32_t *cell, int view_team);
Obj *activate_turret(uint32_t *cell, Obj *target);                /* ActivateTurret 0x423c10 */
int bno_damage_hp(int32_t amount, Obj *src, uint32_t *cell, const BnoDef *d);   /* FUN_00417b50 */
/* Obj fields of a turret (+0x60 byte BNO, +0x61 byte HP, +0x62 short unseen ticks, +0x5c pitch,
   +0x64 wake-up tick, +0x68 next shot, +0x6c rest yaw, +0x70 muzzle table). */
#define TURRET_UNSEEN(o) ((o)->u58)          /* +0x62, zeroed whenever Model_TowerHook draws it */
#define TURRET_PITCH(o)  ((o)->v5c)          /* +0x5c */

/* ---- drone (drone.c) ---- */
extern const ObjClass class_drone;
typedef struct DroneRec {                    /* 0x20-byte records at 0x4597b8 (5 per team) */
    struct DroneRec *next;                   /* +0x00 free list */
    Obj *obj;                                /* +0x04 */
    int32_t drawn;                           /* +0x08 tick of the last draw (0x435090) */
    int8_t team;                             /* +0x0c */
    uint8_t muzzle;                          /* +0x0d left/right gun */
    uint8_t exploded;                        /* +0x0e shot down (explode on destroy) */
    uint32_t kill_heading;                   /* +0x10 */
    int32_t next_fire;                       /* +0x14 */
    Obj *bumped;                             /* +0x18 drone that touched this one this frame */
    uint32_t gun_pitch;                      /* +0x1c relative to the body pitch */
} DroneRec;
extern int32_t drone_count[2];               /* DAT_00459930 */
extern int32_t drone_total;                  /* DAT_0045993c */
DroneRec *drone_rec(const Obj *o);           /* obj +0x6c */
Obj *spawn_drone(Obj *target);               /* SpawnDrone 0x435950 */
void drone_pool_init(int n);                 /* FUN_00435040: n (<= 5) records per team */
/* Obj fields: +0x54 speed, +0x58 roll, +0x5c/+0x60 velocity, +0x68 pitch, +0x6c record, +0x70 params */
#define DRONE_ROLL(o)  ((o)->u58)
#define DRONE_VX(o)    (*(int32_t *)&(o)->v5c)
#define DRONE_VY(o)    ((o)->i60)
#define DRONE_PITCH(o) ((o)->u68)

/* ---- submarine (sub.c) ---- */
extern const ObjClass class_sub;
extern Obj *sub_obj;                         /* DAT_0048bb1c */
void spawn_sub(void);                        /* SpawnSub 0x4076f0 */
/* Obj fields: +0x5c animation frame (16.16; 0 hidden, 1..0x13 surfacing, 0x14..0x19 surfaced loop),
   +0x60 timer, +0x64 tick of the last draw, +0x68 0 idle / 1 surface / 2 dive */
#define SUB_FRAME(o)   (*(int32_t *)&(o)->v5c)
#define SUB_TIMER(o)   ((o)->i60)
#define SUB_DRAWN(o)   ((o)->u64)
#define SUB_STATE(o)   ((o)->u68)
