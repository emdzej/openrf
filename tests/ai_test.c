/* Headless test of the computer enemies (src/game/turret.c, drone.c, sub.c). The world view is really
   rendered every tick (src/render/view.c) because turrets are woken by the renderer (Model_TowerHook
   0x4174e0 -> ActivateTurret 0x423c10) and the idle drone is gated by the enemy turrets in view.
     1. a tank drives up to an enemy tower of a real map: the turret activates, turns, pitches, probes
        with TRACERs, fires type-4 shells and damages the tank; the tank destroys it (turret debris in
        the live pose); a second turret returns to the static tower when its target is gone / unseen
     2. a tank idles in one cell: the Drone comes, attacks, is shot down
     3. a heli leaves the map: SpawnSub, the sub surfaces under it and fires a homing Death Missle
   Run from the project root (data in ./cd):  build/ai_test */
#include "game/game.h"
#include "game/vehicle.h"
#include "game/weapon.h"
#include "game/effect.h"
#include "game/ai.h"
#include "render/render.h"
#include "sprites.h"
#include "assets.h"
#include "world.h"
#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exe.h"
#include "vfs_host.h"

static const char *MAPS[] = {
    "WORLDS/1PLAYER/LEVEL1/RFMAP001.RFM", "WORLDS/1PLAYER/LEVEL2/RFMAP002.RFM", "WORLDS/1PLAYER/LEVEL3/RFMAP014.RFM",
    "WORLDS/1PLAYER/LEVEL4/RFMAP024.RFM", "WORLDS/1PLAYER/LEVEL5/RFMAP034.RFM", "WORLDS/1PLAYER/LEVEL6/RFMAP051.RFM",
};

static World world;
static View *view;
static Framebuffer fb;
static int music_last = -2, n_music;

/* ---- minimal render bridge (play.c collect() for the classes that matter here) ---- */
static RenderObj robj[RENDER_MAX_OBJS];
static int nrobj;
static void collect(Obj *o, void *ctx)
{
    (void)ctx;
    if (!o->gfx || nrobj >= RENDER_MAX_OBJS) return;
    int cls = o->cls->id;
    if (cls == 3 || cls == 11 || cls == 17) return;               /* effects: not needed here */
    const ModelPart *m = model_part_by_addr(o->gfx->addr);
    if (!m) return;
    RenderObj *r = &robj[nrobj++];
    *r = (RenderObj){ .model = m, .pos = { o->pos[0], o->pos[1], o->pos[2] }, .yaw = (int32_t)o->heading,
                      .zbias = o->_50, .team = o->team, .cls = cls, .id = o->id, .tick = (uint32_t)g_tick };
    if (cls == 2) { r->pitch = (int32_t)TURRET_PITCH(o); r->unseen = &TURRET_UNSEEN(o); }
    else if (cls == 9) {
        DroneRec *d = drone_rec(o);
        r->pitch = DRONE_PITCH(o); r->roll = DRONE_ROLL(o);
        r->gun_pitch = d ? (int32_t)d->gun_pitch : 0; r->stamp = d ? &d->drawn : NULL;
    } else if (cls == 15) { r->u5c = SUB_FRAME(o); r->stamp = &SUB_DRAWN(o); }
    else if (cls == 0 || cls == 7 || cls == 16) r->pitch = o->i60;
    render_add_object(view, r);
}
static int on_tower(void *user, View *v, int ci)
{
    (void)user;
    Obj *t = turret_tower_hook(&G.cell[ci], v->team);
    if (!t) return 0;
    collect(t, NULL);
    return 1;
}
static Obj *cam_obj;
static int32_t cam_fixed[3];
static void render_frame(void)
{
    const int32_t *p = cam_obj ? cam_obj->pos : cam_fixed;
    view_look_at(view, p[0], p[1], p[2], 10 << 16);
    render_clear_objects(view);
    nrobj = 0;
    obj_foreach_live(collect, NULL);
    RenderMap rm = { G.cell, (const int8_t (*)[4])G.jitter, on_tower, NULL };
    view->team = 0;
    memset(fb.pixels, 0, (size_t)fb.w * fb.h);
    render_world(view, &rm, &fb, 1);
    G.drones[0] = view->enemy_turrets;
}

static int on_music(int track, int prio, Obj *o)
{
    (void)o;
    if (track != music_last) { printf("      [music request %d prio 0x%x at t=%d]\n", track, prio, g_tick); n_music++; }
    music_last = track;
    return 0;
}

static uint32_t keys(uint32_t w)
{
    if (w & (IN_LEFT | IN_RIGHT)) w |= 0xff;
    if (w & (IN_UP | IN_DOWN)) w |= 0xff00;
    return w;
}
static bool render_on = true;
static void step(uint32_t in)
{
    game_frame(1, keys(in));
    ai_frame_end();
    effects_reap_drawn();
    if (render_on) render_frame();
}
static void run(int n, uint32_t in) { for (int i = 0; i < n; i++) step(in); }

typedef struct { const ObjClass *cls; int id, n; Obj *last; Obj *parent; } Cnt;
static void visit(Obj *o, void *ctx)
{
    Cnt *c = ctx;
    if (o->flags & OF_DELETE) return;
    if ((c->cls && o->cls != c->cls) || (!c->cls && o->cls->id != c->id)) return;
    if (c->parent && o->parent != c->parent) return;
    c->n++; c->last = o;
}
static Obj *turret_at(const uint32_t *cell)
{
    for (int i = 1; i < OBJ_POOL; i++)
        if (obj_pool[i].next && obj_pool[i].cls && obj_pool[i].cls->id == 2 && (obj_pool[i].flags & OF_ALIVE) &&
            !(obj_pool[i].flags & OF_DELETE) && obj_pool[i].cell == cell) return &obj_pool[i];
    return NULL;
}
static Obj *find(int id, int *n) { Cnt c = { NULL, id, 0, NULL, NULL }; obj_foreach_live(visit, &c); if (n) *n = c.n; return c.last; }
static int count_shells_of(Obj *owner) { Cnt c = { &class_missle, 0, 0, NULL, owner }; obj_foreach_live(visit, &c); return c.n; }
static double px(int32_t v) { return v / 65536.0; }
static double deg(uint32_t a) { return (a & ANG_MASK) * (360.0 / 0x400000); }

static Obj *spawn_at(int type, int32_t x, int32_t y, uint32_t heading)
{
    Obj *v = get_team_vehicle(0);
    if (v) obj_destroy_now(v);
    int32_t p[3] = { x, y, 0 };
    v = spawn_vehicle(0, p, heading, type);
    cam_obj = v;
    return v;
}
static void clear_cell(int x, int y, int terrain)
{
    uint32_t *c = &G.cell[y * 128 + x];
    *c = (*c & 0xffff0000u) | (uint32_t)terrain;
}

static bool load(const char *map)
{
    if (!world_load(&world, map) || !game_load(&world, map)) return false;
    G.views[0].mode = BV_PLAYING;                 /* no bunker: vehicles are spawned directly */
    ai_level_start();
    game_hooks.music = on_music;
    return true;
}

/* An enemy (team 1) tower with hit points and 5 dry cells south of it. */
static bool find_tower(int *tx, int *ty)
{
    for (int y = 4; y < 120; y++)
        for (int x = 4; x < 124; x++) {
            uint32_t w = G.cell[y * 128 + x];
            int b = CELL_BNO(w);
            if ((b != 49 && b != 50) || CELL_TEAM(w) != 1 || !CELL_HP(w)) continue;
            bool ok = true;
            for (int k = 1; k <= 6 && ok; k++) { int t = CELL_TERRAIN(G.cell[(y + k) * 128 + x]); if (t == 2 || t == 1) ok = false; }
            if (!ok) continue;
            *tx = x; *ty = y;
            return true;
        }
    return false;
}

static int test_turret(void)
{
    int tx = 0, ty = 0, li = -1;
    for (int i = 0; i < (int)(sizeof MAPS / sizeof *MAPS); i++) {
        if (!load(MAPS[i])) continue;
        if (find_tower(&tx, &ty)) { li = i; break; }
    }
    if (li < 0) { printf("no map with an enemy tower found\n"); return 1; }
    uint32_t *tc = &G.cell[ty * 128 + tx];
    printf("\n== 1. TURRET: map %s '%s', enemy tower %s at (%d,%d) hp %u team %u ==\n", MAPS[li], world.name,
           bno_defs[CELL_BNO(*tc)].name, tx, ty, CELL_HP(*tc), CELL_TEAM(*tc));
    int grass = CELL_TERRAIN(G.cell[(ty + 3) * 128 + tx]);
    for (int k = 1; k <= 13; k++)                                     /* a clear run-up south of it */
        for (int dx = -1; dx <= 1; dx++) clear_cell(tx + dx, ty + k, grass);
    int32_t cx = tx * 0x200000 + 0x100000, cy = ty * 0x200000 + 0x100000;
    Obj *tank = spawn_at(VT_TANK, cx, cy + 12 * 0x200000, 0);          /* 12 cells south, facing north */
    VehState *s = veh_state(tank);
    run(20, 0);
    int t0 = g_tick, act = -1, first_shot = -1, hp0 = s->hp;
    Obj *tur = NULL;
    for (int i = 0; i < 1200 && !tur; i++) {
        step(IN_UP);
        Obj *t = turret_at(tc);
        if (t) { tur = t; act = g_tick; }
    }
    if (!tur) { printf("    turret never activated (tank at %.0f,%.0f)\n", px(tank->pos[0]), px(tank->pos[1])); return 1; }
    printf("    activated at t+%d with the tank at %.0f px (%s, class gfx 0x%x); cell bno now %u (%s)\n", act - t0,
           px(dist2d(tank->pos, tur->pos)), tur->cls->name, tur->gfx->addr, CELL_BNO(*tc), bno_defs[CELL_BNO(*tc)].name);
    printf("    rest pose yaw %.1f pitch 0x%x, wake-up at tick %d (now %d), live turrets team1 %d\n", deg(tur->heading),
           (unsigned)TURRET_PITCH(tur), tur->u64, g_tick, turret_count[1]);
    for (int i = 0; i < 400; i++) {                                    /* keep closing in to ~130 px */
        step(dist2d(tank->pos, tur->pos) > 0x820000 ? IN_UP : 0);
        if (turret_at(tc) != tur) break;
        int n = count_shells_of(tur);
        if (n && first_shot < 0) first_shot = g_tick;
        if (i % 40 == 0)
            printf("      t+%4d yaw %6.1f pitch 0x%06x dist %5.1f px shells in flight %d tank hp %.2f\n", g_tick - t0,
                   deg(tur->heading), (unsigned)TURRET_PITCH(tur), px(dist2d(tank->pos, tur->pos)), n, px(s->hp));
    }
    printf("    first shell at t+%d; tank hp %.2f -> %.2f (type-4 shell 1.5 - armour 0.3 per hit)\n",
           first_shot - t0, px(hp0), px(s->hp));
    int ok1 = first_shot >= 0 && s->hp < hp0;
    /* the tank shoots back: turret aligned north, cannon A */
    uint32_t tyaw = tur->heading, tpitch = TURRET_PITCH(tur);
    int shots = 0, deb0 = 0;
    find(3, &deb0);
    for (int k = 0; k < 20 && CELL_BNO(*tc) == 90; k++) {
        tyaw = tur->heading; tpitch = TURRET_PITCH(tur);
        step(IN_BTN1); run(2, 0); shots++;
        run(40, 0);
        printf("      shot %d: tower hp %u bno %u\n", shots, CELL_HP(*tc), CELL_BNO(*tc));
    }
    run(3, 0);
    int deb = 0;
    find(3, &deb);
    run(150, 0);                                                       /* explosion script: destroyed variant */
    printf("    tower after %d shells: bno %u (%s), turret debris %d (live pose yaw %.1f pitch 0x%x), turrets team1 %d\n",
           shots, CELL_BNO(*tc), bno_defs[CELL_BNO(*tc)].name, deb - deb0, deg(tyaw), (unsigned)tpitch, turret_count[1]);
    int ok2 = CELL_BNO(*tc) != 90 && CELL_BNO(*tc) != 49 && CELL_BNO(*tc) != 50 && turret_count[1] == 0;

    /* 1b. deactivation: a fresh enemy tower; the target is lost -> rest pose -> static tower */
    int ux = tx + 3, uy = ty + 6;
    set_cell_static(49, &G.cell[uy * 128 + ux], 0, 1);
    tank = spawn_at(VT_TANK, ux * 0x200000 + 0x100000, (uy + 4) * 0x200000 + 0x100000, 0);
    run(30, 0);
    Obj *t2 = NULL;
    uint32_t *c2 = &G.cell[uy * 128 + ux];
    for (int i = 0; i < 200 && !t2; i++) { step(0); t2 = turret_at(c2); }
    run(300, 0);
    printf("    tower 2 at (%d,%d): turret %s, yaw %.1f; the tank goes away\n", ux, uy, t2 ? "active" : "NOT active",
           t2 ? deg(t2->heading) : 0.0);
    cam_obj = NULL;
    cam_fixed[0] = ux * 0x200000 + 0x100000; cam_fixed[1] = (uy + 2) * 0x200000; cam_fixed[2] = 0;
    obj_destroy_now(tank);
    int back = -1;
    for (int i = 0; i < 600 && back < 0; i++) { step(0); if (!turret_at(c2)) back = i; }
    uint32_t w2 = G.cell[uy * 128 + ux];
    printf("    target gone -> turret rotated back and was removed after %d ticks; cell bno %u (%s) hp %u\n", back,
           CELL_BNO(w2), bno_defs[CELL_BNO(w2)].name, CELL_HP(w2));
    /* 1c. unseen for 300 ticks -> static */
    tank = spawn_at(VT_TANK, ux * 0x200000 + 0x100000, (uy + 4) * 0x200000 + 0x100000, 0);
    run(30, 0);
    t2 = turret_at(c2);
    render_on = false;
    int gone = -1;
    for (int i = 0; i < 400 && gone < 0; i++) { step(0); if (!turret_at(c2)) gone = i; }
    render_on = true;
    printf("    re-activated: %s; not drawn any more -> static again after %d ticks (limit 300), bno %u\n",
           t2 ? "yes" : "no", gone, CELL_BNO(G.cell[uy * 128 + ux]));
    int ok3 = t2 && back >= 0 && gone >= 290 && gone <= 302 && CELL_BNO(G.cell[uy * 128 + ux]) == 49;
    printf("    RESULT turret: fires+damages %s, destroyed %s, deactivation %s\n", ok1 ? "OK" : "FAIL", ok2 ? "OK" : "FAIL",
           ok3 ? "OK" : "FAIL");
    return !(ok1 && ok2 && ok3);
}

static int test_drone(void)
{
    if (!load(MAPS[0])) return 1;
    drone_pool_init(3);                           /* level 1 has no drones (table 0x443880) */
    printf("\n== 2. DRONE: map %s, tank parked next to the pad ==\n", MAPS[0]);
    int32_t x = G.teams[0].pad->x + 3 * 0x200000, y = G.teams[0].pad->y;
    Obj *tank = spawn_at(VT_TANK, x, y, 0x100000);
    VehState *s = veh_state(tank);
    run(30, 0);
    int t0 = g_tick, hp0 = s->hp;
    Obj *d = NULL;
    for (int i = 0; i < 600 && !d; i++) { step(0); d = find(9, NULL); }
    if (!d) { printf("    no drone\n"); return 1; }
    int shadows = 0;
    { Cnt c = { &class_shadow, 0, 0, NULL, d }; obj_foreach_live(visit, &c); shadows = c.n; }
    printf("    drone (team %d) spawned t+%d at %.0f px from the tank, z %.0f px, shadow objects %d, pool team1 %d\n",
           d->team, g_tick - t0, px(dist2d(d->pos, tank->pos)), px(d->pos[2]), shadows, drone_count[1]);
    int first = -1;
    for (int i = 0; i < 900 && (d->flags & OF_ALIVE) && d->cls == &class_drone; i++) {
        step(0);
        if (first < 0 && count_shells_of(d)) first = g_tick;
        if (i % 60 == 0)
            printf("      t+%4d dist %5.1f px hdg %5.1f speed %.2f roll 0x%06x pitch 0x%06x gun 0x%06x tank hp %.2f\n",
                   g_tick - t0, px(dist2d(d->pos, tank->pos)), deg(d->heading), px(d->speed), (unsigned)DRONE_ROLL(d) & ANG_MASK,
                   (unsigned)DRONE_PITCH(d) & ANG_MASK, drone_rec(d) ? (unsigned)drone_rec(d)->gun_pitch : 0, px(s->hp));
        if (s->hp < hp0 - 0x40000) break;
    }
    printf("    first drone shot t+%d; tank hp %.2f -> %.2f\n", first - t0, px(hp0), px(s->hp));
    int ok1 = first >= 0 && s->hp < hp0;
    /* shoot it down: projectile hit >= 1.0 -> exploded */
    int deb0 = 0;
    find(3, &deb0);
    uint32_t did = d->id;
    if (d->cls->damage) d->cls->damage(d, tank, 0x20000);
    run(3, 0);
    int deb = 0;
    find(3, &deb);
    bool gone1 = obj_pool[did & 0x1ff].id != did || !(obj_pool[did & 0x1ff].flags & OF_ALIVE);
    printf("    hit for 2.0: drone gone %s, debris %d, drones team1 now %d (idle rule keeps sending)\n", gone1 ? "yes" : "no",
           deb - deb0, drone_count[1]);
    int ok2 = gone1 && deb > deb0;
    /* a new one flies away and vanishes when the target docks/dies */
    run(400, 0);
    d = find(9, NULL);
    if (d) {
        obj_destroy_now(tank);
        int gone = -1;
        for (int i = 0; i < 1500 && gone < 0; i++) { step(0); if (!find(9, NULL)) gone = i; }
        printf("    second drone: target gone -> leaves, removed %d ticks later (60 ticks after its last draw)\n", gone);
    } else printf("    (no second drone within 400 ticks)\n");
    printf("    RESULT drone: attack %s, shot down %s\n", ok1 ? "OK" : "FAIL", ok2 ? "OK" : "FAIL");
    return !(ok1 && ok2);
}

static int test_sub(void)
{
    if (!load(MAPS[0])) return 1;
    printf("\n== 3. SUB: heli flies west off the map ==\n");
    int32_t y = G.teams[0].pad->y;
    Obj *h = spawn_at(VT_HELI, 3 * 0x200000, y, 0x300000);             /* heading west */
    VehState *s = veh_state(h);
    int t0 = g_tick;
    for (int i = 0; i < 400 && h->pos[2] < 0x320000; i++) step(0);
    printf("    heli airborne at t+%d (z %.0f px)\n", g_tick - t0, px(h->pos[2]));
    for (int i = 0; i < 400 && ((h->heading - 0x300000 + 0x20000) & ANG_MASK) > 0x40000; i++) step(IN_LEFT);
    run(60, 0);
    printf("    turned west: heading %.1f at x %.0f px\n", deg(h->heading), px(h->pos[0]));
    int spawned = -1, surfaced = -1, fired = -1, hit = -1;
    Obj *m = NULL;
    for (int i = 0; i < 1500; i++) {
        step(h->pos[0] > -0x300000 ? IN_UP : 0);
        if (spawned < 0 && sub_obj) { spawned = g_tick; printf("    SpawnSub at t+%d, heli x %.0f px\n", g_tick - t0, px(h->pos[0])); }
        if (sub_obj && surfaced < 0 && SUB_FRAME(sub_obj) >= 0x140000) {
            surfaced = g_tick;
            printf("    sub surfaced at t+%d at (%.0f, %.0f), heli at (%.0f, %.0f)\n", g_tick - t0, px(sub_obj->pos[0]),
                   px(sub_obj->pos[1]), px(h->pos[0]), px(h->pos[1]));
        }
        if (fired < 0) {
            Cnt c = { &class_death_missle, 0, 0, NULL, NULL };
            obj_foreach_live(visit, &c);
            if (c.n) { fired = g_tick; m = c.last; printf("    Death Missle fired at t+%d, homing on %s\n", g_tick - t0,
                                                           m->parent == h ? "the heli" : "?"); }
        }
        if (get_team_vehicle(0) != h || s->hp <= 0) { hit = g_tick; break; }
        if (i % 100 == 0) printf("      t+%4d heli (%.0f, %.0f, %.0f) speed %.2f hdg %.0f outside %d\n", g_tick - t0, px(h->pos[0]), px(h->pos[1]), px(h->pos[2]), px(h->speed), deg(h->heading), h->cell == &G.outside);
        if (sub_obj && i % 100 == 0) printf("      t+%4d sub frame %5.2f state %d\n", g_tick - t0, px(SUB_FRAME(sub_obj)), SUB_STATE(sub_obj));
    }
    printf("    heli %s at t+%d (hp %.2f)\n", hit >= 0 ? "destroyed" : "survived", hit - t0, px(s->hp));
    for (int i = 0; i < 600 && sub_obj; i++) step(0);
    printf("    sub %s afterwards; music requests seen %d (MUS_SUB = 16)\n", sub_obj ? "still there" : "dived and was removed", n_music);
    int ok = spawned >= 0 && surfaced >= 0 && fired >= 0 && hit >= 0;
    printf("    RESULT sub: %s\n", ok ? "OK" : "FAIL");
    return !ok;
}

int main(void)
{
    vfs_mount_default();                                                  /* ./cd or $OPENRF_DATA */
    if (!exe_load()) { fprintf(stderr, "%s\n", exe_error()); return 1; }   /* tables from RFIRE.BIN */
    SpriteBank *sb = malloc(sizeof *sb);
    if (!sprites_load(sb, "ART/ART.CAR")) { fprintf(stderr, "cannot load ART.CAR (run from the project root)\n"); return 1; }
    render_init(sb);
    fb = (Framebuffer){ 320, 240, calloc(320, 240), { { 0 } } };
    view = calloc(1, sizeof *view);
    view_init(view, 0, 0, 320, 152);
    view_set_pitch_height(view, 0x180000, -0xaa0000);
    int fails = 0;
    fails += test_turret();
    fails += test_drone();
    fails += test_sub();
    printf("\n%s (%d failing groups)\n", fails ? "FAIL" : "ALL OK", fails);
    return fails != 0;
}
