/* Level map (.RFM) and in-memory world grid. See docs/rfm.md. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define WORLD_W 128
#define WORLD_H 128
#define TILE_PX 32
#define OBJ_COUNT 91

enum { TER_GRASS = 0, TER_SHALLOW = 1, TER_DEEP = 2, TER_BRIDGE_H0 = 84, TER_BRIDGE_H1 = 85,
       TER_BRIDGE_V0 = 86, TER_BRIDGE_V1 = 87 };
enum { OBJ_BRIDGE_H = 74, OBJ_BRIDGE_V = 75 };
enum { TEAM_0 = 0, TEAM_1 = 1, TEAM_NEUTRAL = 2 };

typedef struct { uint8_t terrain, object, team, handler; } TileDef;
typedef struct { const char *name; uint8_t terrain_override, strength; uint32_t radar; } ObjDef;
extern TileDef tile_table[240];              /* world_tables.c, filled from RFIRE.BIN by exe_load() */
extern ObjDef obj_table[OBJ_COUNT];

typedef struct { uint8_t terrain, object, team, strength; } Cell;

typedef struct {
    char name[64], author[48];
    int players, level;
    int vehicles[6];                 /* asv, heli, jeep, tank, unk, mines; -1 = default */
    Cell cell[WORLD_H][WORLD_W];
    int pad_x[2], pad_y[2];          /* home pad per team */
    int nflags[2];
    int flag_x[2][64], flag_y[2][64]; /* candidate flag sites per team */
} World;

bool world_load(World *w, const char *rel);
