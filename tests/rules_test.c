/* Headless rules test (src/game/rules.h): on level 1 "The Cakewalk"
   1. the enemy flag buildings are destroyed until the Flag object appears (FlagBuildingDestroyed 0x424170),
   2. a jeep drives into it (FlagOnTouch 0x424760), drops / re-takes it with C (FUN_004248a0) and brings it to
      its own pad -> JeepFlagCheck 0x42a480 -> EndGame(0),
   3. a building is shot until men run out (SpawnMenFromBuilding 0x4071f0); they walk away from a tank and
      one is run over (0x4064d0), a prison frees men of the other side,
   4. an own gate opens for a tank (SpawnGate 0x423fb0), lets it through and closes (the BNO comes back),
   5. the last vehicle dies with no jeeps in stock -> fly-back -> EndGame(-1),
   6. random mines on a level-6 map (FUN_0041c9b0), 7. the high-score file round trip.
   Returns non-zero when a check fails. Run from the project root (data in ./cd): build/rules_test */
#include "game/game.h"
#include "game/vehicle.h"
#include "game/weapon.h"
#include "game/effect.h"
#include "game/rules.h"
#include "endgame.h"
#include "world.h"
#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exe.h"
#include "vfs_host.h"

static int fails;
#define CHECK(c, ...) do { if (c) printf("  ok   " __VA_ARGS__); else { printf("  FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

static int ends, end_winner = -3;
static void on_end(int w) { ends++; end_winner = w; }
static int n_snd_crush, n_snd_ding, n_snd_gate;
static void on_sound(int cmd, uint32_t ev, Obj *o)
{
    (void)o;
    if (cmd != 1) return;
    if (ev == 0x43eb58) n_snd_crush++;
    if (ev == 0x43eab0) n_snd_ding++;
    if (ev == 0x43ea50 || ev == 0x43ea68) n_snd_gate++;
}

static uint32_t keys(uint32_t w)
{
    if (w & (IN_LEFT | IN_RIGHT)) w |= 0xff;
    if (w & (IN_UP | IN_DOWN)) w |= 0xff00;
    return w;
}

typedef struct { int id, n; } Cnt;
static void visit(Obj *o, void *ctx) { Cnt *c = ctx; if (o->cls->id == c->id && !(o->flags & OF_DELETE)) c->n++; }
static int count_class(int id) { Cnt c = { id, 0 }; obj_foreach_live(visit, &c); return c.n; }
/* Men are removed when not drawn for 120 ticks (ManUpdate): the headless test "draws" them every frame. */
static void see_men(Obj *o, void *ctx) { (void)ctx; if (o->cls->id == 14) MAN_DRAWN(o) = g_tick; }
static void step(uint32_t in) { game_frame(1, keys(in)); obj_foreach_live(see_men, NULL); }
static void run(int n, uint32_t in) { for (int i = 0; i < n; i++) step(in); }
static uint32_t *cell_xy(int x, int y) { return &G.cell[y * 128 + x]; }
static int cx_of(const uint32_t *c) { return cell_index(c) & 127; }
static int cy_of(const uint32_t *c) { return cell_index(c) >> 7; }
static double px(int32_t v) { return v / 65536.0; }
#define PAD (G.teams[0].pad)

static void clear_range(int x0, int y0, int x1, int y1, int terrain)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            uint32_t *c = cell_xy(x, y);
            *c &= 0xffff0000u;
            CELL_SET_TERRAIN(*c, terrain);
        }
}

static Obj *spawn_at(int type, int team, int32_t x, int32_t y, uint32_t heading)
{
    Obj *v = get_team_vehicle(team);
    if (v) obj_destroy_now(v);
    int32_t p[3] = { x, y, 0 };
    v = spawn_vehicle(team, p, heading, type);
    run(1, 0);
    return v;
}

static bool load(World *w, const char *map)
{
    if (!world_load(w, map)) { fprintf(stderr, "world_load %s failed (run from the project root, data in ./cd)\n", map); return false; }
    if (!game_load(w, map)) { fprintf(stderr, "game_load %s failed\n", map); return false; }
    G.views[0].mode = BV_PLAYING;              /* no bunker screen: vehicles are spawned directly */
    return true;
}

/* Destroy a BNO cell at once (as a large explosion would): BnoDamage until it changes, then let the
   explosion script put the destroyed variant in. */
static void destroy_cell(uint32_t *c)
{
    for (int k = 0; k < 8 && (*c & 0xe000000u); k++) bno_damage(0x640000, NULL, c, NULL);
    run(80, 0);
}

int main(void)
{
    vfs_mount_default();                                                  /* ./cd or $OPENRF_DATA */
    if (!exe_load()) { fprintf(stderr, "%s\n", exe_error()); return 1; }   /* tables from RFIRE.BIN */
    static World w;
    const char *MAP = "WORLDS/1PLAYER/LEVEL1/RFMAP001.RFM";
    game_hooks.sound = on_sound;
    if (!load(&w, MAP)) return 1;
    game_hooks.end_game = on_end;
    printf("level '%s': team 0 pad (%d,%d), flag sites team0 %d team1 %d, flag hidden at (%d,%d), flag_left %d\n",
           w.name, G.teams[0].pad->x >> 21, G.teams[0].pad->y >> 21, G.nflags[0], G.nflags[1],
           cx_of(G.flag_cell[1]), cy_of(G.flag_cell[1]), G.flag_left[1]);

    printf("\n== 1. destroy enemy flag buildings until the flag shows ==\n");
    int destroyed = 0;
    for (int guard = 0; guard < 64 && !G.flag_obj[1]; guard++) {
        uint32_t *c = G.flag_cell[1];
        if (CELL_BNO(*c) != 22) break;
        printf("    flag in (%d,%d): destroying it (flag_left %d)\n", cx_of(c), cy_of(c), G.flag_left[1]);
        destroy_cell(c);
        destroyed++;
    }
    Obj *flag = G.flag_obj[1];
    CHECK(flag != NULL, "Flag object after %d flag buildings: at (%.1f, %.1f), music bits 0x%x", destroyed,
          flag ? px(flag->pos[0]) : 0, flag ? px(flag->pos[1]) : 0, rules_music_bits);
    if (!flag) return 1;
    CHECK(flag->cls == &class_flag && FLAG_BLIP(flag) >= 0, "class %s, radar blip %d", flag->cls->name, FLAG_BLIP(flag));
    run(40, 0);
    uint32_t *fc = flag->cell;
    printf("    the building is now %s (hp %u): shoot it down to the rubble the jeep can drive into\n",
           bno_defs[CELL_BNO(*fc)].name, CELL_HP(*fc));
    for (int k = 0; k < 4 && CELL_BNO(*fc) != 63; k++) destroy_cell(fc);
    CHECK(CELL_BNO(*fc) == 63, "cell (%d,%d): %s (touch handler DestFlagOnTouch)", cx_of(fc), cy_of(fc), bno_defs[CELL_BNO(*fc)].name);
    CHECK(G.flag_obj[1] == flag && flag->parent == NULL, "lying at (%.1f, %.1f, %.1f), wave frame %.2f",
          px(flag->pos[0]), px(flag->pos[1]), px(flag->pos[2]), px(FLAG_WAVE(flag)));

    printf("\n== 2. jeep picks the flag up and brings it home ==\n");
    /* start 3 cells south of the flag, facing north, on cleared ground */
    int fx = flag->pos[0] >> 21, fy = flag->pos[1] >> 21;
    int grass = CELL_TERRAIN(*cell_xy(G.teams[0].pad->x >> 21, (G.teams[0].pad->y >> 21) + 3));
    clear_range(fx - 1, fy + 1, fx + 1, fy + 4, grass);
    Obj *jeep = spawn_at(VT_JEEP, 0, flag->pos[0], (fy + 4) * 0x200000 + 0x100000, 0);
    int t;
    for (t = 0; t < 600 && flag->parent != jeep; t++) step(t < 110 ? 0 : IN_UP);
    CHECK(flag->parent == jeep, "picked up after %d ticks (jeep at %.1f, %.1f), Ding %d", t, px(jeep->pos[0]), px(jeep->pos[1]), n_snd_ding);
    run(5, 0);
    CHECK(flag->gfx == &rgfx_44e610 && (rules_music_bits & 0x200), "carried model 0x44e610, Flag Pickup bit set");
    run(20, 0);
    step(IN_BTN3);                                        /* C: drop */
    run(2, 0);
    CHECK(flag->parent == NULL && flag_ext[flag->id & 0x1ff].dropped == jeep, "C drops it (no pickup while touching)");
    run(10, 0);
    CHECK(flag->parent == NULL, "still lying next to the jeep");
    /* it was dropped behind the jeep, next to the rubble: back into it (C only re-takes a flag whose own
       collision test reaches the jeep before any static part) */
    printf("    flag at (%.1f, %.1f) cell (%d,%d), jeep at (%.1f, %.1f)\n", px(flag->pos[0]), px(flag->pos[1]),
           cx_of(flag->cell), cy_of(flag->cell), px(jeep->pos[0]), px(jeep->pos[1]));
    for (t = 0; t < 100 && flag->parent != jeep; t++) step(t % 20 == 0 ? IN_BTN3 : IN_DOWN);
    CHECK(flag->parent == jeep, "backing into it takes it again (%d ticks)", t);
    /* drive home: put the jeep next to its pad, then onto the pad terrain */
    obj_move_to(jeep, PAD->x, PAD->y + 0x400000, 0);
    run(3, 0);
    CHECK(G.winner == -2 && ends == 0, "no win off the pad (terrain 0x%02x)", CELL_TERRAIN(*jeep->cell));
    for (t = 0; t < 200 && G.winner == -2; t++) step(IN_UP);
    CHECK(G.winner == 0 && end_winner == 0 && ends >= 1, "EndGame(0) after %d ticks on the pad: winner %d, terrain 0x%02x",
          t, G.winner, CELL_TERRAIN(*jeep->cell));

    printf("\n== 3. men leave a damaged building and run from a tank ==\n");
    if (!load(&w, MAP)) return 1;
    game_hooks.end_game = on_end;
    int bx = (PAD->x >> 21) - 6, by = (PAD->y >> 21) + 6;
    clear_range(bx - 4, by - 6, bx + 4, by + 2, grass);
    set_cell_static(24, cell_xy(bx, by), 0, 1);           /* BNO_BLDG_S, enemy: men 1..3 */
    Obj *tank = spawn_at(VT_TANK, 0, (bx + 3) * 0x200000 + 0x100000, (by - 3) * 0x200000 + 0x100000, 0x300000);
    for (int k = 0; k < 6 && CELL_HP(*cell_xy(bx, by)) > 1; k++) bno_damage(0x10000, NULL, cell_xy(bx, by), NULL);
    int men = count_class(14);
    CHECK(men >= 2 && men <= 3, "hp %u: %d men of BNO_BLDG_S (flags 0x%x: 2..3)", CELL_HP(*cell_xy(bx, by)), men,
          bno_defs[24].flags);
    Obj *m0 = NULL;
    for (int i = 1; i < OBJ_POOL; i++) if (obj_pool[i].cls == &class_man && (obj_pool[i].flags & OF_ALIVE)) { m0 = &obj_pool[i]; break; }
    if (!m0) return 1;
    int32_t p0[3] = { m0->pos[0], m0->pos[1], 0 };
    int32_t d0 = dist2d(m0->pos, tank->pos);
    printf("    man #%03x team %d at (%.1f, %.1f), %d grenades, %.1f px from the tank\n", m0->id & 0x1ff, m0->team,
           px(m0->pos[0]), px(m0->pos[1]), MAN_GRENADES(m0), px(d0));
    run(120, 0);
    int alive = (m0->flags & OF_ALIVE) && m0->cls == &class_man;
    CHECK(alive && dist2d(m0->pos, p0) > 0x40000, "walked %.1f px, now %.1f px from the tank, anim %d frame %.1f, parent %s",
          alive ? px(dist2d(m0->pos, p0)) : 0, alive ? px(dist2d(m0->pos, tank->pos)) : 0, alive ? MAN_ANIM(m0) : -1,
          alive ? px(MAN_FRAME(m0)) : 0, alive && m0->parent ? m0->parent->cls->name : "-");
    if (alive) {
        int32_t d1 = dist2d(m0->pos, tank->pos);
        run(60, 0);
        alive = (m0->flags & OF_ALIVE) && m0->cls == &class_man;
        int32_t d2 = alive ? dist2d(m0->pos, tank->pos) : 0;
        CHECK(alive && d2 > d1, "runs away from the tank: %.1f -> %.1f px in 60 ticks (heading %.0f deg)", px(d1), px(d2),
              alive ? m0->heading * (360.0 / 0x400000) : 0);
    }
    int crushed0 = n_snd_crush, men0 = count_class(14), stays0 = count_class(17);
    if (alive) {                                           /* drive over it */
        obj_move_to(tank, m0->pos[0], m0->pos[1] + 0x180000, 0);
        tank->heading = 0;
        for (t = 0; t < 120 && (m0->flags & OF_ALIVE) && m0->cls == &class_man && n_snd_crush == crushed0; t++) step(IN_UP);
    }
    run(2, 0);
    CHECK(n_snd_crush > crushed0 && count_class(14) < men0 && count_class(17) > stays0,
          "run over: ManCrush, men %d -> %d, remains (Stay) %d -> %d", men0, count_class(14), stays0, count_class(17));
    /* unseen men go away after 120 ticks */
    int before = count_class(14);
    for (int i = 0; i < 130; i++) game_frame(1, 0);
    CHECK(before == 0 || count_class(14) == 0, "not drawn for 120 ticks: men %d -> %d", before, count_class(14));
    /* prison: freed men belong to the other side */
    set_cell_static(15, cell_xy(bx, by), 0, 1);            /* BNO_PRISON_S, enemy prison */
    for (int k = 0; k < 6 && CELL_HP(*cell_xy(bx, by)) > 1; k++) bno_damage(0x10000, NULL, cell_xy(bx, by), NULL);
    int own = 0, all = 0;
    for (int i = 1; i < OBJ_POOL; i++)
        if (obj_pool[i].cls == &class_man && (obj_pool[i].flags & OF_ALIVE)) { all++; own += obj_pool[i].team == 0; }
    CHECK(all >= 4 && own == all, "prison: %d prisoners freed, %d of them team 0", all, own);

    printf("\n== 4. an own gate opens for a tank and closes behind it ==\n");
    if (!load(&w, MAP)) return 1;
    int gx = (PAD->x >> 21) - 6, gy = (PAD->y >> 21) + 5;
    clear_range(gx - 2, gy - 4, gx + 2, gy + 4, grass);
    set_cell_static(43, cell_xy(gx, gy), 0, 0);            /* BNO_GATE_H, own side */
    for (int k = -2; k <= 2; k++) if (k) set_cell_static(45, cell_xy(gx + k, gy), 0, 0);   /* wall either side */
    tank = spawn_at(VT_TANK, 0, gx * 0x200000 + 0x100000, (gy + 3) * 0x200000 + 0x100000, 0);
    run(30, 0);
    Obj *gate = NULL;
    int32_t max_open = 0;
    for (t = 0; t < 900; t++) {
        step(t < 400 ? IN_UP : 0);
        for (int i = 1; i < OBJ_POOL && !gate; i++) if (obj_pool[i].cls == &class_gate && (obj_pool[i].flags & OF_ALIVE)) gate = &obj_pool[i];
        if (gate && (gate->flags & OF_ALIVE) && gate->cls == &class_gate && GATE_OPEN(gate) > max_open) max_open = GATE_OPEN(gate);
        if (gate && t > 400 && CELL_BNO(*cell_xy(gx, gy)) == 43) break;
    }
    CHECK(gate != NULL, "Gate object spawned (BNO cleared while it exists)");
    CHECK(max_open == 0xf0000, "opened to %.1f px", px(max_open));
    CHECK(tank->pos[1] < gy * 0x200000, "tank drove through: y %.1f (gate row %d)", px(tank->pos[1]), gy);
    CHECK(CELL_BNO(*cell_xy(gx, gy)) == 43 && count_class(13) == 0, "closed again after %d ticks, BNO_GATE_H back, GateMove/Close sounds %d",
          t, n_snd_gate);
    /* an enemy gate stays shut */
    set_cell_static(43, cell_xy(gx, gy), 0, 1);
    tank = spawn_at(VT_TANK, 0, gx * 0x200000 + 0x100000, (gy + 3) * 0x200000 + 0x100000, 0);
    run(30, 0);
    run(300, IN_UP);
    CHECK(count_class(13) == 0 && tank->pos[1] > gy * 0x200000, "enemy gate stays closed (tank stopped at y %.1f)", px(tank->pos[1]));

    printf("\n== 5. the last vehicle dies with no jeeps left -> EndGame(-1) ==\n");
    if (!load(&w, MAP)) return 1;
    ends = 0; end_winner = -3;
    game_hooks.end_game = on_end;
    tank = spawn_at(VT_TANK, 0, PAD->x, PAD->y + 0x600000, 0x200000);
    TEAM_STOCK(&G.teams[0], VT_JEEP) = 0;
    run(10, 0);
    veh_damage(tank, NULL, 0x7f0000);
    for (t = 0; t < 3000 && G.winner == -2; t++) step(0);
    CHECK(G.winner == -1 && end_winner == -1, "EndGame(-1) %d ticks after the tank died", t);
    /* with jeeps in stock it is back to the bunker instead */
    if (!load(&w, MAP)) return 1;
    tank = spawn_at(VT_TANK, 0, PAD->x, PAD->y + 0x600000, 0x200000);
    run(10, 0);
    veh_damage(tank, NULL, 0x7f0000);
    for (t = 0; t < 3000 && G.views[0].mode != BV_SELECT; t++) step(0);
    CHECK(G.winner == -2 && G.views[0].mode == BV_SELECT, "jeeps left (%d): back in the vehicle select after %d ticks",
          TEAM_STOCK(&G.teams[0], VT_JEEP), t);

    printf("\n== 6. random mines (FUN_0041c9b0) ==\n");
    const char *M6 = "WORLDS/1PLAYER/LEVEL6/RFMAP046.RFM";
    if (!load(&w, M6)) return 1;
    int nm = count_class(10);
    CHECK(G.random_mines > 0 && nm > 0 && nm <= G.random_mines, "'%s' level %d: %d of %d mines placed (VHCL mines %d)", w.name,
          G.level + 1, nm, G.random_mines, w.vehicles[5]);
    if (!load(&w, MAP)) return 1;
    CHECK(count_class(10) == 0, "level 1: no random mines");

    printf("\n== 7. high scores ==\n");
    setenv("OPENRF_HS", "/tmp/openrf_rules_test_hs", 1);
    remove(highscore_path());
    setenv("USER", "tester", 1);
    GameResult r = { 0, 0, 1, 83000, MAP };
    bool a1 = highscore_record(&r);
    r.time_ms = 90000;
    bool a2 = highscore_record(&r);                        /* slower: kept */
    r.time_ms = 61000;
    bool a3 = highscore_record(&r);                        /* faster: replaced */
    GameResult r2 = { 0, 5, 1, 120000, M6 };
    bool a4 = highscore_record(&r2);
    GameResult lost = { -1, 0, 1, 1000, MAP };
    bool a5 = highscore_record(&lost);
    HighScore1P hs[8];
    int n = highscore_list(hs, 8);
    CHECK(a1 && !a2 && a3 && a4 && !a5 && n == 2 && hs[0].level == 1 && hs[0].time_ms == 61000 && !strcmp(hs[0].map, "RFMAP001") &&
          hs[1].level == 6 && !strcmp(hs[1].player, "tester"), "%d records: L%d %s %s %u ms / L%d %s %u ms", n, hs[0].level,
          hs[0].map, hs[0].player, hs[0].time_ms, n > 1 ? hs[1].level : 0, n > 1 ? hs[1].map : "", n > 1 ? hs[1].time_ms : 0);
    FILE *f = fopen(highscore_path(), "rb");
    uint8_t raw[8] = { 0 };
    if (f) { if (fread(raw, 1, 8, f) != 8) memset(raw, 0, 8); fclose(f); }
    CHECK(raw[0] == (uint8_t)((0x1c ^ 0x5a) + 'r') && raw[4] == (uint8_t)(('r' ^ 0x5a) + 'f'), "file obfuscated ((b ^ 0x5a) + \"retufire\"[i & 7])");
    remove(highscore_path());

    printf("\n%s: %d check(s) failed\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
