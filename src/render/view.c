/* World view: projection/rotation tables, camera (ViewInit / Camera_Update), the far->near floor
   walk of RenderWorldView 0x419dd0, the model queue + depth sort (View_QueueModel 0x4085a0) and
   the model draw functions. Integer port of tools/view.py; see docs/render.md. */
#include "render.h"
#include "../exe.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void cel_init(const SpriteBank *b);

#define F RENDER_F

/* ---- fixed point ---- */
int32_t fixdiv(int32_t a, int32_t b)   /* FixDiv 0x438390: x87 (a/65536)/(b/65536)*65536, _ftol */
{
    if (b == 0) return 0;
    double r = (a * (1.0 / 65536)) / (b * (1.0 / 65536)) * 65536.0;
    return (int32_t)(uint32_t)(int64_t)r;
}
int32_t cosfixed(int32_t a) { return (int32_t)(cos(a * (1.0 / 65536) * (M_PI / 128)) * 65536.0); }  /* 0x438360 */
int32_t sinfixed(int32_t a) { return (int32_t)(sin(a * (1.0 / 65536) * (M_PI / 128)) * 65536.0); }  /* 0x438520 */
static int32_t isqrt(int64_t x) { return (int32_t)sqrt((double)x); }                      /* ISqrt 0x438550 */
static int32_t fixsqrt(int32_t x) { return (int32_t)(sqrt(x * (1.0 / 65536)) * 65536.0); }   /* FixSqrt 0x438580 */

/* PTR_DAT_00440c30[i] = F / (300 - i), i = -1024..511 (0 at i = 300). Computed for any i like view.py. */
static inline int32_t ptab(int32_t i) { int32_t d = 300 - i; return d == 0 ? 0 : F / d; }

static void mat_mul(int32_t *o, const int32_t *a, const int32_t *b)   /* Mat_Mul3x3 0x438460 */
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            o[r * 3 + c] = fixmul(a[r * 3], b[c]) + fixmul(a[r * 3 + 1], b[3 + c]) + fixmul(a[r * 3 + 2], b[6 + c]);
}
static void vec_mul(int32_t *o, const int32_t *v, const int32_t *m)   /* VecMulMat3 0x42bb00 */
{
    int32_t x = v[0], y = v[1], z = v[2];
    for (int c = 0; c < 3; c++) o[c] = fixmul(x, m[c]) + fixmul(y, m[3 + c]) + fixmul(z, m[6 + c]);
}

/* 64-step yaw (0x481780) and pitch (0x48a7b0) tables (InitProjectionAndRotTables 0x408440). */
static int32_t YAW[64][9], PITCH[64][9];
static const SpriteBank *g_sb;

void render_init(const SpriteBank *sb)
{
    g_sb = sb;
    cel_init(sb);
    for (int i = 0; i < 64; i++) {
        int32_t c = cosfixed(i * 0x40000), s = sinfixed(i * 0x40000);
        int32_t y[9] = { c, s, 0, -s, c, 0, 0, 0, 0x10000 }, p[9] = { 0x10000, 0, 0, 0, c, s, 0, -s, c };
        memcpy(YAW[i], y, sizeof y);
        memcpy(PITCH[i], p, sizeof p);
    }
}

/* ---- view / camera ---- */

/* The cache block repeated in ViewInit / Camera_SetScreenWindow / Camera_Update:
   matrix from pitch (when +0x104 != pitch), then [0xd] top and [0xc] left. */
static void view_refresh(View *v)
{
    if (v->cache_pitch != v->pitch) {
        int32_t c = cosfixed(v->pitch), s = sinfixed(v->pitch);
        v->m10 = -c;
        v->m11 = s;
        int32_t m[9] = { 0x10000, 0, 0, 0, s, c, 0, -c, s };
        memcpy(v->mat, m, sizeof m);
        v->cache_pitch = v->pitch;
    }
    int32_t hh = -v->h >> 1, top = 0;
    if (hh != 0) top = fixdiv(v->H + F, fixmul(F, v->m11) / hh - v->m10);
    int32_t inv = fixdiv(v->H + fixmul(v->m10, top) + F, F);
    v->top = top >> 16;
    v->left = (inv * (-v->w >> 1)) >> 16;
}

static int32_t tracker_par_default[9]; /* CamTracker template 0x43eea8 [0xf..0x17] */

void view_init(View *v, int x, int y, int w, int h)
{
    RenderItem *items = v->items;
    memset(v, 0, sizeof *v);
    v->items = items ? items : malloc(sizeof(RenderItem) * RENDER_MAX_ITEMS);
    v->x = x; v->y = y; v->w = w; v->h = h;
    v->cx = (w / 2) << 16;
    v->cy = (h / 2) << 16;
    v->H = 0x10e0000;            /* [8] 270 */
    v->pitch = 0x200000;         /* [9] 45 degrees */
    v->sH = 0xf00000;            /* +0x114 */
    v->tH = -0xaa0000;           /* +0x134 */
    v->tpitch = 0x200000;        /* +0x144 */
    v->tz = 0;                   /* +0x128 */
    v->cache_pitch = 0x7fffffff;
    /* the view's own default node (+0x164), CamTracker_Default 0x402bf0, priority 0 */
    v->trk[0].used = v->trk[0].is_default = true;
    memcpy(v->trk[0].par, tracker_par_default, sizeof tracker_par_default);
    /* Camera_SetScreenWindow 0x403550 with the 1P window (0,12) 288x140 */
    v->win_x = 0;
    v->win_y = 12 << 16;
    view_refresh(v);
}

void view_free(View *v) { free(v->items); v->items = NULL; }

void view_set_window(View *v, int x, int y)
{
    v->win_x = x << 16;
    v->win_y = y << 16;
}

const int32_t *render_yaw_matrix(int i) { return YAW[i & 63]; }

void view_set_pitch_height(View *v, int32_t pitch, int32_t H)
{
    v->pitch = pitch;
    v->H = H;
    view_refresh(v);
}

void view_set_camera(View *v, int32_t camx, int32_t camy, int32_t pitch, int32_t H)
{
    view_set_pitch_height(v, pitch, H);
    v->camx = camx;
    v->camy = camy;
}

void view_look_at(View *v, int32_t tx, int32_t ty, int32_t tz, int32_t zoff)
{
    int32_t t128 = tz + zoff;
    v->camx = tx - v->win_x;
    v->camy = (t128 >> 16) * v->m10 + ty - v->win_y;
}

CamTracker *camera_add_tracker(View *v, int pri, const int32_t *obj_pos, const int32_t *pos,
                               int32_t zoff, int32_t pitch, int32_t height, bool follow, const int32_t *par9)
{
    for (int i = 1; i < CAM_MAX_TRACKERS; i++) {
        CamTracker *t = &v->trk[i];
        if (t->used) continue;
        memset(t, 0, sizeof *t);
        t->used = true;
        t->pri = pri ? (uint8_t)pri : 0x80;
        t->follow = follow;
        t->obj_pos = obj_pos;
        if (obj_pos) memcpy(t->pos, obj_pos, sizeof t->pos);
        else if (pos) memcpy(t->pos, pos, sizeof t->pos);
        t->zoff = zoff; t->pitch = pitch; t->height = height;
        memcpy(t->par, par9 ? par9 : tracker_par_default, sizeof t->par);
        return t;
    }
    return NULL;
}

void camera_remove_tracker(View *v, CamTracker *t) { if (t && !t->is_default) t->used = false; }

void camera_snap(View *v, const int32_t *pos, int32_t H, int32_t pitch)   /* FUN_00402b90 */
{
    v->sx = v->tx = pos[0];
    v->sy = v->ty = pos[1];
    v->sH = v->tH = H;
    v->tz = pos[2];
    v->tpitch = v->pitch = pitch;
}

static int32_t approach(int32_t cur, int32_t target, int32_t step)   /* ApproachValue 0x41ed80 */
{
    if (cur < target) { cur += step; if (target < cur) return target; }
    else if (target < cur) { cur -= step; if (cur < target) return target; }
    return cur;
}

/* Camera_StepAxis 0x403ec0 */
static void step_axis(int32_t *cur, int32_t target, int32_t *vel, const int32_t *par, int32_t dt)
{
    int32_t c = *cur, accel = par[0];
    if (((uint32_t)(c ^ target) & 0xffff0000u) == 0) { *vel = 0; return; }
    int32_t d = target - c;
    bool neg = d < 0;
    if (neg) d = -d;
    int32_t want = par[2];
    if (want != 0) {
        int32_t s = fixsqrt(fixmul(d, par[1]));
        if (s < want) { accel = par[1]; want = s; }
        if (neg) want = -want;
    }
    int32_t nv = approach(*vel, want, accel * dt);
    c += nv * dt;
    if (!neg) { if (c > target) c = target; }
    else if (c < target) c = target;
    *cur = c;
    *vel = nv;
}

/* CamTracker_Update 0x402d90 / CamTracker_Default 0x402bf0. Returns 1 when the node wins. */
static int tracker_update(View *v, CamTracker *t, const int32_t **pp, const int32_t **ph,
                          const int32_t **pa, int32_t dt)
{
    if (t->is_default) {
        if (!*pp) *pp = t->par;
        if (!*ph) { v->tH = 0; *ph = t->par + 3; }
        if (!*pa) { v->tpitch = 0x200000; *pa = t->par + 6; }
        return 1;
    }
    if (t->timer != 0 && (t->timer -= dt) < 1) { t->used = false; return 0; }
    const int32_t *p = t->pos;
    if (t->obj_pos) {
        if (t->follow) memcpy(t->pos, t->obj_pos, sizeof t->pos);
        p = t->obj_pos;
    }
    if (!*pp) { *pp = t->par; v->tx = p[0]; v->ty = p[1]; v->tz = p[2] + t->zoff; }
    if (!*ph) { *ph = t->par + 3; v->tH = t->height; }
    if (!*pa) {
        if (t->pitch == 0) return 0;
        *pa = t->par + 6;
        v->tpitch = t->pitch;
    }
    return 1;
}

/* One position axis of Camera_Update (0x403b29..0x403cb4). d = (smoothed - target) >> 16. */
static void follow_axis(int32_t *pos, int32_t target, int32_t *vel, int32_t d, int32_t dist,
                        const int32_t *par, int32_t dt)
{
    int32_t cap = fixsqrt((d < 0 ? -d : d) * par[1]);
    int32_t want = -fixmul((d << 16) / dist, par[2]), accel = par[0];
    if (want < 0) { if (-cap > want) { want = -cap; accel = par[1]; } }
    else if (cap < want) { want = cap; accel = par[1]; }
    *vel = approach(*vel, want, dt * accel);
    int32_t np = *vel * dt + *pos;
    *pos = np;
    if (d < 1) { if (target < np) *pos = target; }
    else if (np < target) *pos = target;
}

void camera_update(View *v, int32_t dt)
{
    const int32_t *pp = NULL, *ph = NULL, *pa = NULL;
    /* walk the list in priority order (higher first; the default node, priority 0, last) */
    bool done[CAM_MAX_TRACKERS] = { 0 };
    for (;;) {
        int best = -1;
        for (int i = 0; i < CAM_MAX_TRACKERS; i++)
            if (v->trk[i].used && !done[i] && (best < 0 || v->trk[i].pri > v->trk[best].pri ||
                (v->trk[i].pri == v->trk[best].pri && v->trk[best].is_default)))
                best = i;
        if (best < 0) break;
        done[best] = true;
        if (tracker_update(v, &v->trk[best], &pp, &ph, &pa, dt)) break;
    }
    if (!pp || !ph || !pa) return;
    step_axis(&v->pitch, v->tpitch, &v->vpitch, pa, dt);
    bool pitch_changed = v->cache_pitch != v->pitch;
    if (pitch_changed) view_refresh(v);
    int32_t dx = (v->sx - v->tx) >> 16;
    v->ty += (v->tz >> 16) * v->m10;
    int32_t dy = (v->sy - v->ty) >> 16;
    int32_t dist = isqrt((int64_t)dx * dx + (int64_t)dy * dy);
    if (dist >= 1) {
        follow_axis(&v->sx, v->tx, &v->vx, dx, dist, pp, dt);
        follow_axis(&v->sy, v->ty, &v->vy, dy, dist, pp, dt);
    }
    step_axis(&v->sH, v->tH, &v->vH, ph, dt);
    bool h_changed = v->H != v->sH;
    v->H = v->sH;
    v->camx = v->sx - v->win_x;
    v->camy = v->sy - v->win_y;
    if (h_changed || pitch_changed) view_refresh(v);
}

/* ---- dynamic objects ---- */
void render_clear_objects(View *v) { v->nobjs = 0; }
void render_add_object(View *v, const RenderObj *o) { if (v->nobjs < RENDER_MAX_OBJS) v->objs[v->nobjs++] = o; }

void render_heli_shadow(RenderObj *sh, const RenderObj *heli)
{
    int32_t z = heli->pos[2];
    memset(sh, 0, sizeof *sh);
    sh->model = &model_parts[MP_44f1d0];
    sh->pos[0] = heli->pos[0] + (z >> 8) * 0x55;              /* ~z/3 east */
    sh->pos[1] = heli->pos[1] - ((int32_t)((uint32_t)z & 0xffffff01u) >> 1);   /* z/2 north */
    sh->pos[2] = 0;
    sh->yaw = heli->yaw;
    sh->zbias = z - 0x10000;
    sh->team = heli->team;
    sh->cls = 4;
    sh->parent = heli;
    sh->tick = heli->tick;
}

/* ---- model queue (View_QueueModel 0x4085a0) ---- */
static const RenderMap *g_map;
static View *g_view;                                     /* DAT_00480f48: the view being drawn */
static int16_t obj_head[128 * 128], obj_next[RENDER_MAX_OBJS];

static inline const int8_t *jit_by_cell(int ci)   /* CellJitterEntry 0x417820 */
{
    return g_map->jitter[((ci & 0x1e0) * 2 + (ci & 0xf) * 4) / 4];
}

static void insert_item(View *v, const RenderItem *it)
{
    if (v->nitems >= RENDER_MAX_ITEMS) return;
    /* walk from the tail while new.key > node.key; insert after the stopping node */
    int k = v->nitems;
    while (k > 0 && it->key > v->items[k - 1].key) k--;
    memmove(&v->items[k + 1], &v->items[k], sizeof(RenderItem) * (size_t)(v->nitems - k));
    v->items[k] = *it;
    v->nitems++;
}

/* depth hook for static cell parts; returns zbias, *skip = hidden (DAT_0045f398 < 0) */
static int32_t depth_hook(const ModelPart *m, RenderItem *it, uint32_t cell, int ci, bool *skip)
{
    int team = (cell & 0xc000) >> 14;
    *skip = false;
    switch (m->depth_hook) {
    case MODEL_HOOK_TOWER: {                 /* Model_TowerHook 0x4174e0 (no live turret object) */
        if ((cell & 0xe000000) == 0) { *skip = true; return 0; }
        it->team = team;
        if (g_map->tower_hook && g_view) {   /* enemy vehicle in this view: ActivateTurret 0x423c10 */
            int before = g_view->nobjs;
            if (g_map->tower_hook(g_map->user, g_view, ci)) {
                *skip = true;                /* DAT_0045f398 = -1 */
                for (int k = before; k < g_view->nobjs; k++) {   /* ObjLinkToCell: head of the chain */
                    obj_next[k] = obj_head[ci];
                    obj_head[ci] = (int16_t)k;
                }
            }
        }
        const int8_t *j = jit_by_cell(ci);
        it->yaw = ((uint8_t)j[3] & 0x3f) << 16;
        it->pitch = (int32_t)(uint8_t)j[0] << 14;
        return m->zbias;
    }
    case MODEL_HOOK_CELL:                    /* Model_CellHook 0x41f380 */
        it->team = team;
        return m->zbias;
    case MODEL_HOOK_HIDE:                    /* Model_HookHide 0x4174a0 */
        *skip = true;
        return 0;
    case MODEL_HOOK_DEST_PLANTER:            /* 0x41f4f0: hidden when side bits == 1 */
        if ((cell & 0xc000) == 0x4000) { *skip = true; return 0; }
        break;
    }
    it->team = team;
    return m->zbias;
}

/* Depth hooks of object parts (def+0x24 called as hook(item, cell, obj)). Returns the z-bias;
   *now = DAT_0045f398: 0 sorted insert, > 0 draw immediately, < 0 drop. */
static int8_t bubble_frames[16]; /* 0x44c5e8 */

static int flash_team(const RenderObj *o)      /* hit flash: team 2 while state+0x4c >= tick (not wrecks) */
{
    return (o->cls != 6 && o->veh && o->veh->flash_tick >= o->tick) ? 2 : o->team;
}

static int32_t obj_hook(const ModelPart *m, RenderItem *it, int ci, int *now)
{
    const RenderObj *o = it->obj;
    const RenderVeh *s = o->veh;
    int32_t gz = (o->model ? o->model->zbias : 0) + o->zbias;    /* obj->gfx +0x20 + obj+0x50 */
    int32_t dz = m->zbias + o->zbias;                            /* item def +0x20 + obj+0x50 */
    *now = 0;
    it->pitch = 0;
    it->yaw = o->yaw;
    switch (m->depth_hook) {
    case MODEL_HOOK_SPLASH:                   /* 0x42ef80: splash frame */
        it->team = s ? s->water_anim >> 16 : 0;
        return gz;
    case MODEL_HOOK_SINK: {                   /* 0x42efc0: sinking frame = depth in px (+ sink depth for team 1) */
        int32_t sd = s ? s->sink_depth >> 16 : 0, d = -(o->pos[2] >> 16);
        if (sd <= d || d < 0) *now = -1;
        it->team = d + (o->team == 0 ? 0 : sd);
        return gz;
    }
    case MODEL_HOOK_BUBBLES:                  /* 0x42f0e0 */
        it->team = bubble_frames[(o->tick & 0x1e) >> 1];
        return gz;
    case MODEL_HOOK_WRECK:                    /* 0x42f120 */
        if (o->stamp) *o->stamp = (int32_t)o->tick;
        it->pitch = o->u5c;
        it->team = o->team;
        return dz;
    case MODEL_HOOK_VEH_BODY:                 /* 0x42f160 */
        if (o->stamp) *o->stamp = (int32_t)o->tick;
        it->team = flash_team(o);
        return dz;
    case MODEL_HOOK_WRECK_BURNT:              /* 0x42f1c0: burnt sprites (team + 3) */
        if (o->stamp) *o->stamp = (int32_t)o->tick;
        it->pitch = o->u70;
        it->team = o->team + 3;
        return dz;
    case MODEL_HOOK_JEEP_SPLASH: {            /* 0x42f5c0: boat wake (part 0x44e110) once the tyres are in */
        int32_t w = s ? s->water_anim : 0;
        if (o->cls == 1 && s && s->rotor != 0) {
            it->m = &model_parts[MP_44e110];
            if (w > 0x40000) {
                if (w < 0x80000) w = fixmul(w - 0x40000, 0x18000) + 0x40000;
                else {
                    w = fixmul(w - 0x80000, 0x19999) + 0xa0000;
                    if (w > 0x11ffff) w = 0x110000;
                }
            }
        }
        it->team = w >> 16;
        return gz;
    }
    case MODEL_HOOK_HELI:                     /* 0x42f900: bob pitch (obj+0x70) and bank roll */
        if (o->stamp) *o->stamp = (int32_t)o->tick;
        it->team = flash_team(o);
        if (o->cls == 1) {
            it->pitch = o->u70 & 0x3fffff;
            it->x30 = (s ? s->bank : 0) & 0x3fffff;
        } else it->x30 = 0;
        return dz;
    case MODEL_HOOK_HELI_ROTOR:               /* 0x42f790: blade set by rotor speed, 4 = stopped (two blades) */
        if (o->cls == 1 && s) {
            it->pitch = o->u70 & 0x3fffff;
            it->x30 = s->bank & 0x3fffff;
            it->x24 = s->rotor;
            if (s->turret < 0x10000) it->team = 4;
            else {
                int k = (s->rotor_speed >> 16) - 1;
                it->team = k > 3 ? 3 : k < 0 ? 0 : k;
            }
        } else {
            it->x30 = it->x24 = 0;
            it->team = 0;
            it->yaw = (int32_t)(((o->id + o->tick) & 0xf) << 18);
        }
        return dz;
    case MODEL_HOOK_ROTOR_SHADOW: {           /* 0x42fa50: rotor shadow of the heli shadow object */
        const RenderObj *p = o->parent;
        if (!p) { *now = -1; return 0; }
        it->obj = p;
        it->x30 = 0;
        if (p->cls == 1 && p->veh) {
            it->yaw = p->veh->rotor;
            if (p->veh->turret < 0x10000) it->team = 4;
            else {
                int k = (p->veh->rotor_speed >> 16) - 1;
                it->team = k >= 3 ? 2 : k < 0 ? 0 : k;
            }
        } else {
            it->team = 0;
            it->yaw = (int32_t)(((p->id + p->tick) & 0xf) << 18);
        }
        return p->zbias + m->zbias;
    }
    case MODEL_HOOK_STORAGE: {                /* 0x417d20: lift drawn at once while the cell is the lift hole */
        uint32_t cell = ci >= 0 ? g_map->cell[ci] : 0;
        *now = (cell & 2) ? -1 : 1;
        it->team = o->team;
        return gz;
    }
    case MODEL_HOOK_PROJ:                     /* 0x41f800: projectiles, pitch obj+0x60, frame obj+0x71 */
        it->pitch = o->pitch;
        it->team = o->u70;
        return gz;
    case MODEL_HOOK_GRENADE:                  /* 0x41f830: tumble frame (+12 for team 1) */
        it->team = o->u70;
        return gz;
    case MODEL_HOOK_EXPL:                     /* 0x420680: explosion sprites (graphic flags from desc[2]) */
        it->team = 0;
        return o->zbias;
    case MODEL_HOOK_TOWER:                    /* 0x4174e0 with a Turret Gun: live yaw/pitch, unseen = 0 */
        if (o->unseen) *o->unseen = 0;
        it->pitch = o->pitch;
        it->team = o->team;
        return m->zbias + o->zbias;
    case MODEL_HOOK_STAY:                     /* 0x435e10: remains, pitch obj+0x5c */
        it->pitch = o->pitch;
        it->team = o->team;
        return m->zbias + o->zbias;
    default:                                  /* hooks not ported for objects: plain object path */
        it->pitch = o->pitch;
        it->team = o->team;
        return o->zbias + m->zbias;
    }
}

static void draw_item(View *v, RenderItem *it);

/* Explosion sprite graphic 0x44aa20 and debris graphic 0x454f18 (no model data). */
const ModelPart render_part_expl = { 0x44aa20, MODEL_FN_EXPL, NULL, 0, 0, 0, { 0, 0, 0 }, 0, MODEL_HOOK_EXPL, 0,
                                     0, NULL, 0, 0, NULL, { NULL }, 0 };
const ModelPart render_part_debris = { 0x454f18, MODEL_FN_DEBRIS, NULL, 0, 0, 0, { 0, 0, 0 }, 0, 0, 0,
                                       0, NULL, 0, 0, NULL, { NULL }, 0 };

static void queue_model(View *v, const ModelPart *m, const int32_t *pos, int ci, const RenderObj *obj)
{
    for (; m; m = m->next ? m->next
                          : (obj && obj->expl && (m->addr == 0x452dc8 || m->addr == 0x452e0c) ? &render_part_expl : NULL)) {
        if (m->special) return;
        int32_t p[3] = { pos[0], pos[1], pos[2] };
        if (m->pos_hook == MODEL_HOOK_POS_JITTER) {   /* Model_PosJitter 0x41f210 */
            const int8_t *j = g_map->jitter[((p[0] >> 21) & 15) + ((p[1] >> 21) & 15) * 16];
            p[0] += j[0] << 16;
            p[1] += j[1] << 16;
        }
        RenderItem it = { 0 };
        it.m = m;
        it.ci = ci;
        it.pos[0] = m->off[0] - v->camx + p[0];
        it.pos[1] = p[1] - v->camy + m->off[1];
        bool skip = false;
        int now = 0;
        int32_t zb;
        uint32_t mflags = m->flags;
        if (obj && obj->stay) {                    /* the Stay graphic 0x4557f0: no offset, hook 0x435e10 */
            it.pos[0] = p[0] - v->camx;
            it.pos[1] = p[1] - v->camy;
            it.stay = 1;
            mflags = 0;
        }
        if (m == &render_part_expl) mflags = obj->expl->gflags;   /* patched into the graphic by 0x420680 */
        if (obj) {
            it.is_obj = 1;
            it.obj = obj;
            if (obj->stay) {
                ModelPart hm = *m;
                hm.depth_hook = MODEL_HOOK_STAY;
                zb = v->H - obj_hook(&hm, &it, ci, &now);
            } else if (m->depth_hook) {
                zb = v->H - obj_hook(m, &it, ci, &now);
            } else {
                zb = v->H - obj->zbias - m->zbias;
                it.yaw = obj->yaw;
                it.pitch = obj->pitch;
                it.team = obj->team;
            }
        } else if (m->depth_hook == 0) {
            zb = v->H - m->zbias;
            it.team = (g_map->cell[ci] & 0xc000) >> 14;
        } else {
            zb = v->H - depth_hook(m, &it, g_map->cell[ci], ci, &skip);
        }
        it.pos[2] = (mflags & 0x10) ? 0 : p[2] + (it.stay ? 0 : m->off[2]);
        int32_t t[3];
        vec_mul(t, it.pos, v->mat);
        memcpy(it.pos, t, sizeof t);
        int32_t ax = it.pos[0] < 0 ? -it.pos[0] : it.pos[0];
        it.key = (int32_t)((uint32_t)(it.pos[1] >> 8) - (uint32_t)it.pos[2] + (uint32_t)zb + (uint32_t)(ax >> 9));
        it.pos[2] -= v->H;
        if (skip || now < 0) continue;
        if (now > 0) draw_item(v, &it);
        else insert_item(v, &it);
        if (it.stay) return;                        /* one part: the wrapped graphic's own draw function */
    }
}

/* View_QueueCellContents 0x419cf0: the static BNO model at the cell centre, then live objects. */
static void queue_cell(View *v, int ci, int x, int y)
{
    if (ci < 0) return;
    uint32_t cell = g_map->cell[ci];
    int t = (cell & 0x3f80) >> 7;
    if (t && render_bno[t].model) {
        int32_t pos[3] = { (x + 16) << 16, (y + 16) << 16, 0 };
        queue_model(v, render_bno[t].model, pos, ci, NULL);
    }
    for (int k = obj_head[ci]; k >= 0; k = obj_next[k]) {
        const RenderObj *o = v->objs[k];
        if (o->cls == 2 && o->team != v->team) v->enemy_turrets++;   /* DAT_00471458 */
        queue_model(v, o->model, o->pos, ci, o);
    }
}

/* ---- model draw (Model_Draw* + Model_EmitFaces 0x4088a0) ---- */
static Framebuffer *g_fb;
static ClipRect g_clip;
static int g_scale;

/* Every cel goes through here; the last one (DAT_0048ca8c) is what Model_DrawStoragePad saves
   (copy at 0x4571c8) and Model_DrawStorage adds again after the lift doors. */
typedef struct { bool valid; int idx, mode; int32_t c[8]; } SavedCel;
static SavedCel g_last_cel, g_pad_cel;

static void put_cel(View *v, int idx, const int32_t c[8], int mode)
{
    cel_draw(g_fb, &g_clip, g_scale, idx, c, mode);
    g_last_cel.valid = true;
    g_last_cel.idx = idx;
    g_last_cel.mode = mode;
    memcpy(g_last_cel.c, c, sizeof g_last_cel.c);
    v->ncels++;
}

/* Jeep boat parts: 0x44dbf0 = verts of 0x44e110 and verts 44..47 of 0x44dfb0, the source 0x44dc38
   scaled in x/y by max(boat, 0x4000); cached by the scale (DAT_0044dc80) like the original. */
static int32_t jeep_part_src[6][3];      /* 0x44dc38 */
static int32_t jeep_part_v[6][3];        /* 0x44dbf0 (rewritten by jeep_scale) */
static int32_t jeep_part_scale = -1;

static void jeep_scale(int32_t b)
{
    if (b == jeep_part_scale) return;
    int32_t m[9] = { b, 0, 0, 0, b, 0, 0, 0, 0x10000 };     /* 0x481750 */
    for (int i = 0; i < 6; i++) vec_mul(jeep_part_v[i], jeep_part_src[i], m);
    jeep_part_scale = b;
}

/* Parts whose vertex data the wrappers rewrite in place in the original. */
static const int32_t (*part_verts(const ModelPart *m))[3]
{
    static int32_t boat[48][3];
    if (m == &model_parts[MP_44e110]) return (const int32_t (*)[3])jeep_part_v;
    if (m == &model_parts[MP_44dfb0] && m->nverts == 48) {
        memcpy(boat, m->verts, sizeof boat);
        memcpy(boat[44], jeep_part_v, sizeof(int32_t) * 3 * 4);
        return (const int32_t (*)[3])boat;
    }
    return m->verts;
}

/* ProjectVertices 0x401290: false (nothing drawn) when a vertex is behind the near limit. */
static bool project_verts(const View *v, const RenderItem *it, const int32_t (*tv)[3], int n, int32_t *X, int32_t *Y)
{
    for (int i = 0; i < n; i++) {
        int32_t iz = (tv[i][2] + it->pos[2]) >> 16;
        if (iz > 0x1ff) return false;
        int32_t s = ptab(iz);
        int64_t ax = (int64_t)((tv[i][0] + it->pos[0]) >> 14) * s;
        int64_t ay = (int64_t)((tv[i][1] + it->pos[1]) >> 14) * s;
        X[i] = (int32_t)(((int32_t)((uint32_t)ax & 0xfffe0003u) >> 2) + (uint32_t)v->cx);
        Y[i] = (int32_t)(((int32_t)((uint32_t)ay & 0xfffe0003u) >> 2) + (uint32_t)v->cy);
    }
    return true;
}

static void emit_faces(View *v, const RenderItem *it, const int32_t (*tv)[3], const ModelFace *faces)
{
    const ModelPart *m = it->m;
    int32_t X[64], Y[64];
    if (m->nverts > 64) return;
    if (!project_verts(v, it, tv, m->nverts, X, Y)) return;
    const int8_t *order = NULL;
    if (m->nfaces_field < 1) {
        order = m->orders[0] ? m->orders[(((uint32_t)it->yaw & 0xfff9ffffu) >> 19) & 7] : NULL;
        if (!order) return;
    }
    for (int k = 0;; k++) {
        const ModelFace *f;
        if (order) {
            if (order[k] < 0) break;
            f = &faces[order[k]];
        } else {
            if (k >= m->nfaces) break;
            f = &faces[k];
            if ((f->flags & 1) && !(X[f->ta] <= X[f->tb])) continue;
            if ((f->flags & 2) && !(Y[f->ta] <= Y[f->tb])) continue;
        }
        int idx = f->sprite + ((f->flags & 8) ? it->team : 0);
        int32_t c[8];
        for (int j = 0; j < 4; j++) { c[j * 2] = X[f->c[j]]; c[j * 2 + 1] = Y[f->c[j]]; }
        put_cel(v, idx, c, -1);
    }
}

/* Model_DrawBuilding 0x41f550: roof variant and wall sprites from the cell jitter + neighbours. */
static int32_t bldg_walls[6]; /* g_BldgWallSprites 0x4478d8 */
static int32_t fact_walls[6]; /* g_FactWallSprites 0x4478f0 */
static int8_t roof_var[8]; /* 0x4478d0 */

static bool is_rubble(int nci)
{
    if (nci < 0) nci += 128 * 128;           /* view.py: cells[-1] */
    if (nci >= 128 * 128) return false;
    return (g_map->cell[nci] & 0x3f80) == 0x1980;   /* BNO 51 BNO_STD_DEST */
}

static void patch_building(const RenderItem *it, ModelFace *faces)
{
    const ModelPart *m = it->m;
    int ci = it->ci;
    const int8_t *j = jit_by_cell(ci);
    int b1 = (uint8_t)j[3], sb1 = (int8_t)j[3];
    const int32_t *T = m->shape == 0x447890 ? fact_walls : bldg_walls;
    int side = (m->flags & 0xff0000) >> 16;
    int v8 = (sb1 & 0x60) >> 5;
    memcpy(faces, m->faces, sizeof(ModelFace) * (size_t)m->nfaces);
    faces[4].sprite = roof_var[sb1 & 7] + T[0];
#define WALL(nci, s) (is_rubble(nci) ? T[1] : side == (s) ? ((b1 & 0x18) == 0x18 ? T[3] : T[2]) \
                                     : (v8 == (s) ? T[5] : T[4]))
    faces[1].sprite = WALL(ci - 1, 1) + 2;
    faces[2].sprite = WALL(ci + 1, 2);
    faces[3].sprite = WALL(ci + 128, 3);
#undef WALL
}

static void draw_with(View *v, const RenderItem *it, const int32_t *M, const int32_t (*verts)[3],
                      const ModelFace *faces)
{
    const ModelPart *m = it->m;
    int32_t tv[64][3];
    if (m->nverts > 64 || !m->verts) return;
    if (!verts) verts = part_verts(m);
    for (int i = 0; i < m->nverts; i++) vec_mul(tv[i], verts[i], M);   /* Mat_TransformVerts 0x4383c0 */
    emit_faces(v, it, (const int32_t (*)[3])tv, faces ? faces : m->faces);
}

static void draw_static(View *v, const RenderItem *it, const int32_t (*verts)[3], const ModelFace *faces)
{
    draw_with(v, it, v->mat, verts, faces);                  /* Model_DrawStatic 0x408840 */
}

static void draw_yaw(View *v, const RenderItem *it, const int32_t (*verts)[3], const ModelFace *faces)
{
    int32_t mat[9];                                          /* Model_DrawYaw 0x408a20 */
    const int32_t *M = v->mat;
    if (it->yaw & 0xffff0000) { mat_mul(mat, YAW[(it->yaw >> 16) & 63], v->mat); M = mat; }
    draw_with(v, it, M, verts, faces);
}

static void draw_yawpitch(View *v, const RenderItem *it)   /* Model_DrawYawPitch 0x408aa0 */
{
    int32_t mat[9], tmp[9];
    const int32_t *M = v->mat;
    int32_t pit = it->pitch < 0 ? -it->pitch : it->pitch;
    if (pit == 0) {
        if (it->yaw) { mat_mul(mat, YAW[(it->yaw >> 16) & 63], v->mat); M = mat; }
    } else {
        mat_mul(tmp, PITCH[(pit >> 16) & 63], YAW[(it->yaw >> 16) & 63]);
        mat_mul(mat, tmp, v->mat);
        M = mat;
    }
    draw_with(v, it, M, NULL, NULL);
}

#define TILT_INDEX 0x38                                      /* DAT_00440c4c (never written) */
static void draw_tilted(View *v, const RenderItem *it)     /* Model_DrawTilted 0x408d40 */
{
    int32_t a[9], b[9], mat[9];
    const int32_t *A = v->mat;
    if (it->pitch == 0) {
        if (it->yaw) { mat_mul(a, YAW[(it->yaw >> 16) & 63], v->mat); A = a; }
    } else {
        mat_mul(b, PITCH[(it->pitch >> 16) & 63], YAW[(it->yaw >> 16) & 63]);
        mat_mul(a, b, v->mat);
        A = a;
    }
    mat_mul(mat, A, PITCH[TILT_INDEX]);
    draw_with(v, it, mat, NULL, NULL);
}

/* Model_DrawTurret 0x408b80: Rx(item+0x20 * 4) * Ry(item+0x30 * 4) [pre-multiplied by
   Y[item+0x24] when mode 1] * Y[yaw] * C, the two rotations built with CosFixed/SinFixed. */
static void draw_turret(View *v, const RenderItem *it, int mode, const int32_t (*verts)[3])
{
    int32_t c = cosfixed(it->pitch << 2), s = sinfixed(it->pitch << 2);
    int32_t rx[9] = { 0x10000, 0, 0, 0, c, s, 0, -s, c };                   /* 0x457000 */
    c = cosfixed(it->x30 << 2); s = sinfixed(it->x30 << 2);
    int32_t ry[9] = { c, 0, s, 0, 0x10000, 0, -s, 0, c };                   /* 0x456fd8 */
    int32_t a[9], b[9], m[9];
    if (mode == 0) mat_mul(a, rx, ry);
    else {
        mat_mul(b, rx, ry);
        mat_mul(a, YAW[((uint32_t)it->x24 & 0x3f0000) >> 16], b);
    }
    mat_mul(b, a, YAW[(it->yaw >> 16) & 63]);
    mat_mul(m, b, v->mat);
    draw_with(v, it, m, verts, NULL);
}

static void rot_x(int32_t *m, int32_t a)                    /* FUN_00408400 into the zeroed 0x48b0b0 */
{
    int32_t c = cosfixed(a), s = sinfixed(a);
    int32_t r[9] = { 0x10000, 0, 0, 0, c, s, 0, -s, c };
    memcpy(m, r, sizeof r);
}

/* Barrel / launcher: n source verts, rotated about x by the elevation, + offset, into dst
   (FUN_0042f030 pattern: Mat_TransformVerts + FUN_0041ab70). */
static void place_gun(int32_t (*dst)[3], const int32_t (*src)[3], const int32_t *off, int n, int32_t elev)
{
    int32_t m[9];
    if (elev) rot_x(m, elev << 2);
    for (int i = 0; i < n; i++) {
        int32_t p[3] = { src[i][0], src[i][1], src[i][2] };
        if (elev) vec_mul(p, src[i], m);
        for (int k = 0; k < 3; k++) dst[i][k] = p[k] + off[k];
    }
}

static int32_t tank_barrel_src[7][3]; /* 0x44c938 */
static int32_t tank_barrel_off[3]; /* 0x44c704 */

/* Model_DrawTank 0x42f200: hull (tracks included), then the turret part 0x44ccb0 turned by the
   turret yaw with the barrel verts 15..21 (0x44c8e4) raised by the cannon elevation. */
static void draw_tank(View *v, RenderItem *it)
{
    draw_yaw(v, it, NULL, NULL);
    const RenderObj *o = it->obj;
    int32_t elev = 0;
    if (o && o->cls == 1 && o->veh) {
        it->yaw = (int32_t)(((uint32_t)it->yaw + (uint32_t)o->veh->turret) & 0x3fffff);
        elev = o->veh->elev;
    }
    const ModelPart *t = &model_parts[MP_44ccb0];
    int32_t vv[22][3];
    memcpy(vv, t->verts, sizeof vv);
    place_gun(vv + 15, tank_barrel_src, tank_barrel_off, 7, elev);
    it->m = t;
    draw_yaw(v, it, (const int32_t (*)[3])vv, NULL);
}

static int32_t msv_gun_src[8][3]; /* 0x44d028 */
static int32_t msv_gun_off[3]; /* 0x44d094 */

/* Model_DrawMSV 0x42f300: launcher verts 44..51 (0x44d350) raised by the elevation; face 13 sprite
   = 0x146 - launcher frame (state+0x58), a negative frame slides verts 4/5 of the launcher back. */
static void draw_msv(View *v, RenderItem *it)
{
    const ModelPart *m = it->m;
    const RenderObj *o = it->obj;
    int32_t y45 = -0x60000, fr = 0, elev = 0;
    if (o && o->cls == 1 && o->veh) {
        elev = o->veh->elev;
        fr = o->veh->turret;
        if (fr < 0) { y45 = -0x60000 - fr; fr = 0; }
    }
    int32_t src[8][3], vv[52][3];
    ModelFace f[14];
    if (m->nverts != 52 || m->nfaces != 14) { draw_yaw(v, it, NULL, NULL); return; }
    memcpy(src, msv_gun_src, sizeof src);
    src[4][1] = src[5][1] = y45;
    memcpy(vv, m->verts, sizeof vv);
    place_gun(vv + 44, (const int32_t (*)[3])src, msv_gun_off, 8, elev);
    memcpy(f, m->faces, sizeof f);
    f[13].sprite = 0x146 - fr;
    draw_yaw(v, it, (const int32_t (*)[3])vv, f);
}

/* Jeep boat morph: rows of {x, 0, z, x2, 0, z2} for verts 36..43 (0x44de70), by boat >> 13. */
static int32_t jeep_boat_tab[9][6];     /* 0x44de70 */

/* Model_DrawJeep 0x42f400: tyre sprites (faces 9/10) roll with x; the boat blend (state+0x80)
   folds the wheels (verts 36..43) and from 0x8000 on switches to 0x44dfb0 (hull + wake verts). */
static void draw_jeep(View *v, RenderItem *it)
{
    const RenderObj *o = it->obj;
    int32_t spr = 0x1c9;
    if (o) spr += (o->pos[0] & 0x30000) >> 16;
    int32_t vv[48][3];
    const int32_t (*verts)[3] = NULL;
    if (o && o->cls == 1 && o->veh && o->veh->rotor != 0 && it->m->nverts == 44) {
        int k = o->veh->rotor >> 13;
        if (k > 8) k = 8;
        const int32_t *t = jeep_boat_tab[k];
        memcpy(vv, it->m->verts, sizeof(int32_t) * 3 * 44);
        vv[36][0] = vv[37][0] = -t[0]; vv[36][2] = vv[37][2] = t[2];
        vv[38][0] = vv[39][0] = -t[3]; vv[38][2] = vv[39][2] = t[5];
        vv[40][0] = vv[41][0] = t[0];  vv[40][2] = vv[41][2] = t[2];
        vv[42][0] = vv[43][0] = t[3];  vv[42][2] = vv[43][2] = t[5];
        if (k > 3) {
            jeep_scale(o->veh->rotor < 0x4000 ? 0x4000 : o->veh->rotor);
            memcpy(vv[44], jeep_part_v, sizeof(int32_t) * 3 * 4);
            it->m = &model_parts[MP_44dfb0];
        }
        verts = (const int32_t (*)[3])vv;
    }
    ModelFace f[16];
    if (it->m->nfaces > 16 || it->m->nfaces < 11) { draw_yaw(v, it, verts, NULL); return; }
    memcpy(f, it->m->faces, sizeof(ModelFace) * (size_t)it->m->nfaces);
    f[9].sprite = f[10].sprite = spr;
    draw_yaw(v, it, verts, f);
}

static void draw_jeep_shadow(View *v, RenderItem *it)      /* Model_DrawJeepShadow 0x42f660 */
{
    const RenderObj *o = it->obj;
    if (o && o->veh) jeep_scale(o->veh->rotor < 0x4000 ? 0x4000 : o->veh->rotor);
    draw_yaw(v, it, NULL, NULL);
}

/* Heli tail fold: 0x120000 (18 px) on the lift, shrinking to 0 while the rotor spins up (state+0x58). */
static int32_t heli_fold(const RenderObj *o)
{
    if (!o) return 0;
    if (o->cls == 1) {
        int32_t s = o->veh ? o->veh->turret : 0x10000;
        return s < 0x10000 ? 0x120000 - fixmul(s, 0x120000) : 0;
    }
    return o->cls == 5 ? 0x120000 : 0;
}

static int8_t heli_tail_verts[9]; /* 0x44ee40 */

static void draw_heli(View *v, RenderItem *it)             /* Model_DrawHeli 0x42f990 */
{
    int32_t u = heli_fold(it->obj);
    const ModelPart *m = it->m;
    if (u == 0 || m->nverts != 44) { draw_turret(v, it, 0, NULL); return; }
    int32_t vv[44][3];
    memcpy(vv, m->verts, sizeof vv);
    for (const int8_t *k = heli_tail_verts; *k >= 0; k++) vv[(int)*k][1] -= u;
    draw_turret(v, it, 0, (const int32_t (*)[3])vv);
}

/* Two crossed blades at rest (heading and heading + spin-up * 32), part p drawn with Model_DrawYaw. */
static void draw_blades(View *v, RenderItem *it, int part)
{
    const RenderObj *o = it->obj;
    it->m = &model_parts[part];
    it->yaw = o->yaw;
    draw_yaw(v, it, NULL, NULL);
    it->yaw = (int32_t)(((uint32_t)(o->veh ? o->veh->turret : 0) * 0x20 + (uint32_t)o->yaw) & 0x3fffff);
    draw_yaw(v, it, NULL, NULL);
}

static int32_t heli_rotor_v3[6][3]; /* 0x44e898 */
static int32_t heli_rotor_v2[6][3]; /* 0x44e8e0 */

/* 0x42f860 ("Model_DrawHeliShadow"): the rotor disc on the heli, width by rotor speed (item team). */
static void draw_heli_rotor(View *v, RenderItem *it)
{
    const int32_t (*verts)[3] = NULL;
    if (it->team == 2) verts = heli_rotor_v2;
    else if (it->team == 3) verts = heli_rotor_v3;
    else if (it->team == 4) { if (it->obj) draw_blades(v, it, MP_44e9b8); return; }
    draw_turret(v, it, 1, verts);
}

static int32_t rotor_shadow_v2[4][3]; /* 0x44ef88 */

static void draw_rotor_a(View *v, RenderItem *it)          /* 0x42fb10: rotor shadow */
{
    const int32_t (*verts)[3] = NULL;
    if (it->team > 1) {
        if (it->team < 4) verts = rotor_shadow_v2;
        else if (it->team == 4) { if (it->obj) draw_blades(v, it, MP_44f058); return; }
    }
    draw_yaw(v, it, verts, NULL);
}

static void draw_rotor_b(View *v, RenderItem *it)          /* 0x42fba0: heli ground shadow */
{
    const RenderObj *o = it->obj, *p = o ? o->parent : NULL;
    int32_t u = heli_fold(p ? p : o);
    if (u && it->m->nverts == 4) {
        int32_t vv[4][3];
        memcpy(vv, it->m->verts, sizeof vv);
        vv[2][1] -= u;
        vv[3][1] -= u;
        draw_yaw(v, it, (const int32_t (*)[3])vv, NULL);
        return;
    }
    if (p && p->veh && (int32_t)((uint32_t)p->veh->rotor_speed & 0xffff0000u) > 0x3ffff) {
        it->m = &model_parts[MP_44f188];                     /* blurred disc shadow, frame by rotor angle */
        it->team = p->veh->rotor >> 19;
    }
    draw_yaw(v, it, NULL, NULL);
}

/* ---- Storage (bunker lift): pad, platform with the vehicle, sliding doors ---- */
static void draw_storage_pad(View *v, RenderItem *it)      /* 0x417f10: keep the last cel for the doors */
{
    if (g_last_cel.valid) g_pad_cel = g_last_cel;
    draw_static(v, it, NULL, NULL);
}

/* ---- Drone (class 9) and SUB (class 15) ---- */
static int32_t drone_gun_src[8][3]; /* 0x4552f8 -> verts 0..7, pitched */
static int32_t drone_rotor_src[6][3]; /* 0x455358 -> verts 42..47, spinning */

/* 0x435090: record+8 = tick, gun verts by the gun pitch, rotor verts by Y[(id + tick) & 15],
   Model_DrawTurret with pitch obj+0x68 and roll obj+0x58. */
static void draw_drone(View *v, RenderItem *it)
{
    const RenderObj *o = it->obj;
    const ModelPart *m = it->m;
    if (!o || m->nverts != 50) { draw_turret(v, it, 0, NULL); return; }
    if (o->stamp) *o->stamp = (int32_t)o->tick;
    int32_t vv[50][3];
    memcpy(vv, m->verts, sizeof vv);
    const int32_t *P = PITCH[((int32_t)o->gun_pitch >> 16) & 63];
    for (int i = 0; i < 8; i++) vec_mul(vv[i], drone_gun_src[i], P);
    it->pitch = o->pitch;
    it->x30 = o->roll;
    const int32_t *R = YAW[(o->id + o->tick) & 0xf];
    for (int i = 0; i < 6; i++) vec_mul(vv[42 + i], drone_rotor_src[i], R);
    draw_turret(v, it, 0, (const int32_t (*)[3])vv);
}

static void draw_sub(View *v, RenderItem *it)             /* 0x407320: sprite = frame - 1, hidden at 0 */
{
    const RenderObj *o = it->obj;
    if (!o) { draw_static(v, it, NULL, NULL); return; }
    if (o->stamp) *o->stamp = (int32_t)o->tick;              /* obj+0x64 */
    it->team = (o->u5c >> 16) - 1;
    if (it->team >= 0) draw_static(v, it, NULL, NULL);
}

/* Model_DrawMan 0x4061c0: face pair by heading octant and side (tables 0x43f480 team 0 / 0x43f680 team 1:
   shadow 755 + figure base + 20 per octant, mirrored octants share cels); the animation frame obj+0x64
   goes into item +8 (the team offset of the figure face, flag 8). Stamps obj+0x5c: ManUpdate removes men
   that have not been drawn for 120 ticks. (The copy of the frame's CCB into cel 756 is not needed.) */
static void draw_man(View *v, RenderItem *it)
{
    const RenderObj *o = it->obj;
    if (!o || it->m->nverts != 8) { draw_static(v, it, NULL, NULL); return; }
    if (o->stamp) *o->stamp = (int32_t)o->tick;
    int d = (int)((((uint32_t)o->yaw + 0x40000u) & 0x380000u) >> 19);
    /* The sprites come from the face-pair tables; the corners are kept unmirrored (the tables mirror them
       for octants 5..7: not reproduced by this port). */
    uint32_t pair = (o->team == 0 ? 0x43f480u : 0x43f680u) + 0x40u * (uint32_t)d;
    ModelFace f[2] = { { exe_s32(pair), exe_u32(pair + 4), 0, 0, { 4, 5, 6, 7 } },
                       { exe_s32(pair + 0x20), exe_u32(pair + 0x24), 0, 0, { 0, 1, 2, 3 } } };
    it->team = o->u64 >> 16;
    draw_static(v, it, NULL, f);
}

static void draw_man_swim(View *v, RenderItem *it)        /* Model_DrawManSwim 0x406280: 756 + 25*side + frame */
{
    const RenderObj *o = it->obj;
    if (!o) { draw_static(v, it, NULL, NULL); return; }
    if (o->stamp) *o->stamp = (int32_t)o->tick;
    it->team = (it->team != 0 ? 0x19 : 0) + (o->u64 >> 16);
    draw_yaw(v, it, NULL, NULL);
}

/* Model_DrawFlag 0x42f740 / Model_DrawFlagCarried 0x42f6e0: cloth frame obj+0x5c (+13 for side 1) in item +8,
   yaw = obj heading; the lying flag is pitched by (view pitch + DAT_0044e734 -0x190000) >> DAT_0044e738 (3). */
static void draw_flag(View *v, RenderItem *it, int carried)
{
    const RenderObj *o = it->obj;
    if (!o) { draw_static(v, it, NULL, NULL); return; }
    it->team = (o->team != 0 ? 0xd : 0) + (o->u5c >> 16);
    it->yaw = o->yaw;
    if (carried) { it->pitch = 0; draw_yaw(v, it, NULL, NULL); return; }
    it->pitch = (v->pitch + -0x190000) >> 3;
    draw_yawpitch(v, it);
}

/* Model_DrawGateH_W/E 0x4175a0/0x417610, GateV_N/S 0x417680/0x4176f0: the leaf spans 16 px minus the opening of
   the Gate object (obj+0x64, whole px) along x (H) / y (V), negated for the E / S half; the top face uses
   cel 0x35a while open. Map cells (no object) draw the closed gate. */
static void draw_gate(View *v, RenderItem *it, int axis, int sign, int top)
{
    const ModelPart *m = it->m;
    const RenderObj *o = it->is_obj ? it->obj : NULL;
    if (m->nverts != 15 + axis || m->nfaces <= top) { draw_static(v, it, NULL, NULL); return; }
    int32_t w = 0x100000, spr = 0x359;
    if (o) {
        w = 0x100000 - (int32_t)((uint32_t)o->u64 & 0xffff0000u);
        if (w > 0xfffff) w = 0x100000; else spr = 0x35a;
    }
    int32_t vv[16][3];
    ModelFace ff[8];
    memcpy(vv, m->verts, sizeof(int32_t[3]) * (size_t)m->nverts);
    memcpy(ff, m->faces, sizeof(ModelFace) * (size_t)m->nfaces);
    vv[9][axis] = vv[10][axis] = vv[13][axis] = vv[14][axis] = sign * w;
    ff[top].sprite = spr;
    draw_static(v, it, (const int32_t (*)[3])vv, ff);
}

/* FUN_00408780: draw one part at once at pos for obj (no depth hook, yaw = heading, team = obj team). */
static void draw_part_now(View *v, const int32_t *p, const ModelPart *m, const RenderObj *o)
{
    RenderItem t = { 0 };
    t.m = m;
    t.obj = o;
    t.is_obj = 1;
    t.ci = -1;
    t.team = o->team;
    t.yaw = o->yaw;
    t.pos[0] = m->off[0] - v->camx + p[0];
    t.pos[1] = m->off[1] - v->camy + p[1];
    t.pos[2] = (m->flags & 0x10) ? 0 : p[2] + m->off[2];
    int32_t r[3];
    vec_mul(r, t.pos, v->mat);
    memcpy(t.pos, r, sizeof r);
    t.pos[2] -= v->H;
    draw_item(v, &t);
}

static void draw_storage_lift(View *v, RenderItem *it)     /* 0x417e40 */
{
    draw_static(v, it, NULL, NULL);
    const RenderObj *o = it->obj;
    const ModelPart *d = o ? o->lift_model : NULL;
    if (!d) return;
    if (d == &model_parts[MP_44ee68]) {                      /* heli: folded shadow, body, one blade */
        it->m = &model_parts[MP_44f1d0]; draw_rotor_b(v, it);
        it->m = &model_parts[MP_44ee68]; draw_heli(v, it);
        it->m = &model_parts[MP_44e9b8]; draw_yaw(v, it, NULL, NULL);
        return;
    }
    int32_t p[3] = { (o->lift_dx << 16) + o->pos[0], (o->lift_dy << 16) + o->pos[1], o->pos[2] };
    if (d->next) draw_part_now(v, p, d->next, o);
    draw_part_now(v, p, d, o);
}

static int32_t door_mid_v[4][3]; /* 0x442f48 */
static const ModelFace door_face_r = { 0x338, 0x8, 0, 0, { 0, 1, 2, 3 } };   /* 0x442f98 */
static const ModelFace door_face_mid = { 0x33f, 0x0, 0, 0, { 0, 1, 2, 3 } }; /* 0x442fb8 */

static void draw_storage(View *v, RenderItem *it)          /* 0x417d70: doors slide 0x4ccc px per tick */
{
    const RenderObj *o = it->obj;
    int32_t d = (o ? o->u68 : 0) * 0x4ccc;
    if (d < 0x120000 && it->m->nfaces == 1) {
        int32_t x = it->pos[0];
        it->pos[0] = x - d - 0x50000;
        draw_static(v, it, NULL, NULL);
        it->pos[0] = x + d + 0x50000;
        draw_static(v, it, NULL, &door_face_r);
        it->pos[0] = x;
        draw_static(v, it, door_mid_v, &door_face_mid);
    }
    if (g_pad_cel.valid) {
        put_cel(v, g_pad_cel.idx, g_pad_cel.c, g_pad_cel.mode);
        g_pad_cel.valid = false;
    }
}

/* FUN_00407df0 with the value 1.0 - t: hidden below 1/16, drawn a second time (the PIXC blend written to
   +0x30 is not read by the PC cel engine) below 1.0. */
static void put_cel_fade(View *v, int idx, const int32_t c[8], int32_t val)
{
    int k = (val >> 12) - 1;
    if (k < 1) return;
    put_cel(v, idx, c, -1);
    if (k < 16) put_cel(v, idx, c, -1);
}

/* Explosion sprites 0x4206b0: the descriptor's quads (scaled / yawed / pitched), one frame-selected cel
   per face whose frame range contains the current frame; variants picked by the object id. */
static void draw_expl(View *v, RenderItem *it)
{
    const RenderObj *o = it->obj;
    const RenderExpl *e = o ? o->expl : NULL;
    if (!e || e->nverts <= 0 || e->nverts > 64) return;
    int32_t M[9], a[9], b[9];
    const int32_t *R;
    int pk = e->pitch;
    if (e->scale == 0x10000) {
        if (pk == 0) {
            if (o->yaw == 0) R = v->mat;
            else { mat_mul(M, YAW[(it->yaw >> 16) & 63], v->mat); R = M; }
        } else {
            mat_mul(a, PITCH[pk & 63], YAW[(it->yaw >> 16) & 63]);
            mat_mul(M, a, v->mat);
            R = M;
        }
    } else {
        int32_t S[9] = { e->scale, 0, 0, 0, e->scale, 0, 0, 0, ((e->gflags >> 16) & 4) ? 0x10000 : e->scale };  /* 0x481750 */
        if (pk == 0) {
            if (o->yaw == 0) mat_mul(M, S, v->mat);
            else { mat_mul(a, S, YAW[(it->yaw >> 16) & 63]); mat_mul(M, a, v->mat); }
        } else {
            mat_mul(b, S, PITCH[pk & 63]);
            mat_mul(a, b, YAW[(it->yaw >> 16) & 63]);
            mat_mul(M, a, v->mat);
        }
        R = M;
    }
    int32_t tv[64][3], X[64], Y[64];
    for (int i = 0; i < e->nverts; i++) vec_mul(tv[i], e->verts[i], R);
    if (!project_verts(v, it, (const int32_t (*)[3])tv, e->nverts, X, Y)) return;
    int f = (int8_t)(e->frame >> 16);
    const int32_t *p = e->faces, *end = e->faces + e->nfaces * 6;
    uint32_t id = o->id;
    while (p < end) {
        const int32_t *face = p, *next = p + 6;
        int type = ((const uint8_t *)&p[1])[3] & 7;
        switch (type) {
        case 1: if (id & 1) { face = p + 6; next = p + 12; } break;
        case 2: face = p + (id & 3) * 6; next = p + 24; break;
        case 3: face = p + (id & 7) * 6; next = p + 48; break;
        case 4: if (id & 2) { face = p + 6; next = p + 12; } break;
        default: break;
        }
        p = next;
        const int8_t *fb = (const int8_t *)&face[1];
        int lo = fb[0], hi = fb[1], fade = fb[2];
        if (lo > f || f > hi) continue;
        int32_t c[8];
        for (int j = 0; j < 4; j++) {
            int k = face[2 + j];
            if (k < 0 || k >= e->nverts) return;
            c[j * 2] = X[k]; c[j * 2 + 1] = Y[k];
        }
        int idx = face[0] + (f - lo);
        if (fade <= f) {
            int d = hi - fade;
            int32_t r = (d + 2 >= 1 && d + 2 <= 255) ? fixdiv(0x10000, (d + 2) << 16) : 0;   /* 0x482084[d] */
            put_cel_fade(v, idx, c, 0x10000 - fixmul((f - fade + 1) * 0x10000, r));
        } else put_cel(v, idx, c, -1);
    }
}

/* Debris piece 0x433a60: settled pieces that come back into view after 60 idle ticks are removed. */
static void draw_debris(View *v, RenderItem *it)
{
    const RenderObj *o = it->obj;
    const RenderDebris *d = o ? o->debris : NULL;
    if (!d) return;
    if (o->inactive && o->drawn && (int32_t)(*o->drawn + 0x3c) < (int32_t)o->tick) {
        if (o->draw_flags) *o->draw_flags |= 1;
        return;
    }
    if (o->drawn) *o->drawn = (int32_t)o->tick;
    int32_t M[9], a[9];
    const int32_t *R;
    if ((o->pitch >> 16) == 0) {
        if (o->yaw == 0) R = v->mat;
        else { mat_mul(M, YAW[(it->yaw >> 16) & 63], v->mat); R = M; }
    } else {
        mat_mul(a, PITCH[(o->pitch >> 16) & 63], YAW[(it->yaw >> 16) & 63]);
        mat_mul(M, a, v->mat);
        R = M;
    }
    int32_t tv[4][3], X[4], Y[4];
    for (int i = 0; i < 4; i++) vec_mul(tv[i], d->verts[i], R);
    if (!project_verts(v, it, (const int32_t (*)[3])tv, 4, X, Y)) return;
    int32_t c[8] = { X[0], Y[0], X[1], Y[1], X[2], Y[2], X[3], Y[3] };      /* corner order 0x454ed8 */
    int f = d->frame >> 16;
    if (d->fade <= f) {
        int k = d->frames - d->fade;
        int32_t r = (k + 2 >= 1 && k + 2 <= 255) ? fixdiv(0x10000, (k + 2) << 16) : 0;   /* 0x482084[k] */
        put_cel_fade(v, d->sprite, c, r * ((d->fade - f) - 1) + 0x10000);
    } else put_cel(v, d->sprite, c, -1);
}

static void draw_item(View *v, RenderItem *it)
{
    const ModelPart *m = it->m;
    if (it->stay && it->obj) {                             /* Stay 0x435e40: expiry + recycle-list touch */
        const RenderObj *o = it->obj;
        if (o->drawn && *o->drawn != (int32_t)o->tick) {
            if (o->expire != 0 && o->expire <= (int32_t)o->tick && (int32_t)o->tick > *o->drawn + o->linger) {
                if (o->draw_flags) *o->draw_flags |= 1;
                return;
            }
            if (o->draw_flags) *o->draw_flags |= 2;
            *o->drawn = (int32_t)o->tick;
        }
    }
    switch (m->fn) {
    case MODEL_FN_EXPL: draw_expl(v, it); return;
    case MODEL_FN_DEBRIS: draw_debris(v, it); return;
    case MODEL_FN_YAW: draw_yaw(v, it, NULL, NULL); return;
    case MODEL_FN_YAWPITCH: draw_yawpitch(v, it); return;
    case MODEL_FN_TURRET: draw_turret(v, it, 0, NULL); return;
    case MODEL_FN_DRONE: draw_drone(v, it); return;
    case MODEL_FN_DRONE_SHADOW: it->team = 0; draw_yaw(v, it, NULL, NULL); return;   /* 0x435120 */
    case MODEL_FN_SUB: draw_sub(v, it); return;
    case MODEL_FN_TILTED: draw_tilted(v, it); return;
    case MODEL_FN_TANK: draw_tank(v, it); return;
    case MODEL_FN_MSV: draw_msv(v, it); return;
    case MODEL_FN_JEEP: draw_jeep(v, it); return;
    case MODEL_FN_JEEP_SHADOW: draw_jeep_shadow(v, it); return;
    case MODEL_FN_HELI: draw_heli(v, it); return;
    case MODEL_FN_HELI_ROTOR: draw_heli_rotor(v, it); return;
    case MODEL_FN_ROTOR_A: draw_rotor_a(v, it); return;
    case MODEL_FN_ROTOR_B: draw_rotor_b(v, it); return;
    case MODEL_FN_STORAGE: draw_storage(v, it); return;
    case MODEL_FN_STORAGE_LIFT: draw_storage_lift(v, it); return;
    case MODEL_FN_STORAGE_PAD: draw_storage_pad(v, it); return;
    case MODEL_FN_BUILDING:
        if (!it->is_obj && it->ci >= 0 && m->nfaces >= 5 && m->nfaces <= 16) {
            ModelFace faces[16];
            patch_building(it, faces);
            draw_static(v, it, NULL, faces);
            return;
        }
        draw_static(v, it, NULL, NULL);
        return;
    case MODEL_FN_MAN: draw_man(v, it); return;
    case MODEL_FN_MAN_SWIM: draw_man_swim(v, it); return;
    case MODEL_FN_FLAG: draw_flag(v, it, 0); return;
    case MODEL_FN_FLAG_CARRIED: draw_flag(v, it, 1); return;
    case MODEL_FN_GATE_H_W: draw_gate(v, it, 0, 1, 6); return;
    case MODEL_FN_GATE_H_E: draw_gate(v, it, 0, -1, 6); return;
    case MODEL_FN_GATE_V_N: draw_gate(v, it, 1, 1, 7); return;
    case MODEL_FN_GATE_V_S: draw_gate(v, it, 1, -1, 7); return;
    case MODEL_FN_STATIC: case MODEL_FN_AMMO:
    case MODEL_FN_DEST_BUSH: case MODEL_FN_DEST_TREE: case MODEL_FN_DEST_WTOWER: case MODEL_FN_DEST_FUEL:
        draw_static(v, it, NULL, NULL);
        return;
    default:
        /* wrappers not ported: the yaw matrix for live objects, C for map cells */
        if (it->is_obj) draw_yaw(v, it, NULL, NULL);
        else draw_static(v, it, NULL, NULL);
        return;
    }
}

/* ---- RenderWorldView 0x419dd0 ---- */
void render_world(View *v, const RenderMap *map, Framebuffer *fb, int scale)
{
    g_map = map;
    g_view = v;
    v->enemy_turrets = 0;                                    /* DAT_00471458[view team] = 0 */
    g_fb = fb;
    g_scale = scale == 2 ? 2 : 1;
    g_clip = (ClipRect){ v->x * g_scale, v->y * g_scale, (v->x + v->w) * g_scale - 1, (v->y + v->h) * g_scale - 1 };
    if (g_clip.x1 >= fb->w) g_clip.x1 = fb->w - 1;
    if (g_clip.y1 >= fb->h) g_clip.y1 = fb->h - 1;
    v->nitems = 0;
    v->ncels = 0;
    g_last_cel.valid = false;                                /* Cel_FlushList: DAT_0048ca8c = 0 */
    /* bucket live objects by cell (obj chain per cell) */
    memset(obj_head, 0xff, sizeof obj_head);
    int ntail = 0;
    static int16_t offmap[RENDER_MAX_OBJS];
    for (int k = v->nobjs - 1; k >= 0; k--) {
        int cx = v->objs[k]->pos[0] >> 21, cy = v->objs[k]->pos[1] >> 21;
        if (cx < 0 || cy < 0 || cx > 127 || cy > 127) { offmap[ntail++] = (int16_t)k; continue; }
        int ci = cy * 128 + cx;
        obj_next[k] = obj_head[ci];
        obj_head[ci] = (int16_t)k;
    }
    int32_t cxv = v->cx, cyv = v->cy;
    int32_t uv5 = (v->camx >> 16) + v->left;
    int32_t uv2 = (v->camy >> 16) - 0x20 + v->top;
    int32_t col = uv5 >> 5;                                  /* local_40 */
    int32_t xrel = v->left - (uv5 & 0x1f);                   /* local_3c */
    int32_t row = uv2 >> 5;                                  /* local_18 */
    int32_t yrel = (v->top - (uv2 & 0x1f)) - 0x20;           /* local_20 */
    int32_t i8 = v->m10 * yrel + v->H;
    int32_t xs = fixmul(xrel * 0x10000, ptab(-i8 >> 16));    /* local_2c */
    int32_t ys = fixdiv((F >> 16) * v->m11, F + i8) * yrel;  /* local_30 */
    int32_t tw = fixdiv(F, i8 + F) << 5;                     /* local_1c */
    int on = 0;                                              /* local_10 */
    int32_t ypix = row << 5;                                 /* local_24 */
    int32_t rbase = row << 7;                                /* local_14 */
    int bx0 = 0x7fffffff, bx1 = -0x7fffffff, by0 = ypix, by1 = ypix;
    for (;;) {
        int32_t top_y = ys;
        if (cyv <= ys) {
            if (on == 0) break;
            on = 0;
        }
        int32_t xc = xs, tw_top = tw;
        if (on)
            while (tw + xc < -0x20 - cxv) { col++; xrel += 0x20; xc += tw; }
        int32_t cx_ = col - 1;
        bool rowok = rbase >= 0 && rbase <= 0x3fff;
        int cell = (rowok && cx_ >= 0 && cx_ <= 0x7f) ? rbase + cx_ : -1;
        queue_cell(v, cell, cx_ * 0x20, ypix);
        if (cx_ * 0x20 < bx0) bx0 = cx_ * 0x20;
        cx_++;
        if (cell < 0) cell = (rowok && cx_ >= 0 && cx_ < 0x80) ? rbase + cx_ : -1;
        else { cell++; if (cx_ > 0x7f) cell = -1; }
        yrel += 0x20;
        int32_t i6 = v->m10 * yrel + v->H;
        xs = fixmul(xrel << 16, ptab(-i6 >> 16));
        int32_t ybot = fixdiv((F >> 16) * v->m11, F + i6) * yrel;
        ys = ybot;
        tw = fixdiv(F, i6 + F) << 5;
        xc = xc + cxv;
        int32_t xb = cxv + xs;
        int32_t c_top = xc & ~0x7fff, c_bot = xb & ~0x7fff;
        int32_t y_top = top_y + cyv, y_bot = cyv + ybot;
        if (xc < v->w * 0x10000) {
            int32_t px = cx_ << 5;
            for (;;) {
                queue_cell(v, cell, px, ypix);
                xc += tw_top;
                if (on) {
                    int32_t l_top = c_top, l_bot = c_bot;
                    c_top = xc & ~0x7fff;
                    xb += tw;
                    c_bot = xb & ~0x7fff;
                    int t = cell < 0 ? 2 : (int)(map->cell[cell] & 0x7f);
                    int mode = 0xc;
                    if (cell >= 0 && (t == 1 || t == 2 || (t > 3 && t < 0x34))) mode = 0x13;
                    int32_t c[8] = { l_top, y_top, c_top, y_top, c_bot, y_bot, l_bot, y_bot };
                    put_cel(v, t, c, mode);
                }
                px += 0x20;
                cx_++;
                if (cell < 0) {
                    if (rowok && px >= 0 && px < 0x1000) cell = rbase + cx_;
                } else {
                    cell++;
                    if (px > 0xfff) cell = -1;
                }
                if (!(xc < v->w * 0x10000)) break;
            }
        }
        queue_cell(v, cell, cx_ << 5, ypix);
        if ((cx_ << 5) + 32 > bx1) bx1 = (cx_ << 5) + 32;
        row++;
        ypix += 0x20;
        rbase += 0x80;
        by1 = ypix;
        if (ybot < cyv) on = 1;
    }
    /* objects on the off-map pseudo cell (0x452d40) inside the visited box */
    for (int k = ntail - 1; k >= 0; k--) {
        const RenderObj *o = v->objs[offmap[k]];
        int ox = o->pos[0] >> 16, oy = o->pos[1] >> 16;
        if (ox >= bx0 && ox < bx1 && oy >= by0 && oy < by1) queue_model(v, o->model, o->pos, -1, o);
    }
    /* View_DrawQueuedModels 0x408e50: head first = largest key = farthest */
    for (int i = 0; i < v->nitems; i++) draw_item(v, &v->items[i]);
    g_view = NULL;
}

/* ---- tables read from RFIRE.BIN (view_tables_load) ---- */
static void view_tables_load(void)
{
    for (int i = 0; i < 9; i++) ((int32_t *)tracker_par_default)[i] = exe_s32(0x43eee4 + 4 * (uint32_t)i);
    for (int i = 0; i < 18; i++) ((int32_t *)jeep_part_src)[i] = exe_s32(0x44dc38 + 4 * (uint32_t)i);
    for (int i = 0; i < 18; i++) ((int32_t *)jeep_part_v)[i] = exe_s32(0x44dbf0 + 4 * (uint32_t)i);
    for (int i = 0; i < 6; i++) ((int32_t *)bldg_walls)[i] = exe_s32(0x4478d8 + 4 * (uint32_t)i);
    for (int i = 0; i < 6; i++) ((int32_t *)fact_walls)[i] = exe_s32(0x4478f0 + 4 * (uint32_t)i);
    for (int i = 0; i < 21; i++) ((int32_t *)tank_barrel_src)[i] = exe_s32(0x44c938 + 4 * (uint32_t)i);
    for (int i = 0; i < 3; i++) ((int32_t *)tank_barrel_off)[i] = exe_s32(0x44c704 + 4 * (uint32_t)i);
    for (int i = 0; i < 24; i++) ((int32_t *)msv_gun_src)[i] = exe_s32(0x44d028 + 4 * (uint32_t)i);
    for (int i = 0; i < 3; i++) ((int32_t *)msv_gun_off)[i] = exe_s32(0x44d094 + 4 * (uint32_t)i);
    for (int i = 0; i < 54; i++) ((int32_t *)jeep_boat_tab)[i] = exe_s32(0x44de70 + 4 * (uint32_t)i);
    for (int i = 0; i < 18; i++) ((int32_t *)heli_rotor_v3)[i] = exe_s32(0x44e898 + 4 * (uint32_t)i);
    for (int i = 0; i < 18; i++) ((int32_t *)heli_rotor_v2)[i] = exe_s32(0x44e8e0 + 4 * (uint32_t)i);
    for (int i = 0; i < 12; i++) ((int32_t *)rotor_shadow_v2)[i] = exe_s32(0x44ef88 + 4 * (uint32_t)i);
    for (int i = 0; i < 24; i++) ((int32_t *)drone_gun_src)[i] = exe_s32(0x4552f8 + 4 * (uint32_t)i);
    for (int i = 0; i < 18; i++) ((int32_t *)drone_rotor_src)[i] = exe_s32(0x455358 + 4 * (uint32_t)i);
    for (int i = 0; i < 12; i++) ((int32_t *)door_mid_v)[i] = exe_s32(0x442f48 + 4 * (uint32_t)i);
    for (int i = 0; i < 16; i++) ((int8_t *)bubble_frames)[i] = exe_s8(0x44c5e8 + 1 * (uint32_t)i);
    for (int i = 0; i < 8; i++) ((int8_t *)roof_var)[i] = exe_s8(0x4478d0 + 1 * (uint32_t)i);
    for (int i = 0; i < 9; i++) ((int8_t *)heli_tail_verts)[i] = (int8_t)exe_s32(0x44ee40 + 4 * (uint32_t)i);
}
EXE_LOADER(view_tables_load)
