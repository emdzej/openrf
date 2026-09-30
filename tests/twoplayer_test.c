/* Headless 2-player test (GameSetup2P 0x41a750 / GameFrame2P 0x41aa70) on "Driving School"
   (WORLDS/2PLAYER/LEVEL1/RFMAP101.RFM):
   1. both sides start in their bunker select; player 1 launches a tank, player 2 picks the jeep and
      launches it (the views read this frame's input, the vehicles last frame's: GameFrame2P order),
   2. both drive off their pads,
   3. a tower of side 0 seen in player 2's view wakes up against player 2's vehicle, the drone hunts it,
   4. MSV mines are laid (2-player only, Msv_Mine 0x42a310),
   5. player 2's jeep carries player 1's flag onto its pad -> JeepFlagCheck 0x42a480 -> EndGame(1),
   6. side 0 loses its last vehicle while side 1 still has jeeps -> fly-back -> spectate (FUN_00427d70 == 1),
   7. neither side has a jeep -> EndGame(-1) (draw), 8. the 2-player high-score records (RecordHighScore 0x42d8b0).
   Returns non-zero when a check fails. Run from the project root (data in ./cd): build/twoplayer_test */
#include "game/game.h"
#include "game/vehicle.h"
#include "game/weapon.h"
#include "game/rules.h"
#include "game/ai.h"
#include "world.h"
#include "input.h"
#include "endgame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exe.h"

static const char *MAP = "WORLDS/2PLAYER/LEVEL1/RFMAP101.RFM";
static int fails;
#define CHECK(c, ...) do { if (c) printf("  ok   " __VA_ARGS__); else { printf("  FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

static int ends, end_winner = -3;
static void on_end(int w) { ends++; end_winner = w; }

static uint32_t keys(uint32_t w)
{
    if (w & (IN_LEFT | IN_RIGHT)) w |= 0xff;
    if (w & (IN_UP | IN_DOWN)) w |= 0xff00;
    return w;
}
static void step(uint32_t a, uint32_t b) { game_frame_2p(1, keys(a), keys(b)); }
static void run(int n, uint32_t a, uint32_t b) { for (int i = 0; i < n; i++) step(a, b); }
static double px(int32_t v) { return v / 65536.0; }

static const char *mode_name(BunkerMode m)
{
    static const char *n[] = { "SELECT", "LAUNCH", "ZOOM_IN", "PLAYING", "FADE_TO_SELECT", "FLYBACK", "SPECTATE" };
    return m <= BV_SPECTATE ? n[m] : "?";
}
static const char *type_name(int t) { static const char *n[] = { "Tank", "Jeep", "MSV", "Heli" }; return t >= 0 && t < 4 ? n[t] : "?"; }

typedef struct { const ObjClass *c; int team, n; } Cnt;
static void visit(Obj *o, void *ctx) { Cnt *c = ctx; if (o->cls == c->c && (c->team < 0 || o->team == c->team) && !(o->flags & OF_DELETE)) c->n++; }
static int count(const ObjClass *c, int team) { Cnt k = { c, team, 0 }; obj_foreach_live(visit, &k); return k.n; }

static bool load(World *w)
{
    if (!world_load(w, MAP)) { fprintf(stderr, "world_load %s failed (run from the project root, data in ./cd)\n", MAP); return false; }
    if (!game_load(w, MAP)) { fprintf(stderr, "game_load %s failed\n", MAP); return false; }
    ai_level_start();
    game_hooks.end_game = on_end;
    ends = 0; end_winner = -3;
    return true;
}

static void show(const char *tag, int t)
{
    Obj *v = get_team_vehicle(t);
    const BunkerView *bv = &G.views[t];
    if (!v) { printf("    %-8s P%d tick %4d  view %-8s sel %d  (no vehicle)\n", tag, t + 1, g_tick, mode_name(bv->mode), bv->sel); return; }
    printf("    %-8s P%d tick %4d  view %-8s %s team %d at (%.1f, %.1f) heading %d\n", tag, t + 1, g_tick, mode_name(bv->mode),
           v->cls->id == 1 ? type_name(veh_type(v)) : v->cls->name, v->team, px(v->pos[0]), px(v->pos[1]), (int)(v->heading >> 16));
}

int main(void)
{
    if (!exe_load()) { fprintf(stderr, "%s\n", exe_error()); return 1; }   /* tables from RFIRE.BIN */
    static World w;
    if (!load(&w)) return 1;
    printf("level '%s' (%s): players %d, level %d\n", w.name, MAP, G.nplayers, G.level + 1);
    for (int t = 0; t < 2; t++)
        printf("  side %d: pad (%d,%d) of %d, flag sites %d, stock T%d J%d M%d H%d mines %d\n", t, G.teams[t].pad->x >> 21,
               G.teams[t].pad->y >> 21, G.teams[t].npads, G.nflags[t], TEAM_STOCK(&G.teams[t], 0), TEAM_STOCK(&G.teams[t], 1),
               TEAM_STOCK(&G.teams[t], 2), TEAM_STOCK(&G.teams[t], 3), TEAM_MINES(&G.teams[t]));
    CHECK(G.nplayers == 2 && G.views[0].tm == &G.teams[0] && G.views[1].tm == &G.teams[1] && G.teams[1].view == &G.views[1],
          "GameSetup2P: two views, one per side");
    CHECK(G.views[0].mode == BV_SELECT && G.views[1].mode == BV_SELECT, "both start in the bunker select (sel %d / %d)",
          G.views[0].sel, G.views[1].sel);
    CHECK(G.teams[1].in_cur == &G.input_cur[1], "side 1 reads input word 2");

    printf("\n== 1. both players launch ==\n");
    run(20, 0, 0);                                   /* fade in */
    step(0, IN_DOWN);                                /* player 2: tank -> jeep */
    step(0, 0);
    CHECK(G.views[0].sel == VT_TANK && G.views[1].sel == VT_JEEP, "selections: P1 %s, P2 %s", type_name(G.views[0].sel),
          type_name(G.views[1].sel));
    step(IN_BTN1, IN_BTN1);
    CHECK(G.views[0].mode == BV_LAUNCH && G.views[1].mode == BV_LAUNCH, "button 1 starts both lifts on the same frame");
    int t;
    for (t = 0; t < 2000 && !(get_team_vehicle(0) && get_team_vehicle(1)); t++) step(0, 0);   /* lifts rise */
    show("out", 0); show("out", 1);
    Obj *v0 = get_team_vehicle(0), *v1 = get_team_vehicle(1);
    CHECK(v0 && v1 && veh_type(v0) == VT_TANK && veh_type(v1) == VT_JEEP && v0->team == 0 && v1->team == 1,
          "both vehicles up after %d ticks, stock P1 T%d, P2 J%d", t, TEAM_STOCK(&G.teams[0], 0), TEAM_STOCK(&G.teams[1], 1));
    if (!v0 || !v1) return 1;

    printf("\n== 2. both drive off their pads ==\n");
    int32_t p0[3], p1[3];
    memcpy(p0, v0->pos, sizeof p0); memcpy(p1, v1->pos, sizeof p1);
    run(150, IN_UP, IN_UP);
    show("drive", 0); show("drive", 1);
    CHECK(dist2d(v0->pos, p0) > 0x100000 && dist2d(v1->pos, p1) > 0x100000, "P1 moved %.1f px, P2 moved %.1f px",
          px(dist2d(v0->pos, p0)), px(dist2d(v1->pos, p1)));
    run(60, 0, 0);
    /* GameFrame2P polls after ObjUpdateAll: a one-frame press reaches the vehicle on the next frame. */
    uint32_t c0 = veh_state(v0)->ctrl & 0xffff;
    step(IN_UP, 0);
    uint32_t c1 = veh_state(v0)->ctrl & 0xffff;
    step(0, 0);
    uint32_t c2 = veh_state(v0)->ctrl & 0xffff;
    CHECK(!(c1 & CW_FWD) && (c2 & CW_FWD), "input lag: control word 0x%x -> 0x%x -> 0x%x", c0, c1, c2);

    printf("\n== 3. turrets and drones go for either player ==\n");
    int tx = (v1->pos[0] >> 21) + 3, ty = v1->pos[1] >> 21;
    uint32_t *tc = &G.cell[ty * 128 + tx];
    set_cell_static(49, tc, 0, 0);                   /* a side-0 tower next to player 2 */
    CHECK(turret_tower_hook(tc, 0) == NULL, "player 1's view: own tower stays static");
    Obj *tur = turret_tower_hook(tc, 1);
    CHECK(tur && tur->team == 0 && tur->parent == v1, "player 2's view: turret team %d wakes up against the P2 %s",
          tur ? tur->team : -1, type_name(veh_type(v1)));
    drone_pool_init(3);
    Obj *dr = spawn_drone(v1);
    CHECK(dr && dr->team == 0, "idle drone against player 2 belongs to side %d", dr ? dr->team : -1);
    Obj *dr2 = spawn_drone(v0);
    CHECK(dr2 && dr2->team == 1, "idle drone against player 1 belongs to side %d", dr2 ? dr2->team : -1);

    printf("\n== 4. MSV mines (2 players only) ==\n");
    if (!load(&w)) return 1;
    G.views[0].mode = G.views[1].mode = BV_PLAYING;
    int32_t mp[3] = { G.teams[0].pad->x, G.teams[0].pad->y + 0x600000, 0 };
    Obj *msv = spawn_vehicle(0, mp, 0x200000, VT_MSV);
    run(2, 0, 0);
    int mines0 = count(&class_mine, -1), ammo0 = veh_state(msv)->slot[1].ammo;
    for (int k = 0; k < 4; k++) {                    /* C: Msv_Mine (reload 140 ticks); move on between mines */
        run(150, 0, 0);
        step(IN_BTN3, 0);
        step(0, 0);
        run(10, 0, 0);
        obj_move_to(msv, mp[0] + (k & 1) * 0x600000, mp[1] + (k >> 1) * 0x600000, 0);
    }
    int laid = count(&class_mine, -1) - mines0;
    CHECK(laid == 4 && veh_state(msv)->slot[1].ammo == ammo0 - 4, "%d mines laid (ammo %d -> %d, team mines %d)", laid, ammo0,
          veh_state(msv)->slot[1].ammo, TEAM_MINES(&G.teams[0]));

    printf("\n== 5. player 2 brings player 1's flag home ==\n");
    if (!load(&w)) return 1;
    G.views[0].mode = G.views[1].mode = BV_PLAYING;
    const PadRec *pad1 = G.teams[1].pad;
    int32_t jp[3] = { pad1->x, pad1->y + 0x600000, 0 };
    Obj *jeep = spawn_vehicle(1, jp, 0, VT_JEEP);
    run(2, 0, 0);
    int32_t fp[3] = { jeep->pos[0], jeep->pos[1] - 0x300000, 0 };
    Obj *flag = flag_spawn(0, fp);                   /* as if its building had been blown up */
    CHECK(flag && G.flag_obj[0] == flag && flag->team == 0, "side 0 flag lying %.0f px north of the P2 jeep", px(0x300000));
    for (t = 0; t < 400 && flag && flag->parent != jeep; t++) step(0, t < 5 ? 0 : IN_UP);
    CHECK(flag && flag->parent == jeep, "picked up after %d ticks (music bits 0x%x)", t, rules_music_bits);
    CHECK(G.winner == -2, "no win yet (terrain 0x%02x)", CELL_TERRAIN(*jeep->cell));
    obj_move_to(jeep, pad1->x, pad1->y + 0x400000, 0);
    jeep->heading = 0;
    for (t = 0; t < 300 && G.winner == -2; t++) step(0, IN_UP);
    CHECK(G.winner == 1 && end_winner == 1 && ends >= 1, "EndGame(1) after %d ticks on the pad: winner %d (green)", t, G.winner);

    printf("\n== 6. side 0 runs out, side 1 still has jeeps: spectate ==\n");
    if (!load(&w)) return 1;
    G.views[0].mode = G.views[1].mode = BV_PLAYING;
    int32_t tp[3] = { G.teams[0].pad->x, G.teams[0].pad->y + 0x600000, 0 };
    Obj *tank = spawn_vehicle(0, tp, 0x200000, VT_TANK);
    run(2, 0, 0);
    for (int k = 0; k < 4; k++) TEAM_STOCK(&G.teams[0], k) = 0;
    veh_damage(tank, NULL, 0x7f0000);
    for (t = 0; t < 3000 && G.views[0].mode != BV_SPECTATE; t++) step(0, 0);
    CHECK(G.views[0].mode == BV_SPECTATE && team_status(0) == 1, "fly-back -> spectate after %d ticks (FUN_00427d70 = %d)", t,
          team_status(0));
    for (t = 0; t < 600 && G.views[0].fly_stage < 2; t++) step(0, 0);
    CHECK(G.views[0].fly_stage == 2 && G.views[0].fade == 0x10000 && G.winner == -2,
          "static screen after %d more ticks, game goes on (side 1 status %d)", t, team_status(1));
    run(200, 0, 0);
    CHECK(G.views[0].mode == BV_SPECTATE && G.views[1].mode == BV_PLAYING, "P1 stays spectating, P2 plays");

    printf("\n== 7. no jeeps on either side: draw ==\n");
    if (!load(&w)) return 1;
    G.views[0].mode = G.views[1].mode = BV_PLAYING;
    tank = spawn_vehicle(0, tp, 0x200000, VT_TANK);
    run(2, 0, 0);
    TEAM_STOCK(&G.teams[0], VT_JEEP) = 0;
    TEAM_STOCK(&G.teams[1], VT_JEEP) = 0;
    veh_damage(tank, NULL, 0x7f0000);
    for (t = 0; t < 3000 && G.winner == -2; t++) step(0, 0);
    CHECK(G.winner == -1 && end_winner == -1, "EndGame(-1) %d ticks after the tank died", t);

    printf("\n== 8. 2-player scores: wins per side and draws per pair of names ==\n");
    const char *hs_file = "out/twoplayer_test_hs";
    remove(hs_file);
    setenv("OPENRF_HS", hs_file, 1);
    setenv("OPENRF_P1", "Zed", 1);
    setenv("OPENRF_P2", "Amy", 1);
    GameResult gr = { 1, G.level, 2, 0, MAP };
    bool r1 = highscore_record(&gr);                 /* player 2 (Amy) wins */
    gr.winner = 0; bool r2 = highscore_record(&gr);  /* player 1 (Zed) wins */
    gr.winner = 1; bool r3 = highscore_record(&gr);
    gr.winner = -1; bool r4 = highscore_record(&gr); /* draw */
    HighScore2P hs2[4];
    int nh = highscore_list2(hs2, 4);
    CHECK(r1 && r2 && r3 && r4 && nh == 1 && !strcmp(hs2[0].name[0], "Amy") && !strcmp(hs2[0].name[1], "Zed") &&
          hs2[0].wins[0] == 2 && hs2[0].wins[1] == 1 && hs2[0].draws == 1,
          "one record: %s %u, %s %u, draws %u (names sorted, sides swapped with them)", nh ? hs2[0].name[0] : "-",
          nh ? hs2[0].wins[0] : 0, nh ? hs2[0].name[1] : "-", nh ? hs2[0].wins[1] : 0, nh ? hs2[0].draws : 0);
    setenv("OPENRF_P2", "Bob", 1);
    gr.winner = 0; highscore_record(&gr);
    nh = highscore_list2(hs2, 4);
    CHECK(nh == 2 && !strcmp(hs2[1].name[0], "Bob") && hs2[1].wins[1] == 1, "new pair inserted in name order: %s-%s, %s-%s",
          nh > 0 ? hs2[0].name[0] : "-", nh > 0 ? hs2[0].name[1] : "-", nh > 1 ? hs2[1].name[0] : "-", nh > 1 ? hs2[1].name[1] : "-");
    remove(hs_file);

    printf("\n%s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails != 0;
}
