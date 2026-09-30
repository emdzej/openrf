/* Headless combat test: level 1 with a cleared test range east of the pad; vehicles are spawned directly
   (SpawnVehicle) and shoot a bush, a wall (damage states of the neighbours), a building (destroyed
   variant + rubble), a tower (turret debris); the jeep throws grenades, the MSV launches rockets, the heli
   fires guns and missiles. Prints the projectile flights, hits, explosions, debris and the cells after.
   Run from the project root (data in ./cd):  build/combat_test */
#include "game/game.h"
#include "game/vehicle.h"
#include "game/weapon.h"
#include "game/effect.h"
#include "world.h"
#include "input.h"
#include <stdio.h>
#include <string.h>
#include "exe.h"

static const char *MAP = "WORLDS/1PLAYER/LEVEL1/RFMAP001.RFM";
static int n_expl, n_snd, n_flag;
static Obj *on_flag(int team, const int32_t *p)
{
    n_flag++;
    printf("      [flag_spawn hook: team %d at (%.1f, %.1f)]\n", team, p[0] / 65536.0, p[1] / 65536.0);
    return NULL;
}

static void on_sound(int cmd, uint32_t ev, Obj *o) { (void)o; if (cmd == 1 && ev) n_snd++; }
static void on_expl(int team, const int32_t *p, uint32_t d)
{
    n_expl++;
    printf("      explosion 0x%06x team %d at (%.1f, %.1f, %.1f)\n", d, team, p[0] / 65536.0, p[1] / 65536.0, p[2] / 65536.0);
}

static uint32_t keys(uint32_t w)
{
    if (w & (IN_LEFT | IN_RIGHT)) w |= 0xff;
    if (w & (IN_UP | IN_DOWN)) w |= 0xff00;
    return w;
}

typedef struct { int id, n; } Cnt;
static void visit(Obj *o, void *ctx) { Cnt *c = ctx; if (o->cls->id == c->id && !(o->flags & OF_DELETE)) c->n++; }
static int count_live(int id)
{
    Cnt c = { id, 0 };
    obj_foreach_live(visit, &c);
    return c.n;
}
#define count_class count_live

static uint32_t *cell_xy(int x, int y) { return &G.cell[y * 128 + x]; }
static const char *bno_name(uint32_t w) { int b = CELL_BNO(w); return b ? bno_defs[b].name : "(none)"; }

static void show_cell(const char *tag, int x, int y)
{
    uint32_t w = *cell_xy(x, y);
    printf("    %-8s (%3d,%3d) bno %2u %-20s hp %u team %u terrain 0x%02x\n", tag, x, y, CELL_BNO(w), bno_name(w),
           CELL_HP(w), CELL_TEAM(w), CELL_TERRAIN(w));
}

/* Advance one tick with input; print every projectile / grenade that moved. */
static int debris_peak, expl_peak;
static void step(uint32_t in, int verbose)
{
    game_frame(1, keys(in));
    if (count_live(3) > debris_peak) debris_peak = count_live(3);
    if (count_live(11) > expl_peak) expl_peak = count_live(11);
    if (!verbose) return;
    for (int i = 1; i < OBJ_POOL; i++) {
        Obj *o = &obj_pool[i];
        if (!o->next || !o->cls || !(o->cls->id == 0 || o->cls->id == 16 || o->cls->id == 18)) continue;
        if (o->flags == 0) continue;
        if (verbose > 1 || (g_tick % 8) == 0)
            printf("      t=%5d %-8s #%03x pos=(%7.1f,%7.1f,%5.1f) hdg %5.1f pitch 0x%06x\n", g_tick, o->cls->name, o->id & 0x1ff,
                   o->pos[0] / 65536.0, o->pos[1] / 65536.0, o->pos[2] / 65536.0, o->heading * (360.0 / 0x400000),
                   o->cls->id == 18 ? 0 : (uint32_t)o->i60);
    }
}

static void run(int n, uint32_t in, int verbose) { for (int i = 0; i < n; i++) step(in, verbose); }

/* Flatten a box of cells to grass, no objects, team 0. */
static void clear_range(int x0, int y0, int x1, int y1, int terrain)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            uint32_t *c = cell_xy(x, y);
            *c &= 0xffff0000u;
            CELL_SET_TERRAIN(*c, terrain);
        }
}

static Obj *spawn_at(int type, int cx, int cy, uint32_t heading)
{
    Obj *v = get_team_vehicle(0);
    if (v) obj_destroy_now(v);
    int32_t p[3] = { cx * 0x200000 + 0x100000, cy * 0x200000 + 0x100000, 0 };
    v = spawn_vehicle(0, p, heading, type);
    run(1, 0, 0);                               /* first update: ammo, fuel */
    return v;
}

/* Tank / MSV: idle the launch hook off, then roll back to the cell centre. */
static void settle(Obj *v, int cx)
{
    run(20, 0, 0);
    for (int i = 0; i < 400; i++) {
        int32_t dx = v->pos[0] - (cx * 0x200000 + 0x100000);
        if (dx > 0x10000) run(1, IN_DOWN, 0);
        else if (v->speed != 0) run(1, 0, 0);
        else break;
    }
}

static int shoot_until(Obj *v, uint32_t btn, int tx, int ty, int max_shots)
{
    uint32_t w0 = *cell_xy(tx, ty);
    int shots = 0;
    debris_peak = expl_peak = 0;
    VehState *s = veh_state(v);
    int slot = btn == IN_BTN2 && s->def->type == VT_HELI ? 0 : 0;
    for (int k = 0; k < max_shots; k++) {
        int a0 = s->slot[slot].ammo;
        step(btn, 1);                            /* press */
        run(2, 0, 1);                            /* release */
        if (s->slot[slot].ammo < a0) shots++;
        for (int i = 0; i < 60; i++) {
            step(0, 1);
            if (!count_class(0) && !count_class(18)) break;
        }
        uint32_t w = *cell_xy(tx, ty);
        printf("    shot %d: ammo %d, target hp %u, bno %s\n", shots, s->slot[slot].ammo, CELL_HP(w), bno_name(w));
        if (CELL_BNO(w) != CELL_BNO(w0)) break;
        run(20, 0, 0);
    }
    run(120, 0, 0);                              /* explosion scripts finish (delayed swaps) */
    printf("    (during the volley: up to %d debris pieces, %d explosion objects at once)\n", debris_peak, expl_peak);
    return shots;
}

int main(void)
{
    if (!exe_load()) { fprintf(stderr, "%s\n", exe_error()); return 1; }   /* tables from RFIRE.BIN */
    World w;
    if (!world_load(&w, MAP)) { fprintf(stderr, "world_load failed (run from the project root)\n"); return 1; }
    game_hooks.sound = on_sound;
    game_hooks.explosion = on_expl;
    game_hooks.flag_spawn = on_flag;
    if (!game_load(&w, MAP)) { fprintf(stderr, "game_load failed\n"); return 1; }
    G.views[0].mode = BV_PLAYING;                /* no bunker screen: vehicles are spawned directly */
    int grass = CELL_TERRAIN(*cell_xy(70, 66));
    printf("level '%s'; test range cells (72..90, 62..74) cleared to terrain 0x%02x\n", w.name, grass);
    clear_range(72, 62, 90, 74, grass);

    printf("\n== 1. TANK shoots a bush (cell 80,64) ==\n");
    set_cell_static(1, cell_xy(80, 64), 0, 2);
    show_cell("before", 80, 64);
    Obj *v = spawn_at(VT_TANK, 75, 64, 0x100000);          /* facing east */
    settle(v, 75);
    printf("    tank at (%.1f, %.1f) turret %u elev %d ammo %d\n", v->pos[0] / 65536.0, v->pos[1] / 65536.0,
           (unsigned)veh_state(v)->turret, veh_state(v)->elev, veh_state(v)->slot[0].ammo);
    int n = shoot_until(v, IN_BTN1, 80, 64, 3);
    show_cell("after", 80, 64);
    printf("    shots %d, debris now %d, stay %d\n", n, count_live(3), count_live(17));

    printf("\n== 2. TANK shoots the middle of a 3-cell wall (80,67..69): damage states ==\n");
    for (int y = 67; y <= 69; y++) set_cell_static(45, cell_xy(80, y), 0, 1);   /* BNO_WALL_V */
    v = spawn_at(VT_TANK, 75, 68, 0x100000);
    settle(v, 75);
    n = shoot_until(v, IN_BTN1, 80, 68, 8);
    for (int y = 67; y <= 69; y++) show_cell(y == 68 ? "hit" : "neighbour", 80, y);
    printf("    shots %d, debris now %d\n", n, count_live(3));

    printf("\n== 3. TANK shoots a building (80,72): destroyed variant + rubble ==\n");
    set_cell_static(24, cell_xy(80, 72), 0, 1);          /* BNO_BLDG_S */
    show_cell("before", 80, 72);
    v = spawn_at(VT_TANK, 75, 72, 0x100000);
    settle(v, 75);
    n = shoot_until(v, IN_BTN1, 80, 72, 10);
    show_cell("after", 80, 72);
    printf("    shots %d, debris now %d (building faces with debris class bits)\n", n, count_live(3));

    printf("\n== 4. TANK shoots a tower (84,64); then one lobbed shell (B, raised barrel) ==\n");
    set_cell_static(49, cell_xy(84, 64), 0, 1);          /* BNO_TOWER */
    show_cell("before", 84, 64);
    v = spawn_at(VT_TANK, 79, 64, 0x100000);
    settle(v, 79);
    n = shoot_until(v, IN_BTN1, 84, 64, 8);
    show_cell("after", 84, 64);
    printf("    shots %d, debris right after %d\n", n, count_live(3));
    for (int i = 0; i < 40; i++) step(IN_BTN2, 0);       /* raise the barrel (hold B) */
    printf("    elevation 0x%x target 0x%x\n", veh_state(v)->elev, veh_state(v)->elev_target);
    step(0, 0); step(IN_BTN2, 0);
    {
        int32_t zmax = 0, x0 = v->pos[0];
        Obj *sh = NULL;
        for (int i = 1; i < OBJ_POOL; i++) if (obj_pool[i].next && obj_pool[i].cls == &class_missle && obj_pool[i].flags) sh = &obj_pool[i];
        for (int i = 0; i < 100 && sh && sh->cls == &class_missle && sh->flags; i++) {
            if (sh->pos[2] > zmax) zmax = sh->pos[2];
            if (i % 10 == 0) printf("      lob t+%2d x %+6.1f z %5.1f pitch 0x%06x\n", i, (sh->pos[0] - x0) / 65536.0, sh->pos[2] / 65536.0, (uint32_t)sh->i60);
            step(0, 0);
        }
        printf("    lob apex %.1f px (the pitch is turned towards 0 = level; no fall below it)\n", zmax / 65536.0);
    }

    printf("\n== 5. JEEP bumps a bush (80,70) (jeeps are blocked by bushes), backs off and throws grenades at it ==\n");
    clear_range(72, 70, 90, 70, grass);
    set_cell_static(1, cell_xy(80, 70), 0, 2);           /* BNO_BUSH_1S */
    show_cell("before", 80, 70);
    v = spawn_at(VT_JEEP, 77, 70, 0x100000);
    run(110, 0, 0);                                     /* engine start */
    for (int i = 0; i < 80 && !(v->flags & OF_BLOCKED); i++) run(1, IN_UP, 0);   /* bump: state+0xa4 = cell */
    printf("    jeep at (%.1f, %.1f) blocked=%d bumped cell %s\n", v->pos[0] / 65536.0, v->pos[1] / 65536.0,
           !!(v->flags & OF_BLOCKED), veh_state(v)->hit_cell ? bno_name(*veh_state(v)->hit_cell) : "-");
    run(12, IN_DOWN, 0);
    run(40, 0, 0);
    n = shoot_until(v, IN_BTN1, 80, 70, 8);
    show_cell("after", 80, 70);
    printf("    grenades %d, jeep ammo %d, debris %d\n", n, veh_state(v)->slot[0].ammo, count_live(3));

    printf("\n== 6. MSV fires rockets (A flat) at a factory (82,66) (they fly over bushes) ==\n");
    clear_range(72, 66, 90, 66, grass);
    set_cell_static(27, cell_xy(82, 66), 0, 1);          /* BNO_FACT_N */
    v = spawn_at(VT_MSV, 77, 66, 0x100000);
    settle(v, 77);
    n = shoot_until(v, IN_BTN1, 82, 66, 8);
    show_cell("after", 82, 66);
    printf("    rockets %d, launcher 0x%x\n", n, (uint32_t)veh_state(v)->turret);

    printf("\n== 7. HELI: take off, guns (A, pitched down) at an outpost, then missiles (C selects, A drops them) at a building ==\n");
    clear_range(72, 62, 90, 74, grass);
    set_cell_static(35, cell_xy(77, 68), 0, 1);          /* BNO_OUTPOST (2 hp) */
    v = spawn_at(VT_HELI, 74, 68, 0x100000);
    for (int i = 0; i < 600 && v->pos[2] < 0x320000; i++) run(1, 0, 0);
    for (int i = 0; i < 300; i++) {                      /* yaw to east, then drift to a stop */
        int32_t d = (int32_t)((0x100000 - v->heading) & 0x3fffff);
        if (d > 0x200000) d -= 0x400000;
        if (d > -0x8000 && d < 0x8000) break;
        run(1, d > 0 ? IN_RIGHT : IN_LEFT, 0);
    }
    run(80, 0, 0);
    printf("    heli at (%.1f, %.1f, %.1f) hdg %.1f\n", v->pos[0] / 65536.0, v->pos[1] / 65536.0, v->pos[2] / 65536.0,
           v->heading * (360.0 / 0x400000));
    n = shoot_until(v, IN_BTN1, 77, 68, 8);
    show_cell("outpost", 77, 68);
    set_cell_static(23, cell_xy(77, 68), 0, 1);          /* a building where the bush was */
    step(IN_BTN3, 0); run(2, 0, 0);                      /* C: guns -> missiles */
    printf("    weapon slot %u selected, missiles %d\n", (v->flags & 0x10000000) >> 28, veh_state(v)->slot[1].ammo);
    for (int k = 0; k < 8 && CELL_BNO(*cell_xy(77, 68)) == 23; k++) {
        int a0 = veh_state(v)->slot[1].ammo;
        step(IN_BTN1, 1); run(2, 0, 1);
        for (int i = 0; i < 80 && count_class(0); i++) step(0, 1);
        printf("    missile: ammo %d -> %d, building hp %u\n", a0, veh_state(v)->slot[1].ammo, CELL_HP(*cell_xy(77, 68)));
        run(30, 0, 0);
    }
    run(120, 0, 0);
    show_cell("building", 77, 68);

    printf("\n== 7b. HELI guns at bridge decks (77,67..68): planks fall as debris ==\n");
    step(IN_BTN3, 0); run(2, 0, 0);                      /* back to guns */
    set_cell_static(74, cell_xy(77, 67), 0, 1);
    set_cell_static(74, cell_xy(77, 68), 0, 1);
    n = shoot_until(v, IN_BTN1, 77, 68, 20);
    show_cell("deck", 77, 67);
    show_cell("deck", 77, 68);

    printf("\n== 7c. TANK shoots flag buildings (flag hook stub) ==\n");
    clear_range(72, 62, 90, 74, grass);
    set_cell_static(22, cell_xy(80, 64), 0, 1);           /* BNO_FLAG, enemy side */
    G.flag_cell[1] = cell_xy(80, 64);                    /* pretend the flag is hidden here */
    G.flag_left[1] = 0;
    v = spawn_at(VT_TANK, 75, 64, 0x100000);
    settle(v, 75);
    n = shoot_until(v, IN_BTN1, 80, 64, 10);
    show_cell("after", 80, 64);
    printf("    flag_spawn hook calls %d (Flag class not ported)\n", n_flag);

    printf("\n== 8. vehicle damage: shells at an enemy tank until it is a wreck ==\n");
    clear_range(72, 62, 90, 74, grass);
    int32_t ep[3] = { 81 * 0x200000 + 0x100000, 64 * 0x200000 + 0x100000, 0 };
    v = spawn_at(VT_TANK, 75, 64, 0x100000);
    Obj *e = spawn_vehicle(1, ep, 0x300000, VT_TANK);
    settle(v, 75);
    for (int k = 0; k < 40 && e && G.cur_vehicle[1] == e; k++) {
        step(IN_BTN1, 0); run(2, 0, 0);
        for (int i = 0; i < 40 && count_class(0); i++) step(0, 0);
        if (G.cur_vehicle[1] == e) printf("    hit %2d: enemy hp %.2f armour %.2f\n", k + 1, veh_state(e)->hp / 65536.0, veh_state(e)->armour / 65536.0);
        run(18, 0, 0);
    }
    printf("    enemy vehicle %s; wrecks %d\n", G.cur_vehicle[1] ? "alive" : "destroyed", count_live(6));
    for (int i = 0; i < 12; i++) {
        run(10, 0, 0);
        printf("      tick %5d: wrecks %d stay %d debris %d expl %d\n", g_tick, count_live(6), count_live(17), count_live(3), count_live(11));
    }
    run(200, 0, 0);
    printf("    after 200 ticks: wrecks %d, stay %d, debris %d, expl %d\n", count_live(6), count_live(17), count_live(3), count_live(11));

    printf("\nexplosions spawned %d, sound events %d, live objects %d\n", n_expl, n_snd, obj_count_active());
    return 0;
}
