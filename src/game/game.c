/* Level setup (LoadLevelMap 0x4322f0 cell build, FUN_004320c0 loadout, GameSetup1P 0x41a480)
   and the per-frame simulation step (GameFrame1P 0x41a660). */
#include "game.h"
#include "vehicle.h"
#include "rules.h"
#include "../world.h"
#include "../assets.h"
#include <stdlib.h>
#include <string.h>

Game G;
GameHooks game_hooks;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* Home pad handler 0x431fb0 (tile handler 1). */
static void pad_handler(uint32_t *cell, int team, int32_t x, int32_t y)
{
    if (team > 1) return;
    Team *t = &G.teams[team];
    int n = t->npads;
    if (n >= 4) return;
    t->npads = n + 1;
    t->pads[n] = (PadRec){ x, y, cell };
    t->pad_terrain = CELL_TERRAIN(*cell);
    t->pad = &t->pads[rand_range(t->npads)];
}

/* Flag site handler 0x431f60 (tile handler 2). */
static void flag_handler(uint32_t *cell, int team)
{
    if (team > 1 || G.nflags[team] >= 0xfe) return;
    G.flag_sites[team][G.nflags[team]++] = cell;
}

/* FUN_0042fdd0: DAT_0044f2dc is 0, so the 32-bit value comes from FUN_0042bbe0, i.e. three calls of the
   global CRT rand(): (r3 & 3) + (r1 * 0x8000 + r2) * 4; scaled to 0..n-1 in unsigned 32-bit arithmetic. */
static int flag_pick(int n)
{
    uint32_t r1 = (uint32_t)rng_rand(), r2 = (uint32_t)rng_rand(), r3 = (uint32_t)rng_rand();
    uint32_t u = (r3 & 3) + (r1 * 0x8000u + r2) * 4u, un = (uint32_t)n;
    return (int)((((((u * 2u) & 0xffffu) * un) >> 16) + ((u & 0x7fffffffu) >> 15) * un) >> 16);
}

/* FUN_0041f190: per-cell jitter table for bushes/palms (seeded by the sum of all tile bytes). */
static void build_jitter(uint32_t seed)
{
    int saved = rng_rand();
    rng_seed(seed);
    for (int i = 0; i < 256; i++) {
        G.jitter[i][0] = (int8_t)(rand_range(0x19) - 0xc);
        G.jitter[i][1] = (int8_t)(rand_range(0x19) - 0xc);
        G.jitter[i][2] = (int8_t)rand_range(0xb);
        G.jitter[i][3] = (int8_t)rand_range(0x100);
    }
    rng_seed((uint32_t)saved);
}

bool game_load(World *w, const char *rel)
{
    size_t sz;
    uint8_t *d = file_read_all(rel, &sz);
    if (!d || sz < 0x50) { free(d); return false; }
    uint32_t doff = rd32(d + 0x48);
    int mw = rd16(d + 8), mh = rd16(d + 10);
    if (mw > 128 || mh > 128 || doff + (uint32_t)(mw * mh) > sz) { free(d); return false; }

    fixmath_init();
    obj_system_init();
    timer_clear();
    frame_tasks_clear();
    memset(&G, 0, sizeof G);
    memset(veh_states, 0, sizeof veh_states);
    g_tick = 0;
    g_dt = 0;
    G.nplayers = w->players > 1 ? 2 : 1;
    G.running = 1;
    G.winner = -2;
    G.level = w->level > 0 ? (w->level - 1 > 8 ? 8 : w->level - 1) : 0;
    for (int t = 0; t < 2; t++) G.teams[t].index = t;

    /* Clear; the original then writes terrain 2 into cell[0] only (loop without increment). */
    G.cell[0] = (G.cell[0] & 0xffffff82u) | 2;
    int ox = (128 - mw) >> 1, oy = (128 - mh) >> 1;
    uint32_t seed = 0;
    const uint8_t *p = d + doff;
    for (int y = oy; y < oy + mh; y++)
        for (int x = ox; x < ox + mw; x++) {
            uint8_t b = *p++;
            seed += b;
            if (b > 0xef) b = 0;
            const TileDef *t = &tile_table[b];
            uint32_t *c = &G.cell[y * 128 + x];
            if (t->terrain == 0xff) { *c &= 0xffffff80u; continue; }
            CELL_SET_TERRAIN(*c, t->terrain);
            if (t->object) set_cell_static(t->object, c, 0, t->team);
            if (t->handler == 1) pad_handler(c, t->team, x * 0x200000 + 0x100000, y * 0x200000 + 0x100000);
            else if (t->handler == 2) flag_handler(c, t->team);
        }
    free(d);
    if (!G.teams[0].npads || (G.nplayers > 1 && !G.teams[1].npads) || !G.nflags[1] || (G.nplayers > 1 && !G.nflags[0]))
        return false;
    /* Bridge decks between the abutment terrains. */
    for (int i = 0; i < 128 * 128; i++) {
        uint32_t *c = &G.cell[i];
        if (CELL_TERRAIN(*c) == 0x54)
            for (uint32_t *q = c + 1; q < G.cell + 128 * 128 && CELL_TERRAIN(*q) != 0x55; q++)
                set_cell_static(BNO_BRIDGE_H, q, 0, CELL_TEAM(*c));
        if (CELL_TERRAIN(*c) == 0x56)
            for (uint32_t *q = c + 128; q < G.cell + 128 * 128 && CELL_TERRAIN(*q) != 0x57; q += 128)
                set_cell_static(BNO_BRIDGE_V, q, 0, CELL_TEAM(*c));
    }
    build_jitter(seed);
    for (int t = 1; t >= 0; t--)                                /* team 1 first (LoadLevelMap) */
        if (G.nflags[t]) G.flag_cell[t] = G.flag_sites[t][flag_pick(G.nflags[t])];
    G.flag_left[0] = G.nflags[0] >> 1;                       /* _DAT_004810f8 / _DAT_004810fc */
    G.flag_left[1] = G.nflags[1] >> 1;

    /* FUN_004320c0: vehicle stock defaults T3 J8 A3 H3, overridden by VHCL (asv, heli, jeep, tank, pool, mines). */
    int tank = 3, jeep = 8, asv = 3, heli = 3, pool = -1, mines = -1;
    if (w->vehicles[0] >= 0) asv = w->vehicles[0];
    if (w->vehicles[1] >= 0) heli = w->vehicles[1];
    if (w->vehicles[2] >= 0) jeep = w->vehicles[2];
    if (w->vehicles[3] >= 0) tank = w->vehicles[3];
    if (w->vehicles[4] >= 0) pool = w->vehicles[4];
    if (w->vehicles[5] >= 0) mines = w->vehicles[5];
    if (mines < 0 && G.nplayers == 2) mines = 0;
    if (mines < 0) mines = G.level > 5 ? G.level * 4 : 0;   /* level default, see docs/rfm.md */
    G.random_mines = mines;                                  /* placed by rules_level_start (FUN_0041c9b0) */
    G.ai_pool = pool;
    for (int t = 0; t < 2; t++) {
        Team *tm = &G.teams[t];
        memcpy(tm->loadout, loadout_default, sizeof tm->loadout);
        tm->loadout[12] = (uint8_t)tank; tm->loadout[13] = (uint8_t)jeep;
        tm->loadout[14] = (uint8_t)asv; tm->loadout[15] = (uint8_t)heli;
        tm->in_cur = &G.input_cur[t];
        tm->in_prev = &G.input_prev[t];
        tm->arrow_dir = -1;
    }
    for (int t = 0; t < 2; t++) memcpy(G.stock0[t], &G.teams[t].loadout[12], 4);
    rules_level_start(G.random_mines);                       /* StartNewGame: FUN_0041c9b0 before GameSetup1P */
    /* GameSetup1P: only team 0 gets a view; the game starts in the bunker vehicle select.
       GameSetup2P 0x41a750: team 1 gets view 2 (0x48bfdc), input word 2 and its own loadout copy. */
    G.teams[0].loadout[0] |= 1;
    G.views[0].team = 0;
    G.views[0].tm = &G.teams[0];
    G.teams[0].view = &G.views[0];
    if (G.nplayers > 1) {
        G.teams[1].loadout[0] |= 1;
        G.views[1].team = 1; G.views[1].tm = &G.teams[1]; G.teams[1].view = &G.views[1];
    }
    bunker_enter_select(&G.views[0]);
    if (G.nplayers > 1) bunker_enter_select(&G.views[1]);
    return true;
}

void game_frame(int32_t dt, uint32_t input_p1)
{
    g_dt = dt;                       /* FrameTimingUpdate 0x433220 of the previous frame */
    g_tick += dt;
    for (int t = 0; t < 2; t++) G.input_prev[t] = G.input_cur[t];   /* PollAllPlayerInputs 0x42fd30 */
    G.input_cur[0] = input_p1;
    /* Mus_Service 0x41d350: music */
    bunker_door_reset();
    timer_advance(dt);
    obj_update_all();
    if (!G.running) return;
    /* RadarUpdateMovers, Pal_CycleWater: render */
    bunker_view_update(&G.views[0]);
    if (G.nplayers > 1) bunker_view_update(&G.views[1]);
    frame_tasks_run();
    /* HudUpdateAll, Mus_Director, Snd_Service: not part of the simulation */
}

void game_frame_2p(int32_t dt, uint32_t input_p1, uint32_t input_p2)
{
    g_dt = dt;
    g_tick += dt;
    /* Mus_Service 0x41d350: music */
    bunker_door_reset();
    timer_advance(dt);
    obj_update_all();
    /* RadarUpdateMovers, Pal_CycleWater: render */
    for (int t = 0; t < 2; t++) G.input_prev[t] = G.input_cur[t];   /* PollAllPlayerInputs, after the objects */
    G.input_cur[0] = input_p1;
    G.input_cur[1] = input_p2;
    bunker_view_update(&G.views[0]);                                 /* (*0x48be50)(view 1) */
    bunker_view_update(&G.views[1]);                                 /* (*0x48c09c)(view 2) */
    if (!G.running) return;
    frame_tasks_run();
    /* HudUpdateAll(2), Mus_Director, Snd_Service: not part of the simulation */
}
