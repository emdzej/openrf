/* 1-player game loop: State_Game1P 0x41a660 order — poll input, simulate, render the view
   (camera following the player's vehicle), status bar, present. */
#include "play.h"
#include "assets.h"
#include "clock.h"
#include "input.h"
#include "music.h"
#include "sfx.h"
#include "platform.h"
#include "render/render.h"
#include "render/ccb.h"
#include "ui_select.h"
#include "hud.h"
#include "game/game.h"
#include "game/vehicle.h"
#include "game/effect.h"
#include "game/weapon.h"
#include "game/ai.h"
#include "game/rules.h"
#include "play_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Graphic address (obj+0x3c) -> model part. */
static const ModelPart *model_by_addr(uint32_t addr)
{
    for (int i = 0; i < MODEL_PART_COUNT; i++)
        if (model_parts[i].addr == addr) return &model_parts[i];
    return NULL;
}

typedef struct {
    View *v;
    RenderObj objs[RENDER_MAX_OBJS];
    RenderVeh veh[RENDER_MAX_OBJS];
    RenderExpl expl[RENDER_MAX_OBJS];
    RenderDebris debris[RENDER_MAX_OBJS];
    int n;
} Collect;

/* Live object (active, inactive and recycle lists = what the cell chains hold) -> RenderObj with the
   fields the draw hooks read (docs/render.md 5). The heli's ground shadow is a class-4 child object in
   the original (0x42b5e0); it is synthesised here. */
static void collect(Obj *o, void *ctx)
{
    Collect *c = ctx;
    if (!o->gfx || c->n >= RENDER_MAX_OBJS) return;
    int cls = o->cls->id, slot = (int)(o->id & 0x1ff);
    const ModelPart *m;
    if (cls == 3) m = &render_part_debris;                           /* FWall 0x454f18 */
    else if (cls == 17) m = o->p60 ? model_by_addr(((const Gfx *)o->p60)->addr) : NULL;   /* Stay: wrapped graphic */
    else if (cls == 11 && o->gfx->addr == 0x44aa20) m = &render_part_expl;
    else m = model_by_addr(o->gfx->addr);
    if (!m) return;
    int k = c->n++;
    RenderObj *r = &c->objs[k];
    *r = (RenderObj){ .model = m, .pos = { o->pos[0], o->pos[1], o->pos[2] }, .yaw = (int32_t)o->heading, .zbias = o->_50, .team = o->team };
    r->cls = cls;
    r->id = o->id;
    r->u68 = o->u68;
    r->u70 = o->u70;
    r->tick = (uint32_t)g_tick;
    r->stamp = &o->u64;
    r->inactive = (o->flags & OF_INACTIVE_LIST) != 0;
    r->draw_flags = &obj_draw_flags[slot];
    switch (cls) {
    case 0: case 7: case 16:                                         /* projectiles: hook 0x41f800 */
        r->pitch = o->i60;
        r->u70 = (int8_t)o->b70[1];
        r->stamp = NULL;
        break;
    case 18:                                                         /* grenade: hook 0x41f830 */
        r->u70 = (o->team != 0 ? 0xc : 0) + (o->u70 >> 16);
        r->stamp = NULL;
        break;
    case 11: {                                                       /* Expl */
        RenderExpl *e = &c->expl[k];
        uint32_t d = o->e60;
        *e = (RenderExpl){ o->u64, o->u6c, o->b57, expl_field(d, ED_FLAGS), (int)expl_field(d, ED_NVERTS),
                           (const int32_t (*)[3])rf_ptr(expl_field(d, ED_VERTS)), (int)expl_field(d, ED_NFACES),
                           rf_ptr(expl_field(d, ED_FACES)) };
        if (!e->verts || !e->faces) e->nverts = e->nfaces = 0;
        r->expl = e;
        r->stamp = NULL;
        break;
    }
    case 3: {                                                        /* FWall */
        const DebrisExt *x = &debris_ext[slot];
        const DebrisDef *dd = o->p5c;
        RenderDebris *d = &c->debris[k];
        d->sprite = x->sprite;
        memcpy(d->verts, x->verts, sizeof d->verts);
        d->frame = o->u64;
        d->frames = dd ? dd->frames : 0;
        d->fade = dd ? dd->fade : 0;
        r->debris = d;
        r->pitch = o->u68;
        r->drawn = &debris_ext[slot].drawn;
        r->stamp = NULL;
        break;
    }
    case 17:                                                         /* Stay */
        r->stay = 1;
        r->pitch = (int32_t)o->v5c;
        r->drawn = &o->u68;
        r->expire = o->u64;
        r->linger = o->u6c;
        r->stamp = NULL;
        break;
    case 4: case 10:
        r->stamp = NULL;
        break;
    case 2:                                                          /* Turret Gun: hook 0x4174e0 */
        r->pitch = (int32_t)TURRET_PITCH(o);
        r->unseen = &TURRET_UNSEEN(o);
        r->stamp = NULL;
        break;
    case 9: {                                                        /* Drone: draw 0x435090 */
        DroneRec *dr = drone_rec(o);
        r->pitch = DRONE_PITCH(o);
        r->roll = DRONE_ROLL(o);
        r->gun_pitch = dr ? (int32_t)dr->gun_pitch : 0;
        r->stamp = dr ? &dr->drawn : NULL;
        break;
    }
    case 15:                                                         /* SUB: draw 0x407320 */
        r->u5c = SUB_FRAME(o);
        r->stamp = &SUB_DRAWN(o);
        break;
    case 12: case 13: case 14:                                       /* Flag, Gate, MAN (play_rules.c) */
        play_rules_collect(o, r);
        break;
    }
    if ((cls == 1 || cls == 6) && o->p60) {
        const VehState *s = veh_state(o);
        RenderVeh *rv = &c->veh[k];
        *rv = (RenderVeh){ s->water_anim, (uint32_t)s->_4c, s->elev, s->turret, s->boat, s->boat_target,
                           s->tilt, s->def->sink_depth };
        r->veh = rv;
    } else if (cls == 5 && o->p60) {                                 /* Storage: vehicle on the lift */
        r->lift_model = model_by_addr(((const Gfx *)o->p60)->addr);
        r->lift_dx = (int8_t)(o->u58 & 0xff);
        r->lift_dy = (int8_t)((o->u58 >> 8) & 0xff);
    }
    render_add_object(c->v, r);
    if (cls == 1 && r->veh && veh_state(o)->def->type == VT_HELI && c->n < RENDER_MAX_OBJS) {
        RenderObj *sh = &c->objs[c->n++];
        render_heli_shadow(sh, r);
        render_add_object(c->v, sh);
    }
}

/* Model_TowerHook 0x4174e0 -> ActivateTurret: the new turret joins this frame's object list. */
static int on_tower(void *user, View *v, int ci)
{
    Collect *c = user;
    Obj *t = turret_tower_hook(&G.cell[ci], v->team);
    if (!t) return 0;
    collect(t, c);
    return 1;
}

static void *ai_snd_play(uint32_t ev, uint32_t *handle) { return sfx_play(ev, NULL, handle); }
static void ai_snd_stereo(void *in, const int32_t *l, const int32_t *r) { sfx_set_stereo_sources(in, l, r); }
static void ai_snd_kill(void *in, uint32_t handle) { sfx_cmd(SFX_KILL, (intptr_t)handle, in, true); }

static int on_music(int track, int prio, Obj *owner)
{
    return music_request(track, prio, owner ? (int)owner->id : 0) ? 0 : -1;
}

/* Listener gain = view+0xe0: 0 on entering the bunker select, copied from the view fade by
   ViewModeZoomIn 0x404e80, FUN_00404e30 (fade to select) and FUN_004052d0 (fly-back fade),
   left alone by the other modes (so 0x10000 while driving). */
static int32_t view_sound_gain(const BunkerView *bv, int32_t prev, bool *last_copy)
{
    bool copy = bv->mode == BV_ZOOM_IN || bv->mode == BV_FADE_TO_SELECT || (bv->mode == BV_FLYBACK && bv->fly_stage == 1);
    int32_t g = prev;
    if (bv->mode == BV_SELECT || bv->mode == BV_LAUNCH) g = 0;
    else if (copy || *last_copy) g = bv->fade;
    *last_copy = copy;
    return g;
}

/* The status-bar RFA indexes the game palette, shifted +10 in the DirectDraw palette
   (LoadBmpSurfaceCached(.., 10)). Blits the source rectangle (sx, sy, w, h) to (dx, dy) (BltFast). */
static void blit_rfa(const Image8 *img, Framebuffer *fb, int sx, int sy, int w, int h, int dx, int dy)
{
    for (int j = 0; j < h; j++) {
        int y = dy + j;
        if (y < 0 || y >= fb->h || sy + j < 0 || sy + j >= img->h) continue;
        const uint8_t *s = img->pixels + (sy + j) * img->w;
        uint8_t *d = fb->pixels + y * fb->w;
        for (int i = 0; i < w; i++) {
            int x = dx + i;
            if (x < 0 || x >= fb->w || sx + i < 0 || sx + i >= img->w) continue;
            d[x] = (uint8_t)(s[sx + i] < 236 ? s[sx + i] + 10 : s[sx + i]);
        }
    }
}

static void draw_statusbar(const Image8 *img, Framebuffer *fb)
{
    blit_rfa(img, fb, 0, 0, img->w < fb->w ? img->w : fb->w, img->h, 0, fb->h - img->h);
}

/* DrawStatusBarBackground 0x437f90, 2 players: the middle strip 2pMScr.rfa between the views
   (LoadStatusBarArt 0x437e80: lo-res source rect (16,0)-(w,150) at x 0x9c, hi-res (0,0)-(16,h) at
   x 0x138, both at the top), then the status bar over the bottom. */
static void draw_divider(const Image8 *img, Framebuffer *fb, int hires)
{
    if (hires) blit_rfa(img, fb, 0, 0, 16, img->h, 0x138, 0);
    else blit_rfa(img, fb, 16, 0, img->w - 16, 0x96, 0x9c, 0);
}

/* Debug: OPENRF_DEMO=1 scripts input (launch the selected tank, drive, turn) for headless checks.
   OPENRF_DEMO=fire|jeep|msv|heli: debug targets are placed west of the pad (bush, building, wall), the
   vehicle is picked in the bunker, launched, turned west and fires its weapons (screenshots).
   OPENRF_DEMO=turret: two enemy towers north-west of the pad wake up and duel the tank; =drone: 3 drones
   per team, the tank idles next to the pad; =sub: the heli is moved near the west edge once airborne and
   flies off the map (SpawnSub). */
enum { DEMO_OFF, DEMO_DRIVE, DEMO_TANK, DEMO_JEEP, DEMO_MSV, DEMO_HELI, DEMO_TURRET, DEMO_DRONE, DEMO_SUB,
       DEMO_2P, DEMO_2P_WIN, DEMO_2P_SPECTATE, DEMO_2P_HELI };
static uint32_t demo_input(uint64_t t, int mode)
{
    uint32_t w = 0;
    if (t >= 20 && t < 22 && (mode == DEMO_JEEP || mode == DEMO_MSV)) w = IN_DOWN;   /* bunker menu */
    else if (t >= 36 && t < 38 && (mode == DEMO_MSV || mode == DEMO_HELI)) w = IN_LEFT;
    else if (t >= 36 && t < 38 && mode == DEMO_SUB) w = IN_LEFT;
    else if (t >= 60 && t < 64) w = IN_BTN1;                    /* launch */
    else if (mode == DEMO_TURRET) {                             /* enemy towers north-west: drive into view, shoot */
        if (t >= 420 && t < 440) w = IN_UP;
        else if (t >= 450 && t < 514) w = IN_RIGHT;             /* hull south -> west */
        else if (t >= 520 && t < 540) w = IN_BTN5;              /* turret towards the towers */
        else if (t >= 560 && (t - 560) % 24 < 3) w = IN_BTN1;
    } else if (mode == DEMO_DRONE) {                            /* sit still off the pad: the idle drone comes */
        if (t >= 420 && t < 440) w = IN_UP;
    }                                                           /* DEMO_SUB: see the play loop */
    else if (mode == DEMO_DRIVE) {
        if (t >= 420 && t < 560) w = IN_UP;                     /* drive off the pad */
        else if (t >= 560 && t < 620) w = IN_UP | IN_LEFT;
        else if (t >= 620) w = IN_UP;
    } else if (mode == DEMO_TANK || mode == DEMO_MSV) {
        if (t >= 420 && t < 440) w = IN_UP;
        else if (t >= 450 && t < 514 + (mode == DEMO_MSV) * 16) w = IN_RIGHT;      /* hull south -> west */
        else if (t >= 530 && t < 760 && (t - 530) % 30 < 3) w = IN_BTN1;          /* volley at the targets */
        else if (t >= 760 && t < 800 && mode == DEMO_TANK) w = IN_BTN5;            /* turret right (north-west) */
        else if (t >= 800 && t < 960 && (t - 800) % 30 < 3) w = IN_BTN1;
        else if (t >= 960 && t < 1020) w = IN_BTN2;                                /* raise the barrel / launcher */
        else if (t >= 1020 && t < 1200 && (t - 1020) % 40 < 3) w = IN_BTN2;       /* lob */
    } else if (mode == DEMO_JEEP) {
        if (t >= 440 && t < 476) w = IN_UP | IN_RIGHT;                            /* turn west while rolling */
        else if (t >= 476 && t < 490) w = IN_DOWN;
        else if (t >= 520 && t < 900 && (t - 520) % 40 < 3) w = IN_BTN1;          /* grenades */
    } else if (mode == DEMO_HELI) {
        if (t >= 700 && t < 730) w = IN_RIGHT;                                     /* yaw 135 -> 270 */
        else if (t >= 780 && t < 1000 && (t - 780) % 16 < 3) w = IN_BTN1;         /* guns, pitched down */
        else if (t >= 1000 && t < 1003) w = IN_BTN3;                               /* select missiles */
        else if (t >= 1020 && t < 1200 && (t - 1020) % 30 < 3) w = IN_BTN1;       /* dropped missiles */
    }
    if (w & (IN_LEFT | IN_RIGHT)) w |= 0xff;
    if (w & (IN_UP | IN_DOWN)) w |= 0xff00;
    return w;
}

/* 2-player demos (OPENRF_DEMO=2p|2pwin|2pspectate|2pheli, on a 2-player map): player 1 launches a tank,
   player 2 picks the jeep (2pheli: the heli) and launches it; both drive off. 2pwin: EndGame(1) once both
   are out (green banner); 2pspectate: side 0 loses its tank with nothing left in stock -> fly-back skull ->
   spectate screen while player 2 plays on. */
static uint32_t demo_input_2p(uint64_t t, int mode, int p)
{
    uint32_t w = 0;
    if (p == 1 && t >= 20 && t < 22) w = mode == DEMO_2P_HELI ? IN_LEFT : IN_DOWN;   /* bunker menu: jeep / heli */
    else if (t >= 60 + (uint64_t)p * 40 && t < 64 + (uint64_t)p * 40) w = IN_BTN1;                       /* launch (P2 a bit later) */
    else if (p == 0 && mode != DEMO_2P_SPECTATE) {
        if (t >= 420 && t < 560) w = IN_UP;
        else if (t >= 560 && t < 600) w = IN_UP | IN_LEFT;
        else if (t >= 600 && t < 900) w = IN_UP;
    } else if (p == 1) {
        if (mode == DEMO_2P_HELI) {
            if (t >= 600 && t < 640) w = IN_RIGHT;
            else if (t >= 640 && t < 900) w = IN_UP;
        } else if (t >= 470 && t < 620) w = IN_UP;
        else if (t >= 620 && t < 650) w = IN_UP | IN_RIGHT;
        else if (t >= 650 && t < 1000) w = IN_UP;
    }
    if (w & (IN_LEFT | IN_RIGHT)) w |= 0xff;
    if (w & (IN_UP | IN_DOWN)) w |= 0xff00;
    return w;
}

/* One player's screen view: the camera (View struct 0x48bd90 / 0x48bfdc), its tracker, the listener
   (view +0x18 position, +0xe0 gain) and the fly-back skull state. */
typedef struct {
    View *v;
    CamTracker *trk;
    Obj *tracked;
    int32_t pad[3];
    int32_t lis_pos[3], lis_gain;
    bool lis_copy;
    BunkerMode last_mode;
    UiSkull skull;
} PlayerView;

static void player_camera(PlayerView *pv, int team, int32_t drive_H, int dt)
{
    View *v = pv->v;
    Obj *veh = get_team_vehicle(team);
    if (veh != pv->tracked) {
        camera_remove_tracker(v, pv->trk);
        if (veh) pv->trk = camera_add_tracker(v, 0, veh->pos, NULL, 10 << 16, 0x180000,
                                              veh_state(veh)->def->type == VT_HELI ? drive_H + (70 << 16) : drive_H, true, NULL);
        else pv->trk = camera_add_tracker(v, 0, pv->pad, NULL, 0, 0x180000, 0, true, NULL);
        pv->tracked = veh;
    }
    camera_update(v, dt);
    pv->lis_pos[0] = v->camx; pv->lis_pos[1] = v->camy; pv->lis_pos[2] = v->H;   /* view +0x18/+0x1c/+0x20 */
    pv->lis_gain = view_sound_gain(&G.views[team], pv->lis_gain, &pv->lis_copy);
}

/* The view callback (view +0xc0) of one player: ViewModeBunkerSelect / FUN_00404ae0 draw the lift shaft,
   the fly-back and spectate modes their skull screens, the others RenderWorldView. */
static void player_draw(PlayerView *pv, int team, Collect *col, Framebuffer *fb, int scale, int dt)
{
    View *v = pv->v;
    BunkerView *bv = &G.views[team];
    if (bv->mode == BV_FLYBACK && pv->last_mode != BV_FLYBACK) ui_skull_reset(&pv->skull);   /* FUN_00404ee0 */
    pv->last_mode = bv->mode;
    BunkerMode bm = bv->mode;
    if (bm == BV_SELECT || bm == BV_LAUNCH) { ui_select_draw_at(fb, scale, v->x, v->y, v->w, v->h, bv, (uint32_t)g_tick); return; }
    if (bm == BV_SPECTATE) { ui_spectate_draw(fb, scale, v->x, v->y, v->w, v->h, bv, &pv->skull, dt); return; }
    if (bm != BV_FLYBACK || bv->fly_stage < 2) {
        render_clear_objects(v);
        col->v = v;
        col->n = 0;
        obj_foreach_live(collect, col);
        RenderMap rm = { G.cell, (const int8_t (*)[4])G.jitter, on_tower, col };
        v->team = team;
        render_world(v, &rm, fb, scale);
        G.drones[team] = v->enemy_turrets;                /* DAT_00471458: gates the idle drone */
    }
    if (bm == BV_FLYBACK) ui_flyback_draw(fb, scale, v->x, v->y, v->w, v->h, bv, &pv->skull, dt);
}

/* A level in progress: State_Game1P / State_Game2P, one game frame per step, then the end sequence. */
enum { P_RUN, P_FINISH, P_DONE };
struct Play {
    int st, demo;
    const SpriteBank *sb;
    bool two;
    const char *rfm_rel;
    World *w;
    Framebuffer *fb;
    int scale;
    Image8 bar, mid;
    bool have_bar, have_mid;
    int32_t drive_H;
    Input in;
    Clock clk;
    Collect *col;
    int bar_frames;
    /* 1 player */
    View *v;
    int32_t pad[3];
    CamTracker *trk;
    Obj *tracked;
    bool lis_copy;
    BunkerMode last_mode;
    UiSkull skull;
    /* 2 players */
    bool swap, swap_key;
    int demo_phase;
};

/* GameSetup1P 0x40cbb0: both listeners on player 1's camera (view+0x18), 10 units apart, both gains from
   player 1's view (+0xe0). The mixer keeps pointers to these. */
static int32_t lis_pos[3], lis_gain;
static PlayerView pv[2];                                   /* 2 players (the mixer keeps pointers to lis_pos / lis_gain) */

static Play *begin_2p(Play *p);

Play *play_begin(const SpriteBank *sb, const char *rfm_rel)
{
    const char *demo_s = getenv("OPENRF_DEMO");
    int demo = !demo_s ? DEMO_OFF : !strcmp(demo_s, "fire") ? DEMO_TANK : !strcmp(demo_s, "jeep") ? DEMO_JEEP
             : !strcmp(demo_s, "msv") ? DEMO_MSV : !strcmp(demo_s, "heli") ? DEMO_HELI
             : !strcmp(demo_s, "turret") ? DEMO_TURRET : !strcmp(demo_s, "drone") ? DEMO_DRONE
             : !strcmp(demo_s, "sub") ? DEMO_SUB : !strcmp(demo_s, "2p") ? DEMO_2P : !strcmp(demo_s, "2pwin") ? DEMO_2P_WIN
             : !strcmp(demo_s, "2pspectate") ? DEMO_2P_SPECTATE : !strcmp(demo_s, "2pheli") ? DEMO_2P_HELI : DEMO_DRIVE;
    World *w = malloc(sizeof *w);
    ccb_init(sb);
    if (!world_load(w, rfm_rel)) {
        fprintf(stderr, "cannot load level %s\n", rfm_rel);
        free(w);
        return NULL;
    }
    Play *p = calloc(1, sizeof *p);
    p->w = w;
    p->sb = sb;
    p->rfm_rel = rfm_rel;
    p->demo = demo;
    p->fb = plat_fb();
    p->scale = p->fb->w >= 640 ? 2 : 1;
    const char *hs = getenv("OPENRF_CAM_H");
    p->drive_H = (hs ? atoi(hs) : 0) << 16;
    if (w->players > 1) return begin_2p(p);            /* LoadLevelMap: DAT_00443864 = header +0x16 */
    if (demo >= DEMO_2P) p->demo = demo = DEMO_DRIVE;
    /* HudInit(1) runs before ViewEnterBunkerSelect (GameSetup1P). */
    hud_init(1);
    game_hooks.hud = hud_event;
    game_hooks.hud_dirty = hud_mark_dirty;
    if (!game_load(w, rfm_rel)) {
        fprintf(stderr, "cannot load level %s\n", rfm_rel);
        free(w);
        free(p);
        return NULL;
    }
    fprintf(stderr, "level \"%s\" (%s)\n", w->name, rfm_rel);
    ai_level_start();                                  /* drone pool, turret counters, SpawnDrone / SpawnSub hooks */
    if (demo == DEMO_TURRET) {                         /* two enemy towers north-west of the pad */
        int px = G.teams[0].pad->x >> 21, py = G.teams[0].pad->y >> 21;
        set_cell_static(49, &G.cell[(py - 1) * 128 + px - 4], 0, 1);
        set_cell_static(50, &G.cell[(py + 2) * 128 + px - 5], 0, 1);
    } else if (demo == DEMO_DRONE) drone_pool_init(3);
    else if (demo >= DEMO_TANK && demo <= DEMO_HELI) {  /* debug targets west of the pad: bush, building, wall */
        int px = G.teams[0].pad->x >> 21, py = G.teams[0].pad->y >> 21;
        set_cell_static(1, &G.cell[(py + 1) * 128 + px - 2], 0, 2);
        set_cell_static(24, &G.cell[(py + 1) * 128 + px - 4], 0, 1);
        for (int k = 0; k < 3; k++) set_cell_static(45, &G.cell[(py + k) * 128 + px - 6], 0, 1);
        set_cell_static(49, &G.cell[(py - 3) * 128 + px - 2], 0, 1);          /* tower north-west */
    }
    game_hooks.music = on_music;
    game_hooks.sound = sfx_hook;
    game_hooks.sound_step = sfx_play_step;
    ai_hooks.snd_play = ai_snd_play;
    ai_hooks.snd_stereo = ai_snd_stereo;
    ai_hooks.snd_kill = ai_snd_kill;
    p->lis_copy = false;
    lis_gain = 0;
    ai_hooks.listener[0] = ai_hooks.listener[1] = lis_pos;
    sfx_set_time((uint32_t)g_tick);
    sfx_cmd(SFX_LISTENER2, 0, NULL, false);
    sfx_cmd(SFX_LISTENER1, -(10 << 16), lis_pos, false);
    sfx_cmd(SFX_LISTENER2, 10 << 16, lis_pos, false);
    sfx_cmd(SFX_LISTENER_GAIN, 0, &lis_gain, false);
    sfx_cmd(SFX_LISTENER_GAIN, 1, &lis_gain, true);

    memcpy(p->fb->palette, sb->palette, sizeof p->fb->palette);
    p->have_bar = image_load_bmp8(p->scale == 2 ? "ART/1PBSCRH.RFA" : "ART/1PBSCRL.RFA", &p->bar);
    render_init(sb);

    /* Camera: the bunker camera over the pad (View_EnterBunkerCam 0x404ee0), then follow the vehicle.
       The driving height from SpawnVehicle (-170) zooms 2.3x; keep it adjustable until verified. */
    p->v = calloc(1, sizeof *p->v);
    view_init(p->v, 0, 0, 320, 152);
    p->pad[0] = G.teams[0].pad->x; p->pad[1] = G.teams[0].pad->y; p->pad[2] = 0;
    p->trk = camera_add_tracker(p->v, 0, p->pad, NULL, 0, 0x180000, 0, true, NULL);
    camera_snap(p->v, p->pad, 0, 0x180000);
    p->tracked = NULL;
    music_request(MUS_BUNKER, 0x80, 0);   /* FUN_0040cbb0 */
    play_rules_start(p->v, NULL);         /* EndGame hook, game clock, flag camera */

    clock_start(&p->clk);
    p->col = malloc(sizeof *p->col);
    p->bar_frames = 6;
    p->last_mode = G.views[0].mode;
    ui_skull_reset(&p->skull);
    p->st = P_RUN;
    return p;
}

/* Leaving the level (won, lost or Esc): stop everything, then the end sequence. */
static void leave_level(Play *p)
{
    sfx_cmd(SFX_STOP_ALL, 1, NULL, true);                     /* leaving the level: stop everything */
    sfx_cmd(SFX_LISTENER1, 0, NULL, false);
    sfx_cmd(SFX_LISTENER2, 0, NULL, false);
    sfx_cmd(SFX_LISTENER_GAIN, 0, NULL, false);
    sfx_cmd(SFX_LISTENER_GAIN, 1, NULL, true);
    if (p->two) ai_hooks.listener[0] = ai_hooks.listener[1] = NULL;
    game_hooks.sound = NULL;
    game_hooks.sound_step = NULL;
}

static void free_level(Play *p)
{
    free(p->col);
    p->col = NULL;
    if (p->two) for (int k = 0; k < 2; k++) { view_free(pv[k].v); free(pv[k].v); pv[k].v = NULL; }
    else { view_free(p->v); free(p->v); p->v = NULL; }
    if (p->have_bar) image_free(&p->bar);
    if (p->have_mid) image_free(&p->mid);
    p->have_bar = p->have_mid = false;
    free(p->w);
    p->w = NULL;
    game_hooks.hud = NULL;
    game_hooks.hud_dirty = NULL;
}

static Step step_1p(Play *p)
{
    Framebuffer *fb = p->fb;
    int scale = p->scale;
    View *v = p->v;
    int demo = p->demo;
    input_set_menu(0, G.views[0].mode == BV_SELECT);
    input_poll(&p->in, 1);
    int dt = clock_frame(&p->clk);
    if (demo) p->in.cur[0] |= demo_input(p->clk.game_ticks, demo);
    if (demo == DEMO_SUB) {                               /* airborne -> jump near the west edge, fly off */
        Obj *h = get_team_vehicle(0);
        static int sub_phase;
        if (!h) sub_phase = 0;
        else if (sub_phase == 0 && h->pos[2] >= 0x320000) {
            obj_move_to(h, 0x600000, h->pos[1], h->pos[2]);
            h->heading = 0x300000;
            camera_snap(v, h->pos, p->drive_H + (70 << 16), 0x180000);
            sub_phase = 1;
        } else if (sub_phase == 1 && h->pos[0] > -0x600000) p->in.cur[0] |= IN_UP | 0xff00;
    }
    sfx_set_time((uint32_t)g_tick);
    game_frame(dt, p->in.cur[0]);
    ai_frame_end();                                       /* Mus_Director: SUB music */
    play_rules_frame();                                   /* Mus_Director: Flag Discovery / Pickup */
    sfx_set_time((uint32_t)g_tick);

    Obj *veh = get_team_vehicle(0);
    if (veh != p->tracked) {
        camera_remove_tracker(v, p->trk);
        if (veh) p->trk = camera_add_tracker(v, 0, veh->pos, NULL, 10 << 16, 0x180000,
                                             veh_state(veh)->def->type == VT_HELI ? p->drive_H + (70 << 16) : p->drive_H, true, NULL);
        else p->trk = camera_add_tracker(v, 0, p->pad, NULL, 0, 0x180000, 0, true, NULL);
        p->tracked = veh;
    }
    camera_update(v, dt);
    lis_pos[0] = v->camx; lis_pos[1] = v->camy; lis_pos[2] = v->H;   /* view +0x18/+0x1c/+0x20 */
    lis_gain = view_sound_gain(&G.views[0], lis_gain, &p->lis_copy);

    Collect *col = p->col;
    render_clear_objects(v);
    col->v = v;
    col->n = 0;
    obj_foreach_live(collect, col);
    RenderMap rm = { G.cell, (const int8_t (*)[4])G.jitter, on_tower, col };
    v->team = 0;
    hud_radar_update();                                   /* RadarUpdateMovers */
    /* Only the view is cleared: the status bar keeps last frame's pixels and the HUD redraws
       just its dirty widgets, as on the original's un-cleared back buffers. */
    memset(fb->pixels, 0, (size_t)fb->w * (size_t)(v->h * scale < fb->h ? v->h * scale : fb->h));
    /* The view callback: ViewModeBunkerSelect / FUN_00404ae0 draw the lift shaft instead of the world. */
    BunkerMode bm = G.views[0].mode;
    if (bm == BV_FLYBACK && p->last_mode != BV_FLYBACK) ui_skull_reset(&p->skull);   /* FUN_00404ee0 */
    p->last_mode = bm;
    if (bm == BV_SELECT || bm == BV_LAUNCH) ui_select_draw(fb, scale, v->w, v->h, &G.views[0], (uint32_t)g_tick);
    else if (bm != BV_FLYBACK || G.views[0].fly_stage < 2) {   /* 0x405360 / 0x405440: fade cel instead */
        render_world(v, &rm, fb, scale);
        G.drones[0] = v->enemy_turrets;                   /* DAT_00471458: gates the idle drone */
    }
    if (bm == BV_FLYBACK) ui_flyback_draw(fb, scale, v->x, v->y, v->w, v->h, &G.views[0], &p->skull, dt);   /* FUN_00404fb0 */
    effects_reap_drawn();                                 /* draw-time removals of 0x433a60 / 0x435e40 */
    if (p->bar_frames > 0) {                              /* DrawStatusBarBackground (DAT_004410b0 frames) */
        p->bar_frames--;
        if (p->have_bar) draw_statusbar(&p->bar, fb);
        hud_mark_dirty(0, 0x3ff);
    }
    hud_update_all(fb, scale, 1);                         /* HudUpdateAll(1) */
    music_service();
    sfx_frame();                                          /* Snd_QueueCommand(2,0,0,1) + Snd_Service(0x67) */
    plat_present();
    return STEP_FRAME;
}

/* 2-player game: State_Game2P 0x40ec50 / GameFrame2P 0x41aa70 order. GameSetup2P 0x41a750 views
   (320x240 space): player 1 (0,0) 156x149 with screen window (8,12), player 2 (164,0) 156x149 with
   (-8,12); hi-res draws the same layout 2x. Listener 1 = player 1's camera (left channel), listener 2 =
   player 2's (right), each with its own view's gain (+0xe0). */
static Play *begin_2p(Play *p)
{
    World *w = p->w;
    const char *rfm_rel = p->rfm_rel;
    const SpriteBank *sb = p->sb;
    p->two = true;
    if (p->demo && p->demo < DEMO_2P) p->demo = DEMO_2P;
    /* GameSetup2P: HudInit(2) before the two ViewEnterBunkerSelect calls. */
    hud_init(2);
    game_hooks.hud = hud_event;
    game_hooks.hud_dirty = hud_mark_dirty;
    if (!game_load(w, rfm_rel)) {
        fprintf(stderr, "cannot load level %s\n", rfm_rel);
        free(w);
        free(p);
        return NULL;
    }
    fprintf(stderr, "level \"%s\" (%s), 2 players\n", w->name, rfm_rel);
    ai_level_start();
    game_hooks.music = on_music;
    game_hooks.sound = sfx_hook;
    game_hooks.sound_step = sfx_play_step;
    ai_hooks.snd_play = ai_snd_play;
    ai_hooks.snd_stereo = ai_snd_stereo;
    ai_hooks.snd_kill = ai_snd_kill;

    memset(pv, 0, sizeof pv);
    ai_hooks.listener[0] = pv[0].lis_pos;
    ai_hooks.listener[1] = pv[1].lis_pos;
    sfx_set_time((uint32_t)g_tick);
    sfx_cmd(SFX_LISTENER2, 0, pv[1].lis_pos, false);       /* Snd_QueueCommand(7, 0, 0x48bff4) */
    sfx_cmd(SFX_LISTENER1, 0, pv[0].lis_pos, false);       /* (6, 0, view 1 +0x18) */
    sfx_cmd(SFX_LISTENER2, 0, pv[1].lis_pos, false);       /* (7, 0, view 2 +0x18) */
    sfx_cmd(SFX_LISTENER_GAIN, 0, &pv[0].lis_gain, false); /* (8, 0, view 1 +0xe0) */
    sfx_cmd(SFX_LISTENER_GAIN, 1, &pv[1].lis_gain, true);  /* (8, 1, view 2 +0xe0) */

    Framebuffer *fb = p->fb;
    int scale = p->scale;
    memcpy(fb->palette, sb->palette, sizeof fb->palette);
    /* LoadStatusBarArt: 2pBScr{L,H}.rfa + 2pMScr.rfa */
    p->have_bar = image_load_bmp8(scale == 2 ? "ART/2PBSCRH.RFA" : "ART/2PBSCRL.RFA", &p->bar);
    p->have_mid = image_load_bmp8("ART/2PMSCR.RFA", &p->mid);
    render_init(sb);

    for (int k = 0; k < 2; k++) {
        PlayerView *q = &pv[k];
        q->v = calloc(1, sizeof *q->v);
        view_init(q->v, k ? 0xa4 : 0, 0, 0x9c, 0x95);
        view_set_window(q->v, k ? -8 : 8, 0xc);            /* Camera_SetScreenWindow(view, +-8, 12, 0x94, 0x89) */
        q->pad[0] = G.teams[k].pad->x; q->pad[1] = G.teams[k].pad->y; q->pad[2] = 0;
        q->trk = camera_add_tracker(q->v, 0, q->pad, NULL, 0, 0x180000, 0, true, NULL);
        camera_snap(q->v, q->pad, 0, 0x180000);
        q->last_mode = G.views[k].mode;
        ui_skull_reset(&q->skull);
    }
    music_request(MUS_BUNKER, 0x80, 0);                    /* OnMenuCommand 0x40cbb0: DAT_00480e9c = 2 */
    play_rules_start(pv[0].v, pv[1].v);

    clock_start(&p->clk);
    p->col = malloc(sizeof *p->col);
    p->bar_frames = 6;
    p->swap = p->swap_key = false;
    p->demo_phase = 0;
    p->st = P_RUN;
    return p;
}

static Step step_2p(Play *p)
{
    Framebuffer *fb = p->fb;
    int scale = p->scale, demo = p->demo;
    /* Alt+3 "Swap Sides" (menu 0x415, DAT_0043fc9c): the two input devices change players. */
    bool k3 = input_ui(UI_SWAP);
    if (k3 && !p->swap_key) { p->swap = !p->swap; fprintf(stderr, "swap sides: %s\n", p->swap ? "on" : "off"); }
    p->swap_key = k3;
    for (int k = 0; k < 2; k++) input_set_menu(k, G.views[k ^ p->swap].mode == BV_SELECT);
    input_poll(&p->in, 2);
    int dt = clock_frame(&p->clk);
    uint32_t w0 = p->in.cur[p->swap], w1 = p->in.cur[!p->swap];
    if (demo) {
        w0 |= demo_input_2p(p->clk.game_ticks, demo, 0);
        w1 |= demo_input_2p(p->clk.game_ticks, demo, 1);
        Obj *v0 = get_team_vehicle(0), *v1 = get_team_vehicle(1);
        if (demo == DEMO_2P_WIN && p->demo_phase == 0 && v0 && v1 && p->clk.game_ticks > 700) {
            rules_end_game(1);                         /* player 2 (green) wins */
            p->demo_phase = 1;
        }
        if (demo == DEMO_2P_SPECTATE && p->demo_phase == 0 && v0 && p->clk.game_ticks > 420) {
            for (int k = 0; k < 4; k++) TEAM_STOCK(&G.teams[0], k) = 0;
            veh_damage(v0, NULL, 0x7f0000);            /* last vehicle lost -> fly-back -> spectate */
            p->demo_phase = 1;
        }
    }
    sfx_set_time((uint32_t)g_tick);
    game_frame_2p(dt, w0, w1);
    ai_frame_end();                                    /* Mus_Director: SUB music */
    play_rules_frame();                                /* Mus_Director: flag cues, both players in the bunker */
    sfx_set_time((uint32_t)g_tick);
    for (int k = 0; k < 2; k++) player_camera(&pv[k], k, p->drive_H, dt);
    hud_radar_update();                                /* RadarUpdateMovers */
    for (int k = 0; k < 2; k++) {                      /* only the view rectangles are redrawn */
        const View *v = pv[k].v;
        for (int y = v->y * scale; y < (v->y + v->h) * scale && y < fb->h; y++)
            memset(fb->pixels + (size_t)y * fb->w + v->x * scale, 0, (size_t)(v->w * scale));
    }
    for (int k = 0; k < 2; k++) player_draw(&pv[k], k, p->col, fb, scale, dt);   /* (*0x48be50)(), (*0x48c09c)() */
    effects_reap_drawn();
    if (p->bar_frames > 0) {                           /* DrawStatusBarBackground (DAT_004410b0 frames) */
        p->bar_frames--;
        if (p->have_mid) draw_divider(&p->mid, fb, scale == 2);
        if (p->have_bar) draw_statusbar(&p->bar, fb);
        hud_mark_dirty(0, 0x3ff);
        hud_mark_dirty(1, 0x3ff);
    }
    hud_update_all(fb, scale, 2);                      /* HudUpdateAll(2) */
    music_service();
    sfx_frame();
    plat_present();
    return STEP_FRAME;
}

Step play_step(Play *p)
{
    switch (p->st) {
    case P_RUN:
        if (G.winner == -2 && !input_ui(UI_BACK)) return p->two ? step_2p(p) : step_1p(p);
        leave_level(p);                                /* won / lost, or Esc */
        play_rules_finish_begin(p->rfm_rel);           /* EndGame fade, win sequence, high score */
        p->st = P_FINISH;
        return STEP_AGAIN;
    case P_FINISH: {
        Step s = play_rules_finish_step();
        if (s != STEP_DONE) return s;
        free_level(p);
        p->st = P_DONE;
        return STEP_DONE;
    }
    }
    return STEP_DONE;
}

void play_end(Play *p)
{
    if (!p) return;
    if (p->st == P_RUN) leave_level(p);
    if (p->st != P_DONE) { play_rules_cancel(); free_level(p); }
    for (int k = 0; k < 2; k++) input_set_menu(k, false);
    free(p);
}
