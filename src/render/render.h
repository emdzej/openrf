/* World view renderer (RenderWorldView 0x419dd0): camera, perspective floor walk, depth-sorted
   3D models, cel rasterisers. Exact integer port of tools/view.py; see docs/render.md. */
#pragma once
#include "../platform.h"
#include "../sprites.h"
#include "model.h"

#define RENDER_F 0x12c0000                /* DAT_004438a8: focal length 300.0 (16.16) */
#define RENDER_MAX_ITEMS 2048             /* model queue (original pool 0x483f80 holds 512) */
#define RENDER_MAX_OBJS 1024
#define CAM_MAX_TRACKERS 8

struct View;

/* Packed map as the renderer sees it: DAT_0045f3c0 cells + g_CellJitter256 0x472290. */
typedef struct {
    const uint32_t *cell;                 /* 128*128 packed cell words (terrain 0-6, BNO 7-13, side 14-15, HP 25-27) */
    const int8_t (*jitter)[4];            /* 256 x {dx, dy, n, rnd} */
    /* Optional: Model_TowerHook 0x4174e0 calls ActivateTurret while a tower cell (BNO 49/50 with hit
       points) is queued. Returns 1 when it created a turret object and added it (render_add_object) to
       the view being drawn; the static tower is then skipped and the new object queued with the cell. */
    int (*tower_hook)(void *user, struct View *v, int ci);
    void *user;
} RenderMap;

/* A packed map built straight from an .RFM (LoadLevelMap 0x4322f0 + SetCellStaticObject 0x417850
   + InitCellJitterTable 0x41f190), for the viewer / tests. The game keeps its own live copy. */
typedef struct {
    uint32_t cell[128 * 128];
    int8_t jitter[256][4];
    int npads[2];
    int pad_x[2][8], pad_y[2][8];         /* home-pad cells per team (tile handler 1) */
} CellMap;
bool cellmap_load(CellMap *m, const char *rfm_rel);
RenderMap cellmap_view(const CellMap *m);

/* Camera tracker node (0x60 bytes at 0x4568c8 + view*0x60; ViewAddTracker 0x402c70). */
typedef struct CamTracker {
    bool used, is_default;
    uint8_t pri;                          /* +0x08 list order: higher first */
    uint8_t follow;                       /* +0x0b keep the last position when the object goes away */
    const int32_t *obj_pos;               /* [5] tracked object's position (obj+0x40), NULL = fixed */
    int32_t timer;                        /* [7] lifetime in ticks, 0 = forever */
    int32_t pos[3];                       /* [8..10] */
    int32_t zoff, pitch, height;          /* [0xb] [0xc] [0xd] */
    int32_t par[9];                       /* [0xf..0x17] {accel, maxaccel, gain} for pos / height / pitch */
} CamTracker;

/* VehState fields read by the vehicle draw wrappers / depth hooks (obj+0x60 -> state). */
typedef struct {
    int32_t water_anim;                   /* +0x48 splash / sink animation (sprite frame << 16) */
    uint32_t flash_tick;                  /* +0x4c hit flash until this tick (team 2 sprites) */
    int32_t elev;                         /* +0x50 cannon / launcher elevation */
    int32_t turret;                       /* +0x58 tank turret yaw, MSV launcher frame, heli rotor spin-up */
    int32_t rotor;                        /* +0x80 jeep boat blend / heli rotor angle */
    int32_t rotor_speed;                  /* +0x84 heli rotor speed */
    int32_t bank;                         /* +0x88 heli bank */
    int32_t sink_depth;                   /* def+0x158 ([0x56]) */
} RenderVeh;

/* Explosion sprite animation (Expl class graphic 0x44aa20: depth hook 0x420680, draw 0x4206b0). The
   geometry comes from the explosion descriptor: [2] graphic flags, [7..10] vertices / faces. */
typedef struct {
    int32_t frame;                        /* obj+0x64 (hi 16 = frame number) */
    int32_t scale;                        /* obj+0x6c */
    int8_t pitch;                         /* obj+0x57 */
    uint32_t gflags;                      /* desc[2] -> graphic +0x10 (0x10: on the ground) */
    int nverts;
    const int32_t (*verts)[3];
    int nfaces;
    const int32_t *faces;                 /* 6 dwords: sprite, {first, last, fade, variant}, 4 vertex indices */
} RenderExpl;

/* One debris piece (class FWall, draw 0x433a60): a single cel on 4 corners. */
typedef struct {
    int sprite;                           /* heap +0x40 */
    int32_t verts[4][3];                  /* heap +0xf8 */
    int32_t frame;                        /* obj+0x64 */
    int frames, fade;                     /* DebrisDef +4 / +8 */
} RenderDebris;

/* Dynamic object queued with the cell contents (object chain obj+0x3c/+0x40). The fields after
   `team` are what the object draw hooks (docs/render.md 5) read from the Obj; zero = unused. */
typedef struct RenderObj {
    const ModelPart *model;               /* obj+0x3c */
    int32_t pos[3];                       /* obj+0x40 world position 16.16 */
    int32_t yaw, pitch;                   /* obj+0x4c (64 steps per circle << 16), pitch for YawPitch */
    int32_t zbias;                        /* obj+0x50 */
    int team;                             /* obj+0x10 */
    int cls;                              /* class id (obj+0x14 -> +0): 1 Vehicle, 4 Shadow, 5 Storage, 6 wreck */
    uint32_t id;                          /* obj+0x08 */
    int32_t u5c, u68, u70;                /* obj+0x5c, +0x68 (lift door timer), +0x70 (heli bob / wreck pitch) */
    int8_t lift_dx, lift_dy;              /* Storage obj+0x58/+0x59: display offset (px) */
    const ModelPart *lift_model;          /* Storage obj+0x60: vehicle shown on the lift */
    const struct RenderObj *parent;       /* obj+0x2c (Shadow objects) */
    const RenderVeh *veh;                 /* obj+0x60 (Vehicle / Destroyed Vehicle) */
    uint32_t tick;                        /* g_tick (DAT_0045f390) */
    int32_t *stamp;                       /* obj+0x64: the body hooks store the tick when drawn */
    const RenderExpl *expl;               /* Expl (class 11) */
    const RenderDebris *debris;           /* FWall (class 3) */
    int stay;                             /* Stay (class 17): `model` is the wrapped graphic (0x435e40) */
    int inactive;                         /* obj flags 0x20 (settled debris) */
    int32_t *drawn;                       /* debris heap +4 / Stay obj+0x68: tick of the last draw */
    int32_t expire, linger;               /* Stay obj+0x64 / +0x6c */
    uint8_t *draw_flags;                  /* set by the draw: bit0 destroy the object, bit1 LRU touch (Stay) */
    int32_t *unseen;                      /* Turret Gun (class 2) +0x62: zeroed by Model_TowerHook when drawn */
    int32_t roll, gun_pitch;              /* Drone: obj+0x58 roll, record+0x1c gun pitch (draw 0x435090) */
    int32_t u64;                          /* obj+0x64: MAN animation frame, Gate opening (wrappers 0x4175a0..) */
} RenderObj;

/* Pseudo parts for graphics without model data: the explosion sprite graphic 0x44aa20 and the debris
   graphic 0x454f18. The explosion parts 0x452dc8 / 0x452e0c continue with render_part_expl. */
extern const ModelPart render_part_expl, render_part_debris;

typedef struct {
    const ModelPart *m;
    int32_t pos[3];                       /* view space (x, y, z - H) */
    int32_t yaw, pitch, key;
    int team, ci;                         /* ci: cell index or -1 */
    int is_obj;                           /* queued from a RenderObj (not the cell's BNO) */
    const RenderObj *obj;                 /* +0x2c */
    int32_t x24, x30;                     /* +0x24 extra yaw (rotor angle), +0x30 roll (Model_DrawTurret) */
    uint8_t stay;                         /* queued for a Stay object (draw 0x435e40 wraps the part) */
} RenderItem;

/* The render-relevant part of the 0x24c-byte view struct (0x48bd90 + i*0x24c). */
typedef struct View {
    int x, y, w, h;                       /* clip origin, [2] [3] size (logical 320x240 coords) */
    int32_t cx, cy;                       /* [4] [5] screen centre (16.16) */
    int32_t camx, camy;                   /* [6] [7] */
    int32_t H;                            /* [8] +0x20 camera height offset */
    int32_t pitch;                        /* [9] +0x24 */
    int32_t m10, m11;                     /* [10] -cos p, [11] sin p */
    int32_t left, top;                    /* [12] [13] integer px */
    int32_t mat[9];                       /* +0x3c camera matrix */
    int32_t cache_pitch;                  /* +0x104 */
    /* Camera_Update state */
    int32_t sx, sy;                       /* +0x10c/+0x110 smoothed target */
    int32_t sH;                           /* +0x114 smoothed height */
    int32_t tz;                           /* +0x128 target z (+ zoff) */
    int32_t tx, ty;                       /* +0x12c/+0x130 target */
    int32_t tH;                           /* +0x134 */
    int32_t vx, vy, vH, vpitch;           /* +0x138/+0x13c/+0x140/+0x148 velocities */
    int32_t tpitch;                       /* +0x144 */
    int32_t win_x, win_y;                 /* +0x1c4/+0x1c8 screen-window offset (16.16) */
    CamTracker trk[CAM_MAX_TRACKERS];     /* +0x158 list; trk[0] is the view's default node */
    /* model queue (View_QueueModel 0x4085a0) */
    RenderItem *items;
    int nitems, ncels;
    const RenderObj *objs[RENDER_MAX_OBJS];
    int nobjs;
    int team;                             /* +0x04 the view's side */
    int enemy_turrets;                    /* DAT_00471458[team]: enemy Turret Gun objects queued this frame */
} View;

/* InitProjectionAndRotTables 0x408440 + cel tables; call once. */
void render_init(const SpriteBank *sb);

/* ViewInit 0x403750 (+ Camera_SetScreenWindow 0x403550 with the 1P window (0,12)). */
void view_init(View *v, int x, int y, int w, int h);
void view_free(View *v);
/* Camera_SetScreenWindow 0x403550: screen-window offset (x, y) in px (2P: (8,12) / (-8,12)). */
void view_set_window(View *v, int x, int y);
/* Row i (0..63) of the yaw matrix table 0x481780 (after render_init). */
const int32_t *render_yaw_matrix(int i);
/* Set pitch/H and recompute matrix + top/left (the cache-refresh block of Camera_Update). */
void view_set_pitch_height(View *v, int32_t pitch, int32_t H);
/* Place the camera directly (camx/camy = world point that projects to the view centre). */
void view_set_camera(View *v, int32_t camx, int32_t camy, int32_t pitch, int32_t H);
/* Steady state of Camera_Update for a target (tx,ty,tz)+zoff: what view.py's look_at does. */
void view_look_at(View *v, int32_t tx, int32_t ty, int32_t tz, int32_t zoff);

/* Trackers (ViewAddTracker 0x402c70). pri 0 means 0x80. obj_pos may be NULL (fixed point at pos). */
CamTracker *camera_add_tracker(View *v, int pri, const int32_t *obj_pos, const int32_t *pos,
                               int32_t zoff, int32_t pitch, int32_t height, bool follow, const int32_t *par9);
void camera_remove_tracker(View *v, CamTracker *t);
/* FUN_00402b90: snap smoothed + target state to pos/H/pitch. */
void camera_snap(View *v, const int32_t *pos, int32_t H, int32_t pitch);
/* Camera_Update 0x403a40; dt = DAT_0045f3a8 (frame ticks). */
void camera_update(View *v, int32_t dt);

/* Dynamic objects for the next render_world (pointers must stay valid until then). */
void render_clear_objects(View *v);
void render_add_object(View *v, const RenderObj *o);
/* The heli's ground-shadow object (class 4 "Shadow" created by the heli init 0x42b5e0 with model
   0x44f1d0, placed by ShadowUpdate 0x434d70): fills *sh from the heli object. */
void render_heli_shadow(RenderObj *sh, const RenderObj *heli);

/* RenderWorldView 0x419dd0. scale 1: logical 320x240 framebuffer; scale 2: 640x480 hi-res
   (corners >>15, clip rect doubled). */
void render_world(View *v, const RenderMap *map, Framebuffer *fb, int scale);

/* Cel primitives (explicit corners, 16.16 screen coords relative to the clip origin). */
typedef struct { int x0, y0, x1, y1; } ClipRect;
void cel_draw(Framebuffer *fb, const ClipRect *clip, int scale, int sprite, const int32_t corners[8], int mode);
/* Patched-CCB variant used by src/render/ccb.c: corners in absolute screen pixels. */
void cel_draw_px(Framebuffer *fb, const ClipRect *clip, const Sprite *s, int idx, const int p[8], int mode,
                 const uint8_t *remap);

/* fixed-point helpers (FixMul 0x438300, FixDiv 0x438390, CosFixed/SinFixed 0x438360/0x438520) */
static inline int32_t fixmul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }
int32_t fixdiv(int32_t a, int32_t b);
int32_t cosfixed(int32_t a);
int32_t sinfixed(int32_t a);
