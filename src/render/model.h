/* 3D model parts (the 3DO engine's model structs in RFIRE.BIN .data). See docs/render.md 5/7.
   models_data.c (tools/gen_models_c.py) holds only the layout; the data is read from RFIRE.BIN (exe.c). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Face, 0x20 bytes: {sprite, flags, testA, testB, c0, c1, c2, c3}.
   flags: 1 = draw if X[a] <= X[b], 2 = draw if Y[a] <= Y[b], 8 = sprite + team, bits 8-9 debris class. */
typedef struct {
    int32_t sprite;
    uint32_t flags;
    int8_t ta, tb;
    uint8_t c[4];
} ModelFace;

typedef struct ModelPart {
    uint32_t addr;                 /* address of the part struct in RFIRE.BIN */
    uint32_t fn;                   /* +0x00 draw function (MODEL_FN_*) */
    const struct ModelPart *next;  /* +0x04 next part of the chain */
    uint32_t shape;                /* +0x08 collision shape (Model_DrawBuilding picks its sprite table by it) */
    int32_t size;                  /* +0x0c */
    uint32_t flags;                /* +0x10 0x10 = ignore z; byte 2 = building door side */
    int32_t off[3];                /* +0x14..+0x1c part origin offset (16.16) */
    int32_t zbias;                 /* +0x20 */
    uint32_t depth_hook;           /* +0x24 (MODEL_HOOK_*) */
    uint32_t pos_hook;             /* +0x28 (MODEL_HOOK_POS_JITTER) */
    int nverts;                    /* +0x2c */
    const int32_t (*verts)[3];     /* +0x30 */
    int nfaces_field;              /* +0x34 (<= 0: per-yaw face order lists) */
    int nfaces;                    /* entries in faces[] */
    const ModelFace *faces;        /* +0x38 */
    const int8_t *orders[8];       /* +0x3c (nfaces_field <= 0): face lists ending < 0, by yaw octant */
    int special;                   /* no geometry (BNO 76 destroyed bridge, hook only) */
} ModelPart;

/* Draw functions (+0x00). */
enum {
    MODEL_FN_STATIC = 0x408840, MODEL_FN_YAW = 0x408a20, MODEL_FN_YAWPITCH = 0x408aa0,
    MODEL_FN_TURRET = 0x408b80, MODEL_FN_TILTED = 0x408d40,
    MODEL_FN_BUILDING = 0x41f550, MODEL_FN_AMMO = 0x41fbe0,
    MODEL_FN_GATE_H_W = 0x4175a0, MODEL_FN_GATE_H_E = 0x417610,
    MODEL_FN_GATE_V_N = 0x417680, MODEL_FN_GATE_V_S = 0x4176f0,
    MODEL_FN_DEST_BUSH = 0x41f3b0, MODEL_FN_DEST_TREE = 0x41f420,
    MODEL_FN_DEST_WTOWER = 0x41f470, MODEL_FN_DEST_FUEL = 0x41f4b0,
    /* object wrappers (patch verts/faces at run time, then Model_DrawYaw / Model_DrawTurret) */
    MODEL_FN_TANK = 0x42f200, MODEL_FN_MSV = 0x42f300, MODEL_FN_JEEP = 0x42f400,
    MODEL_FN_JEEP_SHADOW = 0x42f660, MODEL_FN_HELI_ROTOR = 0x42f860 /* "Model_DrawHeliShadow" */,
    MODEL_FN_HELI = 0x42f990, MODEL_FN_ROTOR_A = 0x42fb10, MODEL_FN_ROTOR_B = 0x42fba0,
    MODEL_FN_STORAGE = 0x417d70, MODEL_FN_STORAGE_LIFT = 0x417e40, MODEL_FN_STORAGE_PAD = 0x417f10,
    MODEL_FN_EXPL = 0x4206b0, MODEL_FN_DEBRIS = 0x433a60, MODEL_FN_STAY = 0x435e40,
    MODEL_FN_DRONE = 0x435090, MODEL_FN_DRONE_SHADOW = 0x435120, MODEL_FN_SUB = 0x407320,
    MODEL_FN_MAN = 0x4061c0, MODEL_FN_MAN_SWIM = 0x406280, MODEL_FN_FLAG = 0x42f740, MODEL_FN_FLAG_CARRIED = 0x42f6e0,
};
/* Hooks (+0x24 depth hook, +0x28 position hook). */
enum {
    MODEL_HOOK_POS_JITTER = 0x41f210, MODEL_HOOK_TOWER = 0x4174e0, MODEL_HOOK_CELL = 0x41f380,
    MODEL_HOOK_HIDE = 0x4174a0, MODEL_HOOK_DEST_PLANTER = 0x41f4f0,
    /* object depth hooks (item, cell, obj) */
    MODEL_HOOK_SPLASH = 0x42ef80, MODEL_HOOK_SINK = 0x42efc0, MODEL_HOOK_BUBBLES = 0x42f0e0,
    MODEL_HOOK_WRECK = 0x42f120, MODEL_HOOK_VEH_BODY = 0x42f160, MODEL_HOOK_WRECK_BURNT = 0x42f1c0,
    MODEL_HOOK_JEEP_SPLASH = 0x42f5c0, MODEL_HOOK_HELI_ROTOR = 0x42f790, MODEL_HOOK_HELI = 0x42f900,
    MODEL_HOOK_ROTOR_SHADOW = 0x42fa50, MODEL_HOOK_STORAGE = 0x417d20,
    MODEL_HOOK_PROJ = 0x41f800, MODEL_HOOK_GRENADE = 0x41f830, MODEL_HOOK_EXPL = 0x420680, MODEL_HOOK_STAY = 0x435e10,
};

#include "models_data.h"
extern ModelPart model_parts[MODEL_PART_COUNT];            /* filled by exe_load() */

/* BNO table 0x451438 (stride 0x38), the fields the renderer / cell build need. */
#define RENDER_BNO_COUNT 91
typedef struct {
    const char *name;
    const ModelPart *model;     /* +0x08 first part (NULL: none) */
    uint32_t model_addr;
    uint32_t flags;             /* +0x0c (8 = terrain + team) */
    uint8_t terrain;            /* +0x10 terrain override (0xff keep) */
    uint8_t strength;           /* +0x11 */
} RenderBno;
extern RenderBno render_bno[RENDER_BNO_COUNT];

/* Projectile descriptor table 0x450a78: +0x2c model, +0x30 shadow, per projectile type. */
extern const ModelPart *model_projectile_model[12];
extern const ModelPart *model_projectile_shadow[12];
