/* Explosions (class 11 "Expl", ExplInit/Update/Destroy 0x4204c0..0x4205c0, byte-code ops 0x44a9c0),
   debris (class 3 "FWall", DebrisInit/Update 0x4344b0..0x434550, SpawnDebris 0x434670,
   SpawnDebrisBurst 0x4347a0, steps 0x454ee8), remains (class 17 "Stay" 0x435da0..0x435eb0) and the
   projectile / grenade shadow (class 4 "Shadow" 0x434cf0 / 0x434d70). See docs/game.md §6. */
#pragma once
#include "game.h"
#include "../render/model.h"

/* ---- .data of RFIRE.BIN read in place (exe.c): explosion descriptors, scripts, label tables, sprite
   vertices/faces in 0x44a600..0x44aa00 and 0x452d00..0x454800 (reads outside these ranges return 0) ---- */
uint32_t rf_u32(uint32_t va);
int8_t rf_s8(uint32_t va);
const int32_t *rf_ptr(uint32_t va);     /* word-aligned VA -> pointer into the image (NULL outside) */

/* Explosion descriptor (VA; dwords): [0] script, [1] label table, [2] graphic flags (+0x10: 0x10 = on the
   ground), [3] frames (16.16), [4] z-bias, [5] frame rate per tick, [6] scale, [7] nverts, [8] verts,
   [9] nfaces, [10] faces (6 dwords: sprite, {first, last, fade, variant}, 4 vertex indices). */
enum { ED_SCRIPT, ED_LABELS, ED_FLAGS, ED_FRAMES, ED_ZBIAS, ED_RATE, ED_SCALE, ED_NVERTS, ED_VERTS, ED_NFACES, ED_FACES };
static inline uint32_t expl_field(uint32_t desc, int k) { return rf_u32(desc + 4 * (uint32_t)k); }

extern uint32_t expl_tab_450a48[6], expl_tab_450a60[6], expl_tab_vehicle[4];
extern uint32_t snd_expl_tab[40], snd_grenade_tab[3];

/* Per-object "heap block" of an explosion with a damage box (ExplOp13 0x41ff60, 0xb0 bytes). */
typedef struct {
    Gfx box_gfx;            /* copy of 0x44aa20 with the box as shape */
    Gfx part_gfx;           /* copy of explosion part 0x452dc8 + i*0x44 sharing the box (op 6) */
    ShapePart box;          /* +0x2c type 2 AABB, solid 0x20, mask from the script */
    int32_t damage;         /* +0x68: <0 = per tick */
    bool used;
} ExplExt;
extern ExplExt expl_ext[OBJ_POOL];

Obj *spawn_explosion(int team, int32_t x, int32_t y, int32_t z, uint32_t desc, uint32_t *cell);  /* 0x4209a0 */
Obj *spawn_explosion_attached(Obj *parent, int team, const int32_t *off, uint32_t desc);        /* 0x4209d0 */
void expl_set_pitch(Obj *e, int32_t pitch);                                                     /* 0x420a40 */

/* ---- debris ---- */
typedef struct { int32_t op, from, to, p0c, p10, p14, p18, p1c; } DebrisStep;   /* 8 dwords */
typedef struct DebrisDef { int32_t rate, frames, fade, nsteps; const DebrisStep *steps; uint32_t addr; } DebrisDef;
extern const DebrisDef *debris_sets[10][4];
extern const DebrisDef *debris_set_tower[4];
extern DebrisDef debris_44a0a0;

/* Per-object heap block of a debris piece (DebrisInit: 0x130 bytes). */
typedef struct {
    int32_t rate;           /* +0x00 frame rate (from the DebrisDef) */
    int32_t drawn;          /* +0x04 tick of the last draw */
    int32_t slot[10];       /* +0x08.. per-step state (0x7fffffff = not started) */
    int32_t vel[3];         /* +0x34..+0x3c */
    int sprite;             /* +0x40 CCB drawn (ART.CAR index) */
    int sprite_own;         /* +0x48 index of the private CCB copy */
    int32_t centre[3];      /* +0xe0 centroid of the source face */
    int32_t dir[3];         /* +0xec centroid direction (FUN_00433820, 1/len from the reciprocal table) */
    int32_t verts[4][3];    /* +0xf8 face corners relative to the centroid */
} DebrisExt;
extern DebrisExt debris_ext[OBJ_POOL];

Obj *spawn_debris(int team, const int32_t *pos, const DebrisDef *d, int sprite, const int32_t (*verts)[3],
                  const int32_t *idx);                                                         /* 0x434670 */
typedef void (*DebrisCb)(Obj *piece, int n);
int spawn_debris_burst(const int32_t *pos, int team, const DebrisDef *const *set, const ModelPart *m,
                       uint32_t heading, int32_t pitch, DebrisCb cb);                          /* 0x4347a0 */
int debris_frame(const Obj *o);

/* ---- Stay (remains) ---- */
Obj *spawn_stay(int team, const int32_t *pos, const Gfx *g, int32_t life, int32_t linger);    /* 0x435eb0 */
/* Set by the renderer (0x433a60 / 0x435e40 side effects): bit0 destroy, bit1 move to the recycle tail. */
extern uint8_t obj_draw_flags[OBJ_POOL];

/* ---- Shadow of projectiles / grenades ---- */
Obj *spawn_shadow(Obj *parent, const Gfx *g);    /* ObjCreate(0x454fb8, ...) with class+0x40 = g */

/* Model part by original VA (render/models_data.c). */
const ModelPart *model_part_by_addr(uint32_t addr);

extern const ObjClass class_expl, class_fwall, class_stay, class_shadow;

/* frame-level reaper for pieces the renderer flagged (the original destroys them inside the draw). */
void effects_reap_drawn(void);
