#!/usr/bin/env python3
"""Generate src/game/game_tables.c: the layout (addresses, counts, types) of the collision shapes, graphic
descriptors used for collision, static-object (BNO) game fields, shore-water shapes, vehicle descriptors,
tunables, bunker scripts, projectile / explosion / debris tables of RFIRE.BIN, plus the loader that fills
the C structures from the user's RFIRE.BIN at start-up (exe.c). The generated file contains no data from
the executable; the executable is only needed to regenerate it (to discover chains and counts).

Usage: python3 tools/gen_game_tables.py [path/to/RFIRE.BIN] > src/game/game_tables.c
All addresses are VAs in RFIRE.BIN (image base 0x400000). See docs/game.md.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rfexe  # noqa: E402

pe = rfexe.Exe(sys.argv[1] if len(sys.argv) > 1 else None)
rd, u32, s32 = pe.rd, pe.u32, pe.s32

out = []
P = out.append

# ---------------------------------------------------------------- discovery (layout only)
parts = {}      # addr -> (nverts buffer, nedges buffer) or None (no vertex data)
order = []


def add_part(p):
    while p and p not in parts:
        t = s32(p)
        if t in (3, 4):
            parts[p] = (max(1, s32(p + 0x2c)), max(1, s32(p + 0x34)))
        else:
            parts[p] = None
        order.append(p)
        p = u32(p + 4)


gfxs = []


def add_gfx(g):
    if not g or g in gfxs:
        return
    assert u32(g + 0x28) in (0, 0x41f210), hex(g)
    gfxs.append(g)
    if u32(g + 8):
        add_part(u32(g + 8))


for i in range(91):                       # BNO table 0x451438, 91 x 0x38
    a = 0x451438 + i * 0x38
    assert u32(a + 0x18) == 0             # BNO +0x18 custom damage function: unused
    add_gfx(u32(a + 8))
VDEF = [u32(0x44afc8 + 4 * i) for i in range(4)]
for t in VDEF:
    for idx in (0x52, 0x53, 0x55, 0x58, 0x59):
        add_gfx(u32(t + idx * 4))
add_gfx(0x4431f8)          # Storage (bunker lift) class graphic
# combat graphics: projectile models/shadows (type table 0x450a78 +0x2c/+0x30), grenade, class gfx,
# explosion parts 0x452dc8 + i*0x44, explosion-label graphics, debris / Stay / Mine class graphics.
PROJ = 0x450a78
for i in range(12):
    add_gfx(u32(PROJ + i * 0x3c + 0x2c))
    add_gfx(u32(PROJ + i * 0x3c + 0x30))
for g in (0x449aa0, 0x449ae8, 0x449628, 0x44aa20, 0x452dc8, 0x452e0c, 0x44c668, 0x44c6b0, 0x44a610,
          0x454f18, 0x4557f0, 0x4493b0, 0x449478):
    add_gfx(g)
for i in range(4):          # burnt wrecks (def[0x58]) are Stay graphics too
    add_gfx(u32(VDEF[i] + 0x58 * 4))
for g in (0x455680, 0x455730, 0x43fbb0):   # Drone class graphic + its shadow 0x455730, SUB 0x43fbb0 (ai port)
    add_gfx(g)
add_part(0x443330)         # default point part used by GetObjWaterState when an object has no shape
for t in range(4, 24):     # shore polygons 0x443558 + t*8 (+4)
    if u32(0x443558 + t * 8 + 4):
        add_part(u32(0x443558 + t * 8 + 4))

HIT = {0: 'BHIT_NONE', 0x41f290: 'BHIT_BUSH', 0x41f260: 'BHIT_ROCK', 0x41f340: 'BHIT_PICKUP', 0x41f6a0: 'BHIT_TENT',
       0x4247e0: 'BHIT_DEST_FLAG'}
DES = {0: 'BDES_DEFAULT', 0x417a00: 'BDES_WALL', 0x423510: 'BDES_TOWER', 0x424170: 'BDES_FLAG_BUILDING',
       0x41f9a0: 'BDES_BRIDGE'}
for i in range(91):
    a = 0x451438 + i * 0x38
    assert u32(a + 0x14) in HIT and u32(a + 0x1c) in DES, i
# vehicle descriptor hooks (code addresses -> port functions)
FN = [(0x429f20, 'hook', 'veh_tank_launch'), (0x42a250, 'hook', 'veh_msv_launch'), (0x42a410, 'hook', 'veh_jeep_launch'),
      (0x42b3c0, 'hook', 'veh_heli_spinup'), (0x42ab90, 'hook', 'veh_jeep_dock'), (0x42b600, 'hook', 'veh_heli_dock'),
      (0x42a670, 'move', 'veh_move_jeep'), (0x42abe0, 'move', 'veh_move_heli'), (0x4299e0, 'water', 'veh_water_normal'),
      (0x429d30, 'fire', 'veh_tank_fire'), (0x42aa00, 'fire', 'veh_jeep_grenade'), (0x42aae0, 'fire', 'veh_jeep_boat'),
      (0x42ab70, 'fire', 'veh_jeep_drop_flag'), (0x42a010, 'fire', 'veh_msv_fire'), (0x42a310, 'fire', 'veh_msv_mine'),
      (0x42b100, 'fire', 'veh_heli_fire'), (0x42b2a0, 'fire', 'veh_heli_missile'), (0x42b5e0, 'init', 'veh_heli_init'),
      (0x42b330, 'damage', 'veh_heli_damaged')]
FNKIND = {a: k for a, k, _ in FN}
for t in VDEF:
    for i, k in ((5, 'hook'), (6, 'move'), (0x13, 'water'), (0x5f, 'fire'), (0x60, 'fire'), (0x61, 'fire'),
                 (0x8c, 'init'), (0x8d, 'hook'), (0x8e, 'damage'), (0x96, 'hook')):
        v = u32(t + 4 * i)
        assert v == 0 or FNKIND.get(v) == k, (hex(t), hex(i), hex(v))
nsteps = []
for i in range(4):
    p = u32(0x43f1d8 + i * 0x28 + 0x20)
    n = 0
    while n < 16 and s32(p + 20 * n):
        n += 1
    assert n <= 8
    nsteps.append(n)
CLS = {0x450950: 'class_missle', 0x4509a0: 'class_tracer', 0x4509f8: 'class_death_missle'}
ETAB = {0x450a48: 'expl_tab_450a48', 0x450a60: 'expl_tab_450a60'}
for i in range(12):
    a = PROJ + i * 0x3c
    assert u32(a) in CLS and u32(a + 0x34) in ETAB
    assert u32(a + 16) == 0 and u32(a + 20) == 0 and u32(a + 32) == 0 and u32(a + 28) in (0, 0x431100)
DEB = {}       # debris programs: DebrisInit 0x4344b0 arg
SETS = [[u32(0x454e18 + 16 * k + 4 * j) for j in range(4)] for k in range(10)]
TOWER_SET = [u32(0x454730 + 4 * j) for j in range(4)]
for p in [p for s in SETS for p in s] + TOWER_SET + [0x44a0a0]:
    if p and p not in DEB:
        DEB[p] = (s32(p + 12), u32(p + 16))
steps = {}
for p in sorted(DEB):
    n, sp = DEB[p]
    steps.setdefault(sp, n)       # programs may share a step list (a prefix of it)
    assert n <= steps[sp]

# ---------------------------------------------------------------- emit
P('/* Generated by tools/gen_game_tables.py from the layout of RFIRE.BIN. Do not edit.')
P('   Collision shapes, graphic descriptors (collision fields only), BNO game fields, shore-water shapes,')
P('   vehicle descriptors (pointer table 0x44afc8), tunables, bunker scripts and the combat tables.')
P('   Addresses, counts and types only: the contents are read from the user\'s RFIRE.BIN at start-up')
P('   (exe.c, game_tables_load below). */')
P('#include <stddef.h>')
P('#include <stdlib.h>')
P('#include <stdio.h>')
P('#include "game.h"')
P('#include "vehicle.h"')
P('#include "weapon.h"')
P('#include "effect.h"')
P('#include "../exe.h"')
P('')
P('/* ---- collision shape parts (linked lists, see ShapesOverlap 0x41c800); buffers sized by +0x2c / +0x34 ---- */')
for p in order:
    P('static ShapePart shp_%x;' % p)
for p in order:
    if parts[p]:
        P('static int32_t shpv_%x[%d][3];' % (p, parts[p][0]))
        P('static uint8_t shpe_%x[%d];' % (p, parts[p][1]))
P('typedef struct { ShapePart *s; uint32_t addr; int32_t (*v)[3]; int nv; uint8_t *e; int ne; } ShapeDesc;')
P('static const ShapeDesc shape_descs[%d] = {' % len(order))
for p in order:
    if parts[p]:
        P('    { &shp_%x, 0x%x, shpv_%x, %d, shpe_%x, %d },' % (p, p, p, parts[p][0], p, parts[p][1]))
    else:
        P('    { &shp_%x, 0x%x, NULL, 0, NULL, 0 },' % (p, p))
P('};')
P('ShapePart *const shape_default_point = &shp_443330;')
P('')
P('/* ---- graphic descriptors: only +8 shape, +0xc radius and +0x28 position-adjust hook are used by the sim ---- */')
for k in range(0, len(gfxs), 6):
    P('Gfx %s;' % ', '.join('gfx_%x' % g for g in gfxs[k:k + 6]))
P('static Gfx *const gfx_descs[%d] = { %s };' % (len(gfxs), ', '.join('&gfx_%x' % g for g in gfxs)))
P('static const uint32_t gfx_addrs[%d] = { %s };' % (len(gfxs), ', '.join('0x%x' % g for g in gfxs)))
P('/* every graphic above, sorted by address (gfx_by_addr) */')
P('const Gfx *const gfx_table[] = { %s };' % ', '.join('&gfx_%x' % g for g in sorted(gfxs)))
P('const int gfx_table_count = %d;' % len(gfxs))
P('')
P('/* ---- static map objects: BNO table 0x451438 (stride 0x38) ---- */')
P('BnoDef bno_defs[BNO_COUNT];')
P('ShoreShape shore_shapes[24];        /* shore tiles 4..23: {invert, rotation (heading>>16), polygon} from 0x443558 */')
P('int32_t shore_boxes[12][4];         /* shore tiles 40..51: land AABB relative to the cell centre (0x443618) */')
P('')
P('/* ---- vehicle descriptors (0x44afc8 -> Tank 0x44b3a8, JEEP 0x44b690, MSV 0x44b978, HELI 0x44bc60) ---- */')
P('VehicleDef vehicle_defs[4];')
P('')
P('/* Misc tunables. */')
P('int32_t tun_launch_ticks;            /* 0x44b178: tank/MSV forced-forward ticks off the lift */')
P('int32_t tun_idle_drone_ticks;        /* 0x44afa8: same-cell ticks before a Drone is sent */')
P('int32_t tun_door_anim_rate;          /* 0x44afdc */')
P('int32_t tun_heli_takeoff_dx;         /* 0x44b184 */')
P('int32_t tun_heli_turn_up, tun_heli_turn_down; /* 0x44b188 / 0x44b18c */')
P('int32_t tun_tank_long_pitch;         /* 0x44b174 */')
P('int32_t tun_pitch_rate[4][2];        /* 0x44b098 */')
P('int32_t tun_resupply_slots[4];       /* 0x44b0b8 */')
P('int32_t tun_road_flags[17];          /* 0x44af2c[0x49..0x59]: road direction bits by terrain */')
P('int8_t tun_heli_lift_z[56];          /* 0x44b190: obj+0x70 bob by altitude */')
P('int8_t tun_heli_lift_tilt[56];       /* 0x44b1c8: state+0x88 by altitude */')
P('uint8_t loadout_default[20];         /* 0x4438b8 (bytes 12..15 are overwritten by the level VHCL defaults) */')
P('uint8_t bunker_menu_nav[4][4];       /* 0x43f1fc stride 0x28: up, down, left, right */')
P('BunkerStep bunker_scripts[4][9];     /* bunker zoom scripts (menu table 0x43f1d8 +0x20), 0-terminated */')
P('static const int bunker_script_len[4] = { %s };' % ', '.join(str(n) for n in nsteps))
P('')
P('/* ==== combat tables (src/game/weapon.c, effect.c) ==== */')
P('/* explosion descriptors by projectile hit kind (0 ground, 1 water, 2 road, 3 object, 4 static) */')
P('uint32_t expl_tab_450a48[6];')
P('uint32_t expl_tab_450a60[6];         /* also the grenade table (GrenadeDestroy 0x4319d0) */')
P('uint32_t expl_tab_vehicle[4];        /* 0x44b0c8: vehicle death explosion by type */')
P('uint32_t snd_expl_tab[40];           /* 0x43ede0: explosion script sound events */')
P('uint32_t snd_grenade_tab[3];         /* 0x43ee2c */')
P('ProjType proj_types[12];             /* projectile type table 0x450a78 (12 x 0x3c) */')
P('')
P('/* debris (FWall) programs: DebrisInit 0x4344b0 arg; steps dispatched through 0x454ee8 */')
for sp in sorted(steps):
    P('static DebrisStep dstep_%x[%d];' % (sp, steps[sp]))
for k in range(0, len(DEB), 6):
    P('DebrisDef %s;' % ', '.join('debris_%x' % p for p in sorted(DEB)[k:k + 6]))
P('typedef struct { DebrisDef *d; uint32_t addr; DebrisStep *steps; int nsteps; } DebrisDesc;')
P('static const DebrisDesc debris_descs[%d] = {' % len(DEB))
for p in sorted(DEB):
    P('    { &debris_%x, 0x%x, dstep_%x, %d },' % (p, p, DEB[p][1], DEB[p][0]))
P('};')
P('const DebrisDef *debris_sets[10][4];      /* 0x454e18: debris class by face flag bits 8-9 */')
P('const DebrisDef *debris_set_tower[4];     /* 0x454730 (TowerDestroyed) */')
P('')
P('/* weapon geometry / tunables */')
P('int32_t muzzle_tank[2][3];            /* 0x44c6f8 barrel tip, 0x44c704 turret pivot */')
P('int32_t msv_muzzle_raw[16];           /* 0x44d088: tube offsets (3 x 3, overlapping) + [10] 0x44d0b0 rocket/flash points */')
P('int32_t muzzle_heli[2][6];            /* 0x44f218 / 0x44f230: offset + flash offset (L/R) */')
P('int32_t tun_msv_pitch_lob, tun_msv_pitch_flat;   /* 0x44b17c / 0x44b180 */')
P('int32_t tun_dmissile_turn, tun_dmissile_pitch;   /* 0x4509ec / 0x4509f0 */')
P('int32_t tun_gravity, tun_fall_max;    /* 0x4546b0 / 0x4546b4 (FUN_004337d0) */')
P('int32_t tun_bridge[7];                /* 0x44a128..0x44a140 bridge debris randomness */')
P('int32_t bridge_debris_verts[2][4][3]; /* 0x44a0b8 (V bridge) / 0x44a0e8 (H), patched per piece */')
P('')
P('/* ---- code addresses referenced by the tables -> port functions / enums ---- */')
P('typedef struct { uint32_t va; VehHookFn hook; VehMoveFn move; VehWaterFn water; VehFireFn fire; VehInitFn init; VehDamageFn damage; } VehFn;')
P('static const VehFn veh_fns[] = {')
for a, k, n in FN:
    P('    { 0x%x, .%s = %s },' % (a, k, n))
P('};')
P('static const uint32_t bno_hit_fns[] = { %s };   /* BHIT_* */' % ', '.join('0x%x' % a for a in sorted(HIT, key=lambda a: list(HIT.values()).index(HIT[a]))))
P('static const uint32_t bno_des_fns[] = { %s };   /* BDES_* */' % ', '.join('0x%x' % a for a in sorted(DES, key=lambda a: list(DES.values()).index(DES[a]))))
P('static const uint32_t proj_cls_va[3] = { %s };' % ', '.join('0x%x' % a for a in CLS))
P('static const ObjClass *const proj_cls[3] = { %s };' % ', '.join('&' + n for n in CLS.values()))
P('static const uint32_t proj_expl_va[2] = { %s };' % ', '.join('0x%x' % a for a in ETAB))
P('static const uint32_t *const proj_expl[2] = { %s };' % ', '.join(ETAB.values()))
P(r'''
/* ---- loader ---- */
static void fail(const char *what, uint32_t va)
{
    fprintf(stderr, "game_tables: unexpected %s at 0x%x in RFIRE.BIN\n", what, va);
    abort();
}

static void rd_s32(int32_t *dst, uint32_t va, int n) { for (int i = 0; i < n; i++) dst[i] = exe_s32(va + 4 * (uint32_t)i); }
static void rd_u32(uint32_t *dst, uint32_t va, int n) { for (int i = 0; i < n; i++) dst[i] = exe_u32(va + 4 * (uint32_t)i); }
static void rd_u8(uint8_t *dst, uint32_t va, int n) { for (int i = 0; i < n; i++) dst[i] = exe_u8(va + (uint32_t)i); }

static ShapePart *shape_at(uint32_t va)
{
    if (!va) return NULL;
    for (size_t i = 0; i < sizeof shape_descs / sizeof *shape_descs; i++)
        if (shape_descs[i].addr == va) return shape_descs[i].s;
    fail("shape part", va);
    return NULL;
}

static Gfx *gfx_at(uint32_t va)
{
    if (!va) return NULL;
    for (size_t i = 0; i < sizeof gfx_addrs / sizeof *gfx_addrs; i++)
        if (gfx_addrs[i] == va) return gfx_descs[i];
    fail("graphic", va);
    return NULL;
}

static const DebrisDef *debris_at(uint32_t va)
{
    if (!va) return NULL;
    for (size_t i = 0; i < sizeof debris_descs / sizeof *debris_descs; i++)
        if (debris_descs[i].addr == va) return debris_descs[i].d;
    fail("debris program", va);
    return NULL;
}

static const VehFn *veh_fn(uint32_t va)
{
    static const VehFn none;
    if (!va) return &none;
    for (size_t i = 0; i < sizeof veh_fns / sizeof *veh_fns; i++)
        if (veh_fns[i].va == va) return &veh_fns[i];
    fail("vehicle hook", va);
    return &none;
}

static uint8_t fn_index(const uint32_t *tab, int n, uint32_t va)
{
    for (int i = 0; i < n; i++) if (tab[i] == va) return (uint8_t)i;
    fail("handler", va);
    return 0;
}

static void load_shape(const ShapeDesc *d)
{
    ShapePart *s = d->s;
    uint32_t p = d->addr;
    s->type = exe_s32(p);
    s->next = shape_at(exe_u32(p + 4));
    s->b8 = exe_u8(p + 8); s->kind = exe_u8(p + 9); s->solid = exe_u8(p + 10); s->mask = exe_u8(p + 11);
    s->zlo = exe_s32(p + 0xc); s->zhi = exe_s32(p + 0x10);
    s->dx = exe_s32(p + 0x14); s->dy = exe_s32(p + 0x18);
    rd_s32(s->bbox, p + 0x1c, 4);
    s->nverts = 0; s->verts = NULL; s->nedges = 0; s->edges = NULL;
    if (d->v) {                                   /* types 3/4: vertex and edge lists */
        s->nverts = exe_s32(p + 0x2c);
        s->nedges = exe_s32(p + 0x34);
        uint32_t vp = exe_u32(p + 0x30), ep = exe_u32(p + 0x38);
        if (s->nverts > d->nv || s->nedges > d->ne) fail("shape size", p);
        for (int i = 0; i < s->nverts; i++) rd_s32(d->v[i], vp + 12 * (uint32_t)i, 3);
        if (ep) rd_u8(d->e, ep, s->nedges);
        s->verts = d->v;
        s->edges = d->e;
    }
    s->addr = p;
}

static void load_vehicle(VehicleDef *v, uint32_t t)
{
    uint32_t w[0xb8];
    rd_u32(w, t, 0xb8);
#define I(k) ((int32_t)w[k])
    v->type = I(0);
    v->name = exe_str(w[1]);
    v->launch_hook = veh_fn(w[5])->hook;
    v->move = veh_fn(w[6])->move;
    v->armour = I(9); v->hp = I(0xa);
    v->water_hook = veh_fn(w[0x13])->water;
    v->cam_flags = I(0x31);
    for (int i = 0; i < 24; i++) v->cam[i] = I(0x3a + i);
    v->gfx = gfx_at(w[0x52]); v->gfx_splash = gfx_at(w[0x53]); v->gfx_sink = gfx_at(w[0x55]);
    v->sink_depth = I(0x56); v->unk57 = I(0x57);
    v->gfx_wreck = gfx_at(w[0x58]); v->gfx_wreck2 = gfx_at(w[0x59]);
    v->max_fwd = I(0x5a); v->max_rev = I(0x5b); v->accel = I(0x5c); v->friction = I(0x5d); v->turn = I(0x5e);
    for (int i = 0; i < 3; i++) { v->fire[i] = veh_fn(w[0x5f + i])->fire; v->fire_arg[i] = I(0x62 + i); }
    for (int s = 0; s < 3; s++) {
        const uint32_t *x = &w[0x65 + 13 * s];
        WeaponDef *d = &v->weapon[s];
        d->proj = (int32_t)x[0];
        for (int k = 0; k < 3; k++) d->arg[k] = (int32_t)x[1 + k];
        d->reload = (int32_t)x[4]; d->ammo = (int32_t)x[5]; d->hud_kind = (int32_t)x[6];
        for (int k = 0; k < 4; k++) d->hud_rect[k] = (int32_t)x[7 + k];
        d->hud_scale = (int32_t)x[11]; d->w12 = (int32_t)x[12];
    }
    v->init_hook = veh_fn(w[0x8c])->init;
    v->death_hook = veh_fn(w[0x8d])->hook;
    v->damage_hook = veh_fn(w[0x8e])->damage;
    v->snd_engine = w[0x90]; v->snd_91 = I(0x91); v->snd_92 = I(0x92);
    v->cam_attach = w[0x94]; v->dock_radius = I(0x95);
    v->dock_hook = veh_fn(w[0x96])->hook;
    v->wreck_friction = I(0x97); v->respawn_delay = I(0x98);
    v->radar_99 = I(0x99); v->radar_icon[0] = w[0x9a]; v->radar_icon[1] = w[0x9b];
    for (int i = 0; i < 5; i++) v->hud9[i] = I(0x9c + i);
    v->hud_icon[0] = I(0xa1); v->hud_icon[1] = I(0xa2);
    for (int i = 0; i < 3; i++) v->unk_a3[i] = I(0xa3 + i);
    v->arrow_enable = w[0xa6];
    for (int i = 0; i < 4; i++) { v->unk_a7[i] = I(0xa7 + i); v->arrow_box[i] = I(0xab + i); }
    v->music = w[0xaf];
    for (int i = 0; i < 8; i++) v->tail[i] = I(0xb0 + i);
#undef I
}

static void game_tables_load(void)
{
    for (size_t i = 0; i < sizeof shape_descs / sizeof *shape_descs; i++) load_shape(&shape_descs[i]);
    for (size_t i = 0; i < sizeof gfx_addrs / sizeof *gfx_addrs; i++) {
        uint32_t g = gfx_addrs[i], adj = exe_u32(g + 0x28);
        if (adj != 0 && adj != 0x41f210) fail("graphic adjust hook", g);
        *gfx_descs[i] = (Gfx){ g, shape_at(exe_u32(g + 8)), exe_s32(g + 12), adj ? GADJ_JITTER : GADJ_NONE };
    }
    for (int i = 0; i < BNO_COUNT; i++) {
        uint32_t a = 0x451438 + 0x38 * (uint32_t)i;
        BnoDef *b = &bno_defs[i];
        b->name = exe_str(exe_u32(a + 4));
        b->gfx = gfx_at(exe_u32(a + 8));
        b->flags = exe_u32(a + 0xc);
        b->terrain = exe_u8(a + 0x10); b->hp = exe_u8(a + 0x11);
        b->hit = fn_index(bno_hit_fns, (int)(sizeof bno_hit_fns / sizeof *bno_hit_fns), exe_u32(a + 0x14));
        b->destroy = fn_index(bno_des_fns, (int)(sizeof bno_des_fns / sizeof *bno_des_fns), exe_u32(a + 0x1c));
        b->armour = exe_s32(a + 0x20); b->mult = exe_s32(a + 0x24);
        b->expl_crush = exe_u32(a + 0x28); b->expl = exe_u32(a + 0x2c);
        b->dest_terrain = exe_u8(a + 0x30); b->dest_bno = exe_u8(a + 0x31);
    }
    for (int t = 0; t < 24; t++) {
        uint32_t a = 0x443558 + 8 * (uint32_t)t;
        shore_shapes[t] = (ShoreShape){ exe_u8(a), exe_s8(a + 1), t >= 4 ? shape_at(exe_u32(a + 4)) : NULL };
    }
    rd_s32(&shore_boxes[0][0], 0x443618, 12 * 4);
    for (int i = 0; i < 4; i++) load_vehicle(&vehicle_defs[i], exe_u32(0x44afc8 + 4 * (uint32_t)i));

    tun_launch_ticks = exe_s32(0x44b178);
    tun_idle_drone_ticks = exe_s32(0x44afa8);
    tun_door_anim_rate = exe_s32(0x44afdc);
    tun_heli_takeoff_dx = exe_s32(0x44b184);
    tun_heli_turn_up = exe_s32(0x44b188); tun_heli_turn_down = exe_s32(0x44b18c);
    tun_tank_long_pitch = exe_s32(0x44b174);
    rd_s32(&tun_pitch_rate[0][0], 0x44b098, 8);
    rd_s32(tun_resupply_slots, 0x44b0b8, 4);
    rd_s32(tun_road_flags, 0x44af2c + 4 * 0x49, 17);
    rd_u8((uint8_t *)tun_heli_lift_z, 0x44b190, 56);
    rd_u8((uint8_t *)tun_heli_lift_tilt, 0x44b1c8, 56);
    rd_u8(loadout_default, 0x4438b8, 20);
    for (int i = 0; i < 4; i++) {
        uint32_t m = 0x43f1d8 + 0x28 * (uint32_t)i;
        rd_u8(bunker_menu_nav[i], m + 0x24, 4);
        uint32_t p = exe_u32(m + 0x20);
        for (int k = 0; k < 9; k++) bunker_scripts[i][k] = (BunkerStep){ 0, 0, 0, 0, 0 };
        for (int k = 0; k < bunker_script_len[i]; k++) {
            uint32_t e = p + 20 * (uint32_t)k;
            bunker_scripts[i][k] = (BunkerStep){ exe_u32(e), exe_s32(e + 4), exe_s32(e + 8), exe_s32(e + 12), exe_s32(e + 16) };
        }
        if (exe_s32(p + 20 * (uint32_t)bunker_script_len[i])) fail("bunker script length", p);
    }

    rd_u32(expl_tab_450a48, 0x450a48, 6);
    rd_u32(expl_tab_450a60, 0x450a60, 6);
    rd_u32(expl_tab_vehicle, 0x44b0c8, 4);
    rd_u32(snd_expl_tab, 0x43ede0, 40);
    rd_u32(snd_grenade_tab, 0x43ee2c, 3);
    for (int i = 0; i < 12; i++) {
        uint32_t a = 0x450a78 + 0x3c * (uint32_t)i;
        ProjType *t = &proj_types[i];
        uint32_t c = exe_u32(a), x = exe_u32(a + 0x34);
        t->cls = c == proj_cls_va[0] ? proj_cls[0] : c == proj_cls_va[1] ? proj_cls[1] : c == proj_cls_va[2] ? proj_cls[2] : NULL;
        t->expl = x == proj_expl_va[0] ? proj_expl[0] : x == proj_expl_va[1] ? proj_expl[1] : NULL;
        if (!t->cls || !t->expl) fail("projectile type", a);
        t->index = exe_s32(a + 4); t->flags = exe_u32(a + 8); t->speed = exe_s32(a + 12);
        t->snd = exe_u32(a + 0x18); t->impact = exe_u32(a + 0x1c) != 0;
        t->damage = exe_s32(a + 0x24); t->pitch_rate = exe_s32(a + 0x28);
        t->gfx = gfx_at(exe_u32(a + 0x2c)); t->shadow = gfx_at(exe_u32(a + 0x30));
        t->life = exe_u8(a + 0x38); t->nframes = exe_u8(a + 0x39); t->period = exe_u8(a + 0x3a); t->splash = exe_u8(a + 0x3b);
    }
    for (size_t i = 0; i < sizeof debris_descs / sizeof *debris_descs; i++) {
        const DebrisDesc *d = &debris_descs[i];
        uint32_t p = d->addr, sp = exe_u32(p + 16);
        if (exe_s32(p + 12) != d->nsteps) fail("debris step count", p);
        for (int k = 0; k < d->nsteps; k++) rd_s32(&d->steps[k].op, sp + 32 * (uint32_t)k, 8);
        *d->d = (DebrisDef){ exe_s32(p), exe_s32(p + 4), exe_s32(p + 8), d->nsteps, d->steps, p };
    }
    for (int k = 0; k < 10; k++)
        for (int j = 0; j < 4; j++) debris_sets[k][j] = debris_at(exe_u32(0x454e18 + 16 * (uint32_t)k + 4 * (uint32_t)j));
    for (int j = 0; j < 4; j++) debris_set_tower[j] = debris_at(exe_u32(0x454730 + 4 * (uint32_t)j));

    rd_s32(&muzzle_tank[0][0], 0x44c6f8, 6);
    rd_s32(msv_muzzle_raw, 0x44d088, 16);
    rd_s32(&muzzle_heli[0][0], 0x44f218, 12);
    tun_msv_pitch_lob = exe_s32(0x44b17c); tun_msv_pitch_flat = exe_s32(0x44b180);
    tun_dmissile_turn = exe_s32(0x4509ec); tun_dmissile_pitch = exe_s32(0x4509f0);
    tun_gravity = exe_s32(0x4546b0); tun_fall_max = exe_s32(0x4546b4);
    rd_s32(tun_bridge, 0x44a128, 7);
    rd_s32(&bridge_debris_verts[0][0][0], 0x44a0b8, 24);
}
EXE_LOADER(game_tables_load)''')
print('\n'.join(out))
