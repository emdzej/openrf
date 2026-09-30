/* Soldiers (MAN, class 14 0x43fb10), gates (class 13 0x44ade0), the flag (class 12 0x44ae30), random mine
   placement (FUN_0041c9b0) and the end-of-game rules. Files: man.c, gate.c, flag.c, mine.c, rules_tables.c
   (tools/gen_rules_tables.py). See docs/game.md §10. */
#pragma once
#include "game.h"

/* Services the app provides (all optional; headless tests leave them NULL). */
typedef struct {
    /* FlagBuildingDestroyed: ViewAddTracker(view of `team`, flag, zoff 10 px, pitch 0x180000, height
       DAT_0043eea0, timer 120) when that team's vehicle is within 320 px of the new flag. */
    void (*flag_camera)(int team, Obj *flag);
} RulesHooks;
extern RulesHooks rules_hooks;

/* Called by game_load at the point of StartNewGame 0x40b710 between GameInit and GameSetup1P:
   installs the MAN/Flag hooks (game_hooks.spawn_men / spawn_man / flag_spawn when unset) and places
   the random mines (FUN_0041c9b0, VHCL mines or level*4 from level 6 on). */
void rules_level_start(int32_t mines);

/* Music director events (_DAT_00480e3c bits 0x100 Flag Discovery, 0x200 Flag Pickup) raised by the flag
   code; the app's director reads and clears them once per frame (Mus_Director 0x41d730). */
extern uint32_t rules_music_bits;

/* ---- MAN (man.c) ---- */
extern const ObjClass class_man;
Obj *spawn_man(int team, int32_t x, int32_t y);                      /* SpawnMan 0x4071c0 */
int spawn_men_from_building(uint32_t *cell, const BnoDef *d);        /* SpawnMenFromBuilding 0x4071f0 */
/* Obj fields: +0x58 object bumped (ext), +0x5c tick of the last draw (the draw 0x4061c0/0x406280 stamps
   it; undrawn for 120 ticks = removed), +0x60 think timer, +0x64 animation frame 16.16, +0x68 cell bumped
   (ext), +0x70 grenades, +0x71 flee-angle offset, +0x72 animation (0 walk, 1 stand, 2 throw, 3 swim,
   4 tread water), +0x73 water state. parent (+0x2c) = the vehicle it reacts to. */
#define MAN_DRAWN(o)    (*(int32_t *)&(o)->v5c)
#define MAN_TIMER(o)    ((o)->i60)
#define MAN_FRAME(o)    ((o)->u64)
#define MAN_GRENADES(o) (*(int8_t *)&(o)->b70[0])
#define MAN_ANGLE(o)    (*(int8_t *)&(o)->b70[1])
#define MAN_ANIM(o)     ((o)->b70[2])
#define MAN_WATER(o)    ((o)->b70[3])
typedef struct { Obj *bumped; uint32_t *cell; } ManExt;
extern ManExt man_ext[OBJ_POOL];

/* ---- Gate (gate.c) ---- */
extern const ObjClass class_gate;
Obj *spawn_gate(uint32_t *cell, Obj *veh);                           /* SpawnGate 0x423fb0 */
/* Obj fields: +0x3c private copy of the graphic (doors move), +0x60 BNO id, +0x64 opening 16.16 px
   (0..15, read by the draw wrappers 0x4175a0..0x4176f0), +0x68 ticks fully closed, +0x70 target. */
#define GATE_OPEN(o)   ((o)->u64)
#define GATE_CLOSED(o) ((o)->u68)
#define GATE_TARGET(o) ((o)->u70)
#define GATE_BNO(o)    (*(uint8_t *)&(o)->i60)

/* ---- Flag (flag.c) ---- */
extern const ObjClass class_flag;
Obj *flag_spawn(int team, const int32_t *pos);                      /* FlagBuildingDestroyed: ObjCreate(Flag) part */
int flag_toggle(Obj *jeep);                                          /* FUN_004248a0 (jeep button C): -1 dropped, 1 picked up */
int flag_touch_dest_building(Obj *o, uint32_t *cell);                /* DestFlagOnTouch 0x4247e0 */
void flag_on_lift(Obj *lift, Obj *flag);                             /* StorageUpdate 0x417fb0, class 12 on the lift */
/* Obj fields: +0x4c heading, +0x5c wave frame 16.16, +0x60 next radar blink, +0x64 radar blip (here: 1 =
   shape 0x44e7a8 shown, 0 = 0x44e7b8 empty), +0x68 jeep touching (ext), +0x6c target heading,
   +0x70 jeep that dropped it (ext; no pickup until it leaves). */
#define FLAG_WAVE(o)   (*(int32_t *)&(o)->v5c)
#define FLAG_BLINK(o)  ((o)->i60)
#define FLAG_BLIP(o)   ((o)->u64)
#define FLAG_TARGET(o) ((o)->u6c)
typedef struct { Obj *touch, *dropped; } FlagExt;
extern FlagExt flag_ext[OBJ_POOL];
extern int32_t flag_home[2][3];                                      /* DAT_004588b0: last dry position */

/* ---- mines (mine.c) ---- */
void place_random_mines(int n);                                      /* FUN_0041c9b0 */

/* ---- end of game ---- */
void rules_end_game(int winner);                                     /* TeamWins 0x427e80 / EndGame 0x40f380 */

/* ---- generated tables (rules_tables.c) ---- */
/* rules_tables.c, filled from RFIRE.BIN by exe_load() */
extern Gfx rgfx_43f900, rgfx_43f998, rgfx_43fa30, rgfx_44e740, rgfx_44e610, rgfx_4428a8, rgfx_442d70;
extern const ShapePart *const gate_door_parts[2];
extern ShapePart *const gate_zone_part;
extern int32_t man_walk_speed, man_anim_rate, man_grenade_min;
extern int8_t man_grenades[16];
extern int32_t man_unstick[8][3];
extern int32_t flag_carry_offset[3], flag_pitch_bias, flag_pitch_shift;
extern int32_t win_movie_by_level[10], win_banner_pos[4][2];
