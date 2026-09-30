/* The submarine (class 15 "SUB", descriptor 0x43fc10): created by SpawnSub 0x4076f0 whenever a
   helicopter is more than a cell off the map (Heli_Move). It waits until a team vehicle has been on
   the off-map pseudo cell for 240 ticks (FUN_00407470), jumps under the one farthest out, surfaces
   (FUN_00407580), fires a homing Death Missle (projectile type 10, pitch 0x355555) 180 ticks after
   surfacing (FUN_00407620), stays up while the missile flies (FUN_004076b0) and dives (FUN_004075f0).
   While it exists the music director plays MUS_SUB (Sub_UpdateSetsMusicFlag 0x4073a0 sets
   0x1000 in DAT_00480e3c). Graphic 0x43fbb0 (draw 0x407320: sprite = frame - 1). See docs/game.md §9. */
#include "ai.h"
#include "weapon.h"
#include "../exe.h"
#include <stddef.h>

extern Gfx gfx_43fbb0;

Obj *sub_obj;                                 /* DAT_0048bb1c */
uint32_t mus_director_flags;                  /* _DAT_00480e3c */
static int32_t sub_muzzle[3];                 /* 0x43fbf8 */
static uint8_t level_pool[9];                 /* 0x443880: AI pool by level */
static void sub_tables_load(void)
{
    for (int i = 0; i < 3; i++) sub_muzzle[i] = exe_s32(0x43fbf8 + 4 * (uint32_t)i);
    for (int i = 0; i < 9; i++) level_pool[i] = exe_u8(0x443880 + (uint32_t)i);
}
EXE_LOADER(sub_tables_load)
enum { SUB_ANIM_RATE = 0x3333, SUB_WAIT = 240 };            /* DAT_0043fc04 / DAT_0043fc08 */

static int think_idle(Obj *o);
static int think_surface(Obj *o);
static int think_dive(Obj *o);
static int think_fire(Obj *o);
static int think_watch(Obj *o);

static bool off_map(const Obj *v) { return v->cell == &G.outside; }

static int think_idle(Obj *o)                 /* FUN_00407470 (class default think) */
{
    int32_t far = 0;
    Obj *best = NULL;
    for (int t = 0; t < 2; t++) {
        Obj *v = get_team_vehicle(t);
        if (!v || !off_map(v)) continue;
        int32_t x = v->pos[0], y = v->pos[1];
        if (x < 0 && -far != x && far <= -x) { far = -x; best = v; }
        if (y < 0 && -far != y && far <= -y) { far = -y; best = v; }
        if (far < x - 0x10000000) { far = x - 0x10000000; best = v; }
        if (far < y - 0x10000000) { far = y - 0x10000000; best = v; }
    }
    if (best) {
        SUB_TIMER(o) += g_dt;
        if (SUB_WAIT < SUB_TIMER(o)) {
            obj_move_to(o, best->pos[0] + 0x20, best->pos[1] + 0x20, 0);
            obj_set_parent(o, best);
            o->think = think_surface;
            return 1;
        }
        SUB_STATE(o) = 0;
        return 0;
    }
    SUB_STATE(o) = 0;
    if (SUB_FRAME(o) < 1) { o->think = NULL; return 1; }
    return 0;
}

/* The target must still be off the map and the sub drawn recently: `drawn <= tick + 60` as in the
   original (always true, the draw stores the current tick). */
static bool target_ok(const Obj *o)
{
    return o->parent && off_map(o->parent) && SUB_DRAWN(o) <= g_tick + 60;
}

static int think_surface(Obj *o)              /* FUN_00407580 */
{
    SUB_STATE(o) = 1;
    if (target_ok(o)) {
        if (SUB_FRAME(o) > 0x13ffff) {
            o->think = think_fire;
            SUB_TIMER(o) = g_tick + 180;
        }
        return 0;
    }
    obj_set_parent(o, NULL);
    o->think = think_dive;
    return 1;
}

static int think_dive(Obj *o)                 /* FUN_004075f0 */
{
    SUB_STATE(o) = 2;
    if (SUB_FRAME(o) < 1) {
        o->think = think_idle;
        SUB_TIMER(o) = 0;
        return 1;
    }
    return 0;
}

static int think_fire(Obj *o)                 /* FUN_00407620 */
{
    SUB_STATE(o) = 1;
    Obj *tg = o->parent;
    if (target_ok(o)) {
        if (g_tick < SUB_TIMER(o)) return 0;
        Obj *m = fire_projectile(o->pos, sub_muzzle, 0, 0x355555, 10, 0, tg);   /* homes on its parent */
        if (m) {
            o->think = think_watch;
            obj_set_parent(o, m);
        }
        return 0;
    }
    obj_set_parent(o, NULL);
    o->think = think_watch;
    return 1;
}

static int think_watch(Obj *o)                /* FUN_004076b0: surfaced while the missile flies */
{
    if (!o->parent) {
        SUB_STATE(o) = 2;
        obj_set_parent(o, NULL);
        o->think = think_dive;
        return 1;
    }
    SUB_STATE(o) = 1;
    return 0;
}

static int sub_init(const ObjClass *c, Obj *o, void *arg)       /* SubInit 0x407350 */
{
    (void)c; (void)arg;
    SUB_FRAME(o) = 0;
    SUB_DRAWN(o) = g_tick;
    SUB_STATE(o) = 0;
    SUB_TIMER(o) = 0;
    return 1;
}

static int sub_destroy(const ObjClass *c, Obj *o)               /* SubDestroy 0x407380 */
{
    (void)c;
    if (sub_obj == o) sub_obj = NULL;
    return 1;
}

static void sub_update(const ObjClass *c, Obj *o)               /* Sub_UpdateSetsMusicFlag 0x4073a0 */
{
    (void)c;
    mus_director_flags |= 0x1000;
    do {
        if (!o->think) { obj_destroy_now(o); return; }
    } while (o->think(o) != 0);
    /* animation frame: 1..0x13 surfacing / diving, 0x14..0x19 surfaced (wraps) */
    int32_t f = SUB_FRAME(o), step = SUB_ANIM_RATE * g_dt;
    int st = SUB_STATE(o);
    if (st == 1) {
        f += step;
        if (f > 0x190000) f -= (int32_t)((uint32_t)(f - 0x140000) / 0x50001u) * 0x50001;
    } else if (st == 0 || st == 2) {
        if (f < 0x140000) {
            f -= step;
            if (f < 0) { SUB_FRAME(o) = 0; return; }
        } else {
            f += step;
            if (f > 0x190000) { SUB_FRAME(o) = f - (int32_t)((uint32_t)(f - 0xeffff) / 0x50001u) * 0x50001; return; }
        }
    }
    SUB_FRAME(o) = f;
}

const ObjClass class_sub = {
    15, OBJ_CLASS_NAME, sub_init, sub_destroy, sub_update, &gfx_43fbb0, NULL, think_idle,
    NULL, NULL, NULL, NULL, 0x7d, NULL, NULL, NULL, NULL, 0
};

void spawn_sub(void)                          /* SpawnSub 0x4076f0 */
{
    if (!sub_obj) sub_obj = obj_create(&class_sub, 0, 0, 0, 0, NULL);
}

/* ---- per level / per frame ---- */
static int mus_state;                         /* DAT_00480e5c (only the sub transitions are driven here) */

static void on_drone(Obj *target) { spawn_drone(target); }

void ai_level_start(void)
{
    int pool = G.ai_pool >= 0 && G.ai_pool != 0xff ? G.ai_pool : level_pool[G.level < 0 ? 0 : G.level > 8 ? 8 : G.level];
    drone_pool_init(pool);
    turret_count[0] = turret_count[1] = 0;
    turret_near_dist[0] = turret_near_dist[1] = 0x7fffffff;
    tower_yaw = -1;
    sub_obj = NULL;
    mus_director_flags = 0;
    mus_state = 0;
    game_hooks.drone = on_drone;
    game_hooks.sub = spawn_sub;
}

void ai_frame_end(void)                       /* Mus_Director 0x41a1c0: the 0x1000 (SUB) branch */
{
    if (mus_director_flags & 0x1000) {
        if (mus_state < 6) {
            if (game_hooks.music) game_hooks.music(16 /* MUS_SUB */, 0x8a, NULL);
            mus_state = 6;
        }
    } else if (mus_state == 6) {
        mus_state = 1;
        if (game_hooks.music) game_hooks.music(-1, 100, NULL);    /* state 1 -> Mus_Request(-1, 100) */
        mus_state = 0;
    }
    mus_director_flags &= ~0x1000u;
}
