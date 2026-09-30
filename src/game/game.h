/* Core game state for the simulation port: packed map cells, teams, static-object (BNO) table,
   collision, bunker/vehicle selection, per-frame update (GameFrame1P 0x41a660). See docs/game.md. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "fixmath.h"
#include "shape.h"
#include "object.h"

#include "../world.h"

/* ---- packed cell word (DAT_0045f3c0, uint32 cell[128][128], row-major) ---- */
#define CELL_TERRAIN(w)  ((w) & 0x7f)                 /* bits 0-6 */
#define CELL_BNO(w)      (((w) & 0x3f80) >> 7)        /* bits 7-13 */
#define CELL_TEAM(w)     (((w) & 0xc000) >> 14)       /* bits 14-15 */
#define CELL_HEAD(w)     (((w) & 0x1ff0000) >> 16)    /* bits 16-24: slot of first object in the cell */
#define CELL_HP(w)       (((w) & 0xe000000) >> 25)    /* bits 25-27 */
#define CELL_SET_TERRAIN(w, t) ((w) = ((w) & ~0x7fu) | ((uint32_t)(t) & 0x7f))

enum { TERR_PAD0 = 90, TERR_PAD1 = 91, TERR_LIFT = 92 };
enum { BNO_COUNT = 91, BNO_BRIDGE_H = 74, BNO_BRIDGE_V = 75 };

/* BNO record fields used by the sim (table 0x451438, stride 0x38). */
enum { BHIT_NONE, BHIT_BUSH, BHIT_ROCK, BHIT_PICKUP, BHIT_TENT, BHIT_DEST_FLAG };   /* +0x14 */
enum { BDES_DEFAULT, BDES_WALL, BDES_TOWER, BDES_FLAG_BUILDING, BDES_BRIDGE };      /* +0x1c */
typedef struct BnoDef {
    const char *name;        /* +0x04 */
    const Gfx *gfx;          /* +0x08 (collision shape at gfx->shape) */
    uint32_t flags;          /* +0x0c bits 1/2 random terrain variants, 16-23 men min/max, 0x8000 prisoners */
    uint8_t terrain, hp;     /* +0x10 terrain override (0xff keep), +0x11 hit points */
    uint8_t hit;             /* +0x14 BHIT_* touch handler */
    uint8_t destroy;         /* +0x1c BDES_* */
    int32_t armour;          /* +0x20 damage threshold */
    int32_t mult;            /* +0x24 damage multiplier (0 = 1) */
    uint32_t expl_crush;     /* +0x28 explosion when crushed by a vehicle (bushes) */
    uint32_t expl;           /* +0x2c explosion descriptor VA */
    uint8_t dest_terrain;    /* +0x30 */
    uint8_t dest_bno;        /* +0x31 */
} BnoDef;
extern BnoDef bno_defs[BNO_COUNT];                 /* game_tables.c, filled by exe_load() */

typedef struct { uint8_t invert; int8_t rot; ShapePart *shape; } ShoreShape;
extern ShoreShape shore_shapes[24];
extern int32_t shore_boxes[12][4];
extern ShapePart *const shape_default_point;

/* Home pad record (team+0x3c + i*12): pad centre and its cell. */
typedef struct { int32_t x, y; uint32_t *cell; } PadRec;

/* Team struct (0x480f50 + team*0xd0). Only the fields the sim uses. */
struct Team {
    int32_t index;           /* +0x08 */
    Obj *lift_or_wreck;      /* +0x10 set by WreckInit, cleared by StorageInit */
    struct BunkerView *view; /* +0x14 */
    const uint32_t *in_cur, *in_prev; /* +0x18/+0x1c raw input words */
    uint32_t *door_cell;     /* +0x28 cell whose terrain is restored to pad_terrain each vehicle update */
    uint32_t pad_terrain;    /* +0x2c terrain id of the home pad (90/91) */
    PadRec *pad;             /* +0x34 (and +0x30) the launch pad, picked at random */
    int32_t npads;           /* +0x38 */
    PadRec pads[4];          /* +0x3c */
    uint32_t *hud;           /* +0x78 HUD block (not ported) */
    int32_t arrow_tick;      /* +0x7c next enemy-arrow update */
    int16_t arrow_dir;       /* +0x80 0..7, 8 = enemy in view, -1 = none */
    uint8_t loadout[20];     /* +0xac: [4..7] controller scheme per type, [12..15] stock T/J/M/H, [16] mines */
    int32_t cell_tick;       /* +0xcc tick of the last cell change (idle-drone rule) */
};
#define TEAM_STOCK(t, type) ((t)->loadout[12 + (type)])
#define TEAM_MINES(t)       ((t)->loadout[16])
#define TEAM_SCHEME(t, type) ((t)->loadout[4 + (type)])

/* Bunker / vehicle-select view state (subset of the 0x24c-byte view struct at 0x48bd90). */
/* BV_SPECTATE (2 players only): the side has no vehicles left while the other still has jeeps
   (FUN_00427d70 == 1): FUN_004054f0 -> LAB_00405590 -> 0x405710, a static "skull and crossed-out
   vehicles" screen until the other player wins. */
typedef enum { BV_SELECT, BV_LAUNCH, BV_ZOOM_IN, BV_PLAYING, BV_FADE_TO_SELECT, BV_FLYBACK, BV_SPECTATE } BunkerMode;
typedef struct { uint32_t fn; int32_t a, b, c; int32_t cont; } BunkerStep;
typedef struct BunkerView {
    int team;                /* +0x04 */
    Team *tm;                /* +0x68 */
    BunkerMode mode;         /* +0xc0 mode function */
    int32_t fade;            /* +0xbc 0..0x10000 */
    int32_t sel;             /* +0xcc selected vehicle type */
    int32_t cam_d0, cam_d4, cam_d8; /* +0xd0/+0xd4/+0xd8 bunker camera animation values */
    const BunkerStep *step;  /* +0xc8 zoom script cursor */
    Obj *lift;               /* +0xc4 */
    int32_t flyback_until;   /* +0xc8 in fly-back mode */
    int fly_stage;           /* which of 0x404f70 / 0x4052d0 / 0x405360 / 0x405440 is running */
    int last_type;           /* +0x3a type of the last vehicle launched */
    int redraw;              /* +0x3b spectate: frames the static screen is still drawn (both buffers) */
} BunkerView;
extern BunkerStep bunker_scripts[4][9];
extern uint8_t bunker_menu_nav[4][4];
extern uint8_t loadout_default[20];

/* Out-of-scope subsystems are reached through these hooks (all optional). */
typedef struct {
    void (*sound)(int cmd, uint32_t event_va, Obj *owner);          /* Snd_QueueCommand 0x42cb40 */
    int (*music)(int track, int prio, Obj *owner);                  /* Mus_Request 0x41d530 */
    void (*explosion)(int team, const int32_t *pos, uint32_t desc_va); /* notification: SpawnExplosion 0x4209a0 */
    void (*drone)(Obj *target);                                     /* SpawnDrone 0x435950 */
    void (*sub)(void);                                              /* SpawnSub (heli left the map) */
    void (*end_game)(int winner);                                   /* EndGame 0x40f380 */
    void (*log)(const char *msg, Obj *o);
    void (*sound_step)(int which, int32_t ratio, int32_t ticks);   /* bunker zoom-script sound step 0x4042b0 */
    void (*hud)(int ev, int team, Obj *o);                         /* HUD slot changes (HudSetSlot callers), GHUD_* */
    void (*hud_dirty)(int team, uint32_t mask);                     /* HudMarkDirty(0x472060 + team*0x108, mask) */
    /* Classes owned by later ports (MAN 0x43fb10, Flag 0x44ae30): */
    int (*spawn_men)(uint32_t *cell, const struct BnoDef *d);       /* SpawnMenFromBuilding 0x4071f0: returns men count */
    Obj *(*spawn_man)(int team, int32_t x, int32_t y);              /* SpawnMan 0x4071c0 (wreck crew bail-out) */
    Obj *(*flag_spawn)(int team, const int32_t *pos);               /* FlagBuildingDestroyed: ObjCreate(Flag) + radar
                                                                       blip + enemy camera tracker (120 ticks) */
} GameHooks;
enum { GHUD_SELECT, GHUD_DASH, GHUD_VEH_INIT, GHUD_VEH_START, GHUD_VEH_GONE };
extern GameHooks game_hooks;

typedef struct {
    uint32_t cell[128 * 128];     /* DAT_0045f3c0 */
    uint32_t outside;             /* *PTR_00452d40 (0x46f3c0): pseudo-cell for everything off the map */
    Team teams[2];
    BunkerView views[2];
    uint32_t input_cur[2], input_prev[2]; /* DAT_0046f5dc / DAT_0046f5d4 */
    Obj *cur_vehicle[2];          /* DAT_00471468 */
    uint32_t cur_vehicle_id[2];   /* DAT_00471460 */
    Obj *flag_obj[2];             /* DAT_00472038 (Flag class not ported) */
    int32_t drones[2];            /* DAT_00471458: enemy Turret Gun objects in the view last frame (renderer, View.enemy_turrets); < 3 allows the idle drone */
    int nplayers;                 /* DAT_00443864 */
    int running;                  /* DAT_0043fcdc */
    int winner;                   /* DAT_00457100 */
    int level;                    /* DAT_00443868 (0..8) */
    int8_t jitter[256][4];        /* DAT_00472290 (FUN_0041f190) */
    int door_anim_active;         /* DAT_00459610 */
    int32_t door_anim_acc[2];     /* 0x44b048 */
    int storage_blocked;          /* DAT_0045720c */
    int32_t random_mines;         /* DAT_0043fe18 */
    int32_t ai_pool;              /* DAT_0043fe1c */
    int nflags[2];
    uint32_t *flag_sites[2][254]; /* 0x471c40 / 0x471840 */
    uint32_t *flag_cell[2];       /* DAT_00471818 / DAT_0047181c */
    Obj *man_focus[2];            /* DAT_00456a18 (FUN_00406180): the team's latest wreck, for the MAN AI */
    uint32_t man_focus_id[2];     /* DAT_00456a20 */
    int32_t flag_left[2];         /* DAT_004810f8 / DAT_004810fc: flag buildings left before the flag must show */
    uint8_t stock0[2][4];         /* DAT_0048110c + team*0x14 + 0xc: the level's starting stock T/J/M/H (fly-back icons) */
} Game;
extern Game G;

/* ---- setup / frame ---- */
bool game_load(World *w, const char *rfm_rel);   /* LoadLevelMap 0x4322f0 + StartNewGame/GameSetup1P */
void game_frame(int32_t dt, uint32_t input_p1);          /* GameFrame1P 0x41a660 (sim part) */
/* GameFrame2P 0x41aa70 (sim part): the objects update before the input poll (they see last frame's
   words), then both bunker views run. */
void game_frame_2p(int32_t dt, uint32_t input_p1, uint32_t input_p2);
uint32_t *cell_at(const int32_t *pos);                   /* FUN_00417760 */
void cell_to_pos(const uint32_t *cell, int32_t *pos);    /* CellToWorldPos 0x4177e0 */
int cell_index(const uint32_t *cell);

/* ---- collision (collide.c) ---- */
int obj_check_collision(Obj *o);                          /* ObjCheckCollision 0x41dcf0 */
int water_state(Obj *o);                                  /* GetObjWaterState 0x4185e0: 0 dry, 1 shallow, 2 deep */
int water_state_at(const int32_t *pos);                   /* FUN_00418770 */
int cell_water_type(uint32_t w);                          /* GetCellWaterType 0x418a90 */
int bno_damage(int32_t amount, Obj *src, uint32_t *cell, const BnoDef *d); /* BnoDamage 0x417c20: 1 destroyed, -1 not */
int bno_destroy(Obj *src, uint32_t *cell, const BnoDef *d);                /* BnoDestroy 0x417a00 */
void bno_set_destroyed(uint32_t *cell, const BnoDef *d, int team);         /* FUN_00417960 */
void bno_adjust_pos(const BnoDef *d, int32_t *pos);                        /* gfx +0x28 hook (0x41f210 jitter) */
int hide_flag_in_building(int team);                                      /* HideFlagInRandomBuilding 0x424060 */
const Gfx *gfx_by_addr(uint32_t addr);                                    /* graphic by original VA (game_tables.c) */
void set_cell_static(int bno, uint32_t *cell, int hp_bonus, int team);     /* SetCellStaticObject 0x417850 */

/* ---- bunker / lift (vehicle.c) ---- */
void bunker_enter_select(BunkerView *v);                  /* ViewEnterBunkerSelect 0x404d30 */
void bunker_view_update(BunkerView *v);                   /* view->mode(view) */
Obj *spawn_lift_vehicle(int team, int type);              /* SpawnLiftVehicle 0x428020 */
Obj *spawn_vehicle(int team, const int32_t *pos, uint32_t heading, int type); /* SpawnVehicle 0x427cd0 */
Obj *dock_vehicle(Obj *veh);                              /* DockVehicleInBunker 0x418470 */
Obj *get_team_vehicle(int team);
void bunker_door_animate(int team);                       /* BunkerDoorAnimate 0x427f10 */
void bunker_door_reset(void);                             /* FUN_00427fe0 */                          /* GetTeamVehicle 0x427ed0 */
int team_status(int team);                                /* FUN_00427d70 */
extern const ObjClass class_vehicle, class_storage_up, class_storage_down, class_wreck;
