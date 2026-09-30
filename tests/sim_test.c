/* Headless simulation test: loads level 1, launches a tank from the bunker lift, drives it with
   scripted input words, then a jeep; prints positions/headings and collision/water events.
   Build (from the project root):
     clang -std=c11 -Isrc src/game/[a-z]*.c src/world.c src/world_tables.c src/assets.c src/exe.c src/render/models_data.c tests/sim_test.c -lm -o /tmp/sim_test
   Run from the project root (data under ./cd):  /tmp/sim_test [--map] */
#include "game/game.h"
#include "game/vehicle.h"
#include "world.h"
#include "input.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "exe.h"
#include "vfs_host.h"

static const char *MAP = "WORLDS/1PLAYER/LEVEL1/RFMAP001.RFM";
static int n_sounds;

static void on_sound(int cmd, uint32_t ev, Obj *o) { (void)o; if (cmd == 1 && ev) n_sounds++; }
static void on_log_expl(int team, const int32_t *p, uint32_t d)
{
    printf("    [explosion desc 0x%x team %d at %.1f,%.1f]\n", d, team, p[0] / 65536.0, p[1] / 65536.0);
}

static char cell_char(uint32_t w)
{
    int t = CELL_TERRAIN(w), b = CELL_BNO(w);
    if (b == BNO_BRIDGE_H || b == BNO_BRIDGE_V) return '=';
    if (b) {
        const BnoDef *d = &bno_defs[b];
        if (d->hit == BHIT_BUSH) return '*';
        if (d->hit == BHIT_ROCK) return 'o';
        if (d->destroy == BDES_WALL) return '#';
        if (d->destroy == BDES_TOWER) return 'T';
        if (d->hit == BHIT_PICKUP) return 'P';
        if (strstr(d->name, "PALM") || strstr(d->name, "TREE")) return 'Y';
        return 'B';
    }
    if (t == TERR_PAD0 || t == TERR_PAD1 || t == TERR_LIFT) return 'H';
    if (t == 2) return '~';
    if (t == 1) return ',';
    if (t >= 4 && t < 0x34) return ';';
    if (t >= 0x49 && t <= 0x53) return ':';
    return '.';
}

static void dump_map(int cx, int cy, int r)
{
    for (int y = cy - r; y <= cy + r; y++) {
        if (y < 0 || y > 127) continue;
        printf("  %3d ", y);
        for (int x = cx - r; x <= cx + r; x++) putchar(x < 0 || x > 127 ? ' ' : cell_char(G.cell[y * 128 + x]));
        putchar('\n');
    }
}

static const char *mode_name(BunkerMode m)
{
    static const char *n[] = { "SELECT", "LAUNCH", "ZOOM_IN", "PLAYING", "FADE_TO_SELECT", "FLYBACK" };
    return n[m];
}

static void report(const char *tag, Obj *v)
{
    if (!v) { printf("  %-10s tick %5d  (no vehicle)  view=%s sel=%d\n", tag, g_tick, mode_name(G.views[0].mode), G.views[0].sel); return; }
    VehState *s = veh_state(v);
    printf("  %-10s tick %5d %-4s pos=(%7.2f,%7.2f,%5.2f) cell=(%3d,%3d) '%c' hdg=%5.1f deg spd=%6.3f px/t"
           " fuel=%5.1f water=%d%s%s\n",
           tag, g_tick, s->def->name, v->pos[0] / 65536.0, v->pos[1] / 65536.0, v->pos[2] / 65536.0,
           v->pos[0] >> 21, v->pos[1] >> 21, cell_char(*v->cell), v->heading * (360.0 / 0x400000),
           v->speed / 65536.0, s->fuel / 65536.0, s->water,
           (v->flags & OF_BLOCKED) ? " BLOCKED" : "", s->water_hook && s->water_hook != veh_water_normal ? " (splash/sink)" : "");
}

/* ReadPlayerInput 0x401340 adds full-deflection magnitudes for digital keys. */
static uint32_t keys(uint32_t w)
{
    if (w & (IN_LEFT | IN_RIGHT)) w |= 0xff;
    if (w & (IN_UP | IN_DOWN)) w |= 0xff00;
    return w;
}

/* Run `n` frames of `dt` ticks with input word `in`; print every `every` frames. */
static Obj *run(const char *tag, int n, int dt, uint32_t in, int every)
{
    Obj *v = NULL;
    in = keys(in);
    for (int i = 0; i < n; i++) {
        game_frame(dt, in);
        v = get_team_vehicle(0);
        if (every && (i % every == every - 1)) report(tag, v);
        if (G.winner != -2) break;
    }
    return v;
}

/* Turn (while pressing `also`) until the heading is within 3 degrees of `deg`. */
static Obj *steer(Obj *v, double deg, uint32_t also)
{
    for (int i = 0; i < 400 && v; i++) {
        double h = v->heading * (360.0 / 0x400000), d = deg - h;
        while (d > 180) d -= 360;
        while (d < -180) d += 360;
        if (d > -3 && d < 3) break;
        v = run("steer", 1, 1, also | (d > 0 ? IN_RIGHT : IN_LEFT), 0);
    }
    return v;
}

static Obj *launch(int type)
{
    BunkerView *bv = &G.views[0];
    /* wait in the select screen until the fade-in completes, pick the type with up/down/left/right */
    run("select", 20, 1, 0, 0);
    for (int i = 0; i < 300 && obj_count_active(); i++) run("select", 1, 1, 0, 0);  /* old lift still going down */
    for (int guard = 0; guard < 8 && bv->sel != type; guard++) {
        uint32_t k = (type == VT_JEEP || type == VT_MSV) ? IN_DOWN : IN_UP;
        if ((bv->sel == VT_TANK || bv->sel == VT_JEEP) && (type == VT_MSV || type == VT_HELI)) k = IN_LEFT;
        if ((bv->sel == VT_MSV || bv->sel == VT_HELI) && (type == VT_TANK || type == VT_JEEP)) k = IN_RIGHT;
        run("select", 1, 1, k, 0);
    }
    printf("  selected %s (stock T%d J%d M%d H%d), fade=0x%x\n", vehicle_defs[bv->sel].name,
           TEAM_STOCK(&G.teams[0], 0), TEAM_STOCK(&G.teams[0], 1), TEAM_STOCK(&G.teams[0], 2), TEAM_STOCK(&G.teams[0], 3), bv->fade);
    run("select", 1, 1, IN_BTN1, 0);          /* fire = launch */
    int t0 = g_tick;
    Obj *v = NULL;
    for (int i = 0; i < 2000 && !(v = get_team_vehicle(0)); i++) run("lift", 1, 1, 0, 0);
    printf("  lift: script+rise took %d ticks; vehicle spawned\n", g_tick - t0);
    return v;
}

int main(int argc, char **argv)
{
    vfs_mount_default();                                                  /* ./cd or $OPENRF_DATA */
    if (!exe_load()) { fprintf(stderr, "%s\n", exe_error()); return 1; }   /* tables from RFIRE.BIN */
    World w;
    if (!world_load(&w, MAP)) { fprintf(stderr, "world_load failed (run from the project root, data in ./cd)\n"); return 1; }
    game_hooks.sound = on_sound;
    game_hooks.explosion = on_log_expl;
    if (!game_load(&w, MAP)) { fprintf(stderr, "game_load failed\n"); return 1; }
    PadRec *pad = G.teams[0].pad;
    int px = pad->x >> 21, py = pad->y >> 21;
    printf("map '%s' level %d: pad (%d,%d) terrain %u, flags(team1)=%d, stock T%d J%d M%d H%d mines %d\n",
           w.name, G.level + 1, px, py, G.teams[0].pad_terrain, G.nflags[1], TEAM_STOCK(&G.teams[0], 0),
           TEAM_STOCK(&G.teams[0], 1), TEAM_STOCK(&G.teams[0], 2), TEAM_STOCK(&G.teams[0], 3), TEAM_MINES(&G.teams[0]));
    printf("legend: H pad  . land  : road  , shallow  ; shore  ~ deep  = bridge  # wall  T tower  * bush  Y palm  o rock  B building  P pickup\n");
    dump_map(px, py, argc > 1 && !strcmp(argv[1], "--map") ? 30 : 12);

    printf("\n== 1. TANK: launch, back onto the pad, dock (stock returns) ==\n");
    Obj *v = launch(VT_TANK);
    report("spawned", v);
    v = run("off-lift", 15, 1, 0, 5);           /* launch hook forces full forward for 15 ticks */
    v = run("coast", 30, 1, 0, 10);
    for (int i = 0; i < 400 && v; i++) {        /* reverse until within the dock radius of the pad centre */
        int32_t dy = v->pos[1] - pad->y;
        uint32_t in = dy > 3 * 65536 ? IN_DOWN : 0;
        if (!in && v->speed == 0) break;
        v = run("back", 1, 1, in, 0);
    }
    report("on pad", v);
    v = run("dock", 1, 1, IN_BTN1, 1);          /* fire on the pad = DockVehicleInBunker */
    for (int i = 0; i < 400 && G.views[0].mode != BV_SELECT; i++) run("lower", 1, 1, 0, 0);
    printf("  docked: view %s, tank stock %d, tick %d\n", mode_name(G.views[0].mode), TEAM_STOCK(&G.teams[0], 0), g_tick);

    printf("\n== 2. TANK: turn west in place, drive into the palms (blocked), back, south: crush a bush, sink ==\n");
    v = launch(VT_TANK);
    v = run("off-lift", 45, 1, 0, 15);
    v = run("turn-R", 64, 1, IN_RIGHT, 16);     /* 0x4000/tick = 1.41 deg/tick */
    v = run("fwd-W", 90, 2, IN_UP, 10);         /* dt = 2 (variable timestep) */
    for (int i = 0; i < 300 && v && v->pos[0] < pad->x - 2 * 65536; i++) v = run("rev-E", 1, 2, IN_DOWN, 0);
    v = run("stop", 12, 2, 0, 12);
    v = run("turn-L", 32, 2, IN_LEFT, 8);       /* back to south */
    v = run("turret-Q", 10, 2, IN_BTN4, 10);
    if (v) printf("  turret yaw %.1f deg (relative to hull)\n", (uint32_t)veh_state(v)->turret * (360.0 / 0x400000));
    for (int i = 0; i < 400 && v; i++) v = run("fwd-S", 1, 3, IN_UP, 0), (v && i % 8 == 7) ? report("fwd-S", v) : (void)0;
    printf("  tank lost in deep water; waiting for the bunker\n");
    for (int i = 0; i < 1500 && G.views[0].mode != BV_SELECT; i++) run("wait", 1, 2, 0, 0);
    printf("  view mode now %s, tick %d, stock T%d\n", mode_name(G.views[0].mode), g_tick, TEAM_STOCK(&G.teams[0], 0));

    printf("\n== 3. JEEP: steer east into a bush (jeeps are blocked), south to the sea, TireOut (boat mode) ==\n");
    v = launch(VT_JEEP);
    report("spawned", v);
    v = run("start", 100, 1, 0, 25);            /* engine start delay 0x62 ticks, then half speed */
    v = steer(v, 90.0, IN_UP);                  /* the jeep only turns while moving */
    report("east", v);
    for (int i = 0; i < 300 && v && !(v->flags & OF_BLOCKED); i++) v = run("fwd-E", 1, 2, IN_UP, 0), (v && i % 10 == 9) ? report("fwd-E", v) : (void)0;
    report("blocked", v);
    for (int i = 0; i < 300 && v && v->pos[0] > (76 * 32 + 16) * 65536; i++) v = run("rev-W", 1, 2, IN_DOWN, 0);
    report("rev-W", v);
    v = steer(v, 180.0, IN_UP);
    for (int i = 0; i < 300 && v && veh_state(v)->water == 0; i++) v = run("to-water", 1, 2, IN_UP, 0), (v && i % 10 == 9) ? report("to-water", v) : (void)0;
    report("in water", v);
    v = run("B=boat", 1, 1, IN_BTN2, 1);        /* B = TireOut when in water */
    v = run("morph", 40, 2, 0, 10);             /* control forced idle while the tyres retract */
    v = run("boat-fwd", 200, 3, IN_UP, 20);     /* deep water no longer sinks the jeep */
    v = run("boat-turn", 30, 2, IN_RIGHT, 10);
    if (v) {
        v = run("self-destruct", 1, 1, IN_BTN1 | IN_BTN2 | IN_BTN3, 1);   /* all three buttons */
        for (int i = 0; i < 1500 && G.views[0].mode != BV_SELECT; i++) run("wait", 1, 2, 0, 0);
    }
    printf("  view mode now %s, tick %d, stock J%d\n", mode_name(G.views[0].mode), g_tick, TEAM_STOCK(&G.teams[0], 1));

    printf("\n== 4. HELI: spin-up, take-off, fly over trees and water ==\n");
    v = launch(VT_HELI);
    v = run("spin-up", 150, 1, 0, 25);
    v = run("climb", 60, 2, 0, 15);
    v = run("fwd", 200, 2, IN_UP, 20);
    v = run("turn-L", 40, 2, IN_UP | IN_LEFT, 10);
    v = run("hover", 60, 2, 0, 20);
    printf("\nobjects active: %d, sound events queued: %d, winner %d\n", obj_count_active(), n_sounds, G.winner);
    return 0;
}
