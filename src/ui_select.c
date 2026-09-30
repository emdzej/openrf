/* Bunker vehicle-select overlay: what ViewModeBunkerSelect 0x404570 and the launch mode
   FUN_00404ae0 draw instead of the world view: FUN_00404800 (lift shaft panel, vehicle icons,
   highlight, animated arrow, lift platform) on top of FUN_00403fa0 (surface strip with random
   tiles, clouds, dirt), plus the fade cel (Cel_SetupFullscreenFade 0x407b90 + FUN_00407d80). */
#include "ui_select.h"
#include "render/ccb.h"
#include "game/game.h"
#include "exe.h"

/* CCB indices (template offsets / 0x44 in the original). */
enum {
    CEL_STRIP_FILL = 2141,   /* 0x238b4: also the full-screen fade cel */
    CEL_SURFACE_MID = 2084,  /* 0x22990: tile above the shaft */
    CEL_SURFACE = 2085,      /* 0x229d4 + RandRange(2) */
    CEL_CLOUD = 2087,        /* 0x22a5c + RandRange(3) */
    CEL_DIRT = 2080,         /* 0x22880 + RandRange(4) */
    CEL_PANEL = 2075,        /* 0x2272c lift shaft / hangar */
    CEL_PLATFORM = 2076,     /* 0x22770 */
    CEL_GLOW = 2077,         /* 0x227b4 mode 5 light */
    CEL_HIGHLIGHT = 2078,    /* 0x227f8 */
    CEL_CABLE = 2079,        /* 0x2283c stretched from the platform to the bottom */
    CEL_ARROW = 2091,        /* 0x22b6c + (tick >> 4) % 3 */
    CEL_ICON = 2094,         /* 0x22c38 + team + 2 * type: side views */
};

/* Menu grid 0x43f1d8 (10 dwords per type): icon, arrow, glow, highlight offsets (16.16);
   dword 8 = zoom script, dword 9 = nav bytes (bunker_menu_nav). */
static int32_t menu[4][8];                                   /* read from RFIRE.BIN (ui_tables_load) */

/* MSVC rand (0x438610) on a private seed: FUN_00438600 reseeds the global one here, which is
   only used for these tiles by the time the select screen runs (the original then restores a
   random seed; our sim keeps its own RNG). */
static uint32_t seed;
static int rnd(void) { seed = seed * 0x343fd + 0x269ec3; return (int)((seed & 0x7fff0000) >> 16); }
static int ui_rand_range(int n) { return (int)((uint32_t)(rnd() & 0x7fff) * 2 * (uint32_t)n >> 16); }

typedef struct { Framebuffer *fb; const ClipRect *clip; int scale; } Ctx;

static void put(const Ctx *x, int idx, int32_t px, int32_t py)
{
    Ccb c = ccb_get(idx);
    c.v[CCB_X] = px;
    c.v[CCB_Y] = py;
    ccb_draw(x->fb, x->clip, x->scale, &c);
}

/* FUN_00403fa0: surface strip above the shaft; returns the panel top (16.16). */
static int32_t draw_surface(const Ctx *x, int team, int vw, int vh, int32_t cx, int32_t top)
{
    seed = 0x1abfac + (uint32_t)team;                        /* DAT_0043ef44 + team */
    Ccb c = ccb_get(CEL_STRIP_FILL);
    ccb_set_scale(&c, vw << 16, top);
    ccb_draw(x->fb, x->clip, x->scale, &c);                  /* (PLUT <- CCB 2084's, unused by mode 0) */
    const Sprite *mid = &ccb_bank()->spr[CEL_SURFACE_MID];
    int32_t mx = cx + mid->w * -0x8000;
    put(x, CEL_SURFACE_MID, mx, 0);
    put(x, CEL_SURFACE_MID, mx, top);
    int32_t step = mid->w * 0x10000, bottom = mid->h * 0x10000 + top;
    for (int32_t i = mx; i > -1;) {
        i -= step;
        put(x, CEL_SURFACE + ui_rand_range(2), i, 0);
        put(x, CEL_SURFACE + ui_rand_range(2), i, top);
    }
    for (int32_t i = mx + step; i < vw * 0x10000; i += step) {
        put(x, CEL_SURFACE + ui_rand_range(2), i, 0);
        put(x, CEL_SURFACE + ui_rand_range(2), i, top);
    }
    int32_t cstep = ccb_bank()->spr[CEL_CLOUD].w * 0x10000;
    for (int32_t i = 0; i < vw << 16; i += cstep) put(x, CEL_CLOUD + ui_rand_range(3), i, top - 0xc0000);
    const Sprite *dirt = &ccb_bank()->spr[CEL_DIRT];
    for (int32_t i = 0; i < vw << 16; i += dirt->w * 0x10000) {
        seed = 0x1abfac + (uint32_t)i + (uint32_t)team;
        for (int32_t j = bottom - 0x60000; j < vh * 0x10000; j += dirt->h * 0x10000)
            put(x, CEL_DIRT + ui_rand_range(4), i, j);
    }
    return bottom - 0x1a0000;
}

void ui_select_draw(Framebuffer *fb, int scale, int vw, int vh, const BunkerView *bv, uint32_t tick)
{
    ui_select_draw_at(fb, scale, 0, 0, vw, vh, bv, tick);
}

/* The view bitmap's clip rectangle (ViewInit: Bitmap_SetClipOrigin/Width/Height), in framebuffer pixels. */
static ClipRect view_clip(const Framebuffer *fb, int scale, int vx, int vy, int vw, int vh)
{
    ClipRect clip = { vx * scale, vy * scale, (vx + vw) * scale - 1, (vy + vh) * scale - 1 };
    if (clip.x1 >= fb->w) clip.x1 = fb->w - 1;
    if (clip.y1 >= fb->h) clip.y1 = fb->h - 1;
    return clip;
}

void ui_select_draw_at(Framebuffer *fb, int scale, int vx, int vy, int vw, int vh, const BunkerView *bv, uint32_t tick)
{
    const SpriteBank *sb = ccb_bank();
    if (!sb) return;
    ClipRect clip = view_clip(fb, scale, vx, vy, vw, vh);
    if (clip.x1 >= fb->w) clip.x1 = fb->w - 1;
    if (clip.y1 >= fb->h) clip.y1 = fb->h - 1;
    Ctx x = { fb, &clip, scale };
    const Team *tm = bv->tm;
    int team = bv->team;
    int pw = sb->spr[CEL_PANEL].w;
    int32_t x0 = (vw - pw) * 0x8000;
    int32_t y0 = draw_surface(&x, team, vw, vh, pw * 0x8000 + x0, 0x100000);
    put(&x, CEL_PANEL, x0, y0);
    for (int i = 0; i < 4; i++) {
        const int32_t *m = menu[i];
        int icon = CEL_ICON + team + 2 * i;
        bool sel = bv->sel == i;
        if (!sel && !TEAM_STOCK(tm, i)) continue;
        if (sel) put(&x, CEL_HIGHLIGHT, m[6] + x0, m[7] + y0);
        Ccb c = ccb_get(icon);
        c.v[CCB_X] = (sel ? bv->cam_d4 : 0) + m[0] + x0 + 0x20000;
        c.v[CCB_Y] = (sel ? bv->cam_d8 : 0) + m[1] + y0 + 0x30000;
        c.v[CCB_HDX] >>= 1;
        c.v[CCB_VDY] >>= 1;
        ccb_draw(fb, &clip, scale, &c);
        if (!sel) continue;
        Ccb a = ccb_get(CEL_ARROW + (int)((tick >> 4) % 3));
        a.v[CCB_HDX] -= 0x4000;
        a.v[CCB_VDY] -= 4;
        a.v[CCB_X] = m[2] + x0 + 0x6666;
        a.v[CCB_Y] = m[3] + y0 + 0x10000;
        if (m[2] > 0x3c0000) {                                /* right column: mirrored */
            a.v[CCB_HDX] = 0x8000 - a.v[CCB_HDX];
            a.v[CCB_X] += (a.w + 1) * 0x10000;
        }
        ccb_draw(fb, &clip, scale, &a);
        put(&x, CEL_GLOW, m[4] + x0, m[5] + y0);             /* PIXC 0x1f801f80 (3DO only) */
    }
    /* Lift cable (explicit quad from the platform down past the bottom) and platform. */
    int32_t lx = x0 + 0x338000, ly = bv->cam_d0 + y0;
    Ccb cab = ccb_get(CEL_CABLE);
    int32_t rx = sb->spr[CEL_CABLE].w * 0x10000 + lx, by = (vh + 2) * 0x10000;
    int32_t q[8] = { lx, ly, rx, ly, rx, by, lx, by };
    for (int k = 0; k < 8; k++) cab.v[k] = q[k];
    cab.flags |= CCB_F_EXPLICIT;
    ccb_draw(fb, &clip, scale, &cab);
    put(&x, CEL_PLATFORM, lx, ly);
    /* Fade-in: mode 8 over the view. FUN_00407d80 overwrites the level with fade >> 10, so on the PC
       Cel_ShadeRect always picks T_SHADE[0] (the visible fade is the palette ramp, SetFadeTarget). */
    if (bv->fade < 0x10000) {
        Ccb f = ccb_get(CEL_STRIP_FILL);
        int32_t q2[8] = { 0, 0, vw << 16, 0, vw << 16, vh << 16, 0, vh << 16 };
        for (int k = 0; k < 8; k++) f.v[k] = q2[k];
        f.flags |= CCB_F_EXPLICIT;
        f.mode = 8;
        f.p1 = (bv->fade < 0 ? 0 : bv->fade) >> 10;
        ccb_draw(fb, &clip, scale, &f);
    }
}

/* ---- fly-back / spectate overlay ---------------------------------------------------------------- */
static int8_t skull_frames[64];                              /* 0x43f288, indexed by view +0xd8 >> 16 */
static int32_t skull_verts[4][2];                            /* 0x43f2c8 (x, y, z = 0), corner order 0x43f2f8 */
/* 0x43f320 + type * 0x1c: icon cel, (unused), dy, x step, row step, cross offset x / y */
static int32_t lost_icons[4][7];
static int32_t spectate_icons[4];                            /* 0x43f390 */
static int8_t spectate_at[4][2];                             /* 0x43f3a0 */
static void ui_tables_load(void)
{
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 8; k++) menu[i][k] = exe_s32(0x43f1d8 + 0x28 * (uint32_t)i + 4 * (uint32_t)k);
        for (int k = 0; k < 2; k++) skull_verts[i][k] = exe_s32(0x43f2c8 + 12 * (uint32_t)i + 4 * (uint32_t)k);
        for (int k = 0; k < 7; k++) lost_icons[i][k] = exe_s32(0x43f320 + 0x1c * (uint32_t)i + 4 * (uint32_t)k);
        spectate_icons[i] = exe_s32(0x43f390 + 4 * (uint32_t)i);
        for (int k = 0; k < 2; k++) spectate_at[i][k] = exe_s8(0x43f3a0 + 2 * (uint32_t)i + (uint32_t)k);
    }
    for (int i = 0; i < 64; i++) skull_frames[i] = exe_s8(0x43f288 + (uint32_t)i);
}
EXE_LOADER(ui_tables_load)
enum { CEL_SKULL = 0x84d /* 2125: the red cross; skulls 2126.. (+7 for side 0) */, CEL_SKULL_H = 2126 };

/* FUN_00407df0: the cel is skipped while the level (>> 12) - 1 < 1; the rest is 3DO PIXC only. */
static void put_faded(const Ctx *x, int idx, int32_t px, int32_t py, int32_t level)
{
    if ((level >> 12) - 1 < 1) return;
    put(x, idx, px, py);
}

static void skull_step(UiSkull *a, int32_t dt)               /* FUN_00404fb0: zoom and spin in */
{
    if (a->scale < 0x13333 && (a->scale += dt * 0x666) > 0x13333) a->scale = 0x13333;
    if (a->scale != 0x13333 || a->angle != 0) {
        uint32_t u = (uint32_t)a->angle + (uint32_t)(dt * 0x28000);
        a->angle = ((int32_t)u < 0x400000 || a->scale < 0x13333) ? (int32_t)(u & 0x3fffff) : 0;
    }
}

/* FUN_00404fb0(view, 0x10000): the other side's skull in the middle of the view (animation frame from
   view +0xd8) and, while +0xd8 > 0, the starting stock of the lost type with the lost ones crossed out. */
static void draw_skull(const Ctx *x, int vw, int vh, const BunkerView *bv, UiSkull *a, int32_t dt, int32_t d8, int type)
{
    skull_step(a, dt);
    int32_t cx = (vw / 2) << 16, cy = (vh / 2) << 16;       /* view +0x10 / +0x14 */
    int f = skull_frames[(d8 >> 16) & 63];
    const int32_t *yaw = render_yaw_matrix(a->angle >> 16);
    Ccb c = ccb_get(f + CEL_SKULL + (bv->team == 0 ? 7 : 0));
    for (int k = 0; k < 4; k++) {                           /* diag(s, s, 0) x yaw, Mat_TransformVerts */
        int32_t m0 = fixmul(a->scale, yaw[0]), m1 = fixmul(a->scale, yaw[1]);
        int32_t m3 = fixmul(a->scale, yaw[3]), m4 = fixmul(a->scale, yaw[4]);
        c.v[k * 2] = fixmul(skull_verts[k][0], m0) + fixmul(skull_verts[k][1], m3) + cx;
        c.v[k * 2 + 1] = fixmul(skull_verts[k][0], m1) + fixmul(skull_verts[k][1], m4) + cy;
    }
    c.flags |= CCB_F_EXPLICIT;
    ccb_draw(x->fb, x->clip, x->scale, &c);
    if (d8 <= 0 || type < 0 || type > 3) return;
    int32_t lv;                                             /* view +0xcc: icon fade-in (negative: fading out) */
    if (bv->mode == BV_SPECTATE && bv->sel < 0) lv = -bv->sel;
    else {
        if ((a->icons += dt * 0x51e) > 0x10000) a->icons = 0x10000;
        lv = a->icons;
    }
    const int32_t *t = lost_icons[type];
    int n = G.stock0[bv->team & 1][type] > 8 ? 8 : G.stock0[bv->team & 1][type];
    int have = TEAM_STOCK(bv->tm, type), k = n;
    int32_t y = ccb_bank()->spr[CEL_SKULL_H].h * 0x8000 + t[2] + cy;
    while (n > 0) {
        int row = n > 3 ? 4 : n;
        n -= row;
        int32_t px = (cx * 2 - t[3] * row) >> 1;
        while (row-- > 0) {
            k--;
            put_faded(x, t[0], px, y, lv);
            if (have <= k) put_faded(x, CEL_SKULL, t[5] + px, t[6] + y, lv);
            px += t[3];
        }
        y += t[4];
    }
}

void ui_flyback_draw(Framebuffer *fb, int scale, int vx, int vy, int vw, int vh, const BunkerView *bv, UiSkull *a,
                     int32_t dt)
{
    if (!ccb_bank()) return;
    ClipRect clip = view_clip(fb, scale, vx, vy, vw, vh);
    Ctx x = { fb, &clip, scale };
    if (bv->fly_stage >= 2) {                               /* 0x405360 / 0x405440: fade cel over the view */
        Ccb f = ccb_get(CEL_STRIP_FILL);                    /* Cel_SetupFullscreenFade 0x407b90 */
        int32_t q[8] = { 0, 0, vw << 16, 0, vw << 16, vh << 16, 0, vh << 16 };
        for (int k = 0; k < 8; k++) f.v[k] = q[k];
        f.flags |= CCB_F_EXPLICIT;
        f.mode = 8;
        f.p1 = 0xffff - (bv->fade < 0 ? 0 : bv->fade > 0xffff ? 0xffff : bv->fade);
        ccb_draw(fb, &clip, scale, &f);
    }
    draw_skull(&x, vw, vh, bv, a, dt, bv->cam_d8, bv->last_type);
}

void ui_spectate_draw(Framebuffer *fb, int scale, int vx, int vy, int vw, int vh, const BunkerView *bv, UiSkull *a,
                      int32_t dt)
{
    if (!ccb_bank()) return;
    ClipRect clip = view_clip(fb, scale, vx, vy, vw, vh);
    Ctx x = { fb, &clip, scale };
    Ccb f = ccb_get(CEL_STRIP_FILL);                        /* the fade cel (on a black view) */
    int32_t q[8] = { 0, 0, vw << 16, 0, vw << 16, vh << 16, 0, vh << 16 };
    for (int k = 0; k < 8; k++) f.v[k] = q[k];
    f.flags |= CCB_F_EXPLICIT;
    f.mode = 8;
    f.p1 = 0xffff - (bv->fade < 0 ? 0 : bv->fade > 0xffff ? 0xffff : bv->fade);
    ccb_draw(fb, &clip, scale, &f);
    if (bv->fly_stage == 0) {                               /* FUN_004054f0: still the fly-back picture */
        draw_skull(&x, vw, vh, bv, a, dt, bv->cam_d8, bv->last_type);
        return;
    }
    draw_skull(&x, vw, vh, bv, a, dt, 0, -1);               /* FUN_004055f0: +0xd8 = 0 */
    int32_t cx = (vw / 2) << 16, cy = (vh / 2) << 16;
    int32_t bx = cx - 0x230000, by = ccb_bank()->spr[CEL_SKULL_H].h * 0x8000 + cy + 0xa0000;   /* 0x43f310 / 0x43f314 */
    for (int i = 0; i < 4; i++) {                           /* every vehicle type crossed out, mode 0xe */
        int32_t px = bx + (spectate_at[i][0] << 16), py = by + (spectate_at[i][1] << 16);
        Ccb c = ccb_get(spectate_icons[i]);
        c.v[CCB_X] = px; c.v[CCB_Y] = py; c.mode = 0xe;
        ccb_draw(fb, &clip, scale, &c);
        c = ccb_get(CEL_SKULL);
        c.v[CCB_X] = px + 0x40000; c.v[CCB_Y] = py + 0x70000; c.mode = 0xe;   /* 0x43f308 / 0x43f30c */
        ccb_draw(fb, &clip, scale, &c);
    }
}
