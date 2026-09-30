/* HUD widgets (per-player block 0x472060, 0x108 bytes, template 0x44abb8) and the radar image
   DAT_00440c64. Like the original, widgets are drawn only when their dirty bit is set, straight
   into the (persistent) status-bar area of the frame buffer. */
#include "hud.h"
#include "game/rules.h"
#include "render/ccb.h"
#include "game/game.h"
#include "game/vehicle.h"
#include "exe.h"
#include <string.h>

/* Widget kinds: table 0x44acc0 (1..9) plus the two template-only draw functions. */
enum {
    K_NONE, K_CEL /* Hud_DrawCelWidget 0x421170 */, K_SLIDE_IN /* 0x421730 */, K_SLIDE_OUT /* 0x421810 */,
    K_BAR /* 0x421920 */, K_FUEL /* 0x421af0 */, K_RADAR /* 0x421e40 */, K_SEGS /* 0x422320 */,
    K_BEACON /* 0x4224d0 */, K_HELI /* 0x422570 */,
    K_FRAME = 100 /* Hud_DrawFrame 0x4211d0 */, K_WCOUNT /* Hud_DrawWeaponCounts 0x421220 */,
};

typedef struct {
    int kind;
    uint32_t chain;               /* +4: bits also drawn after this one */
    int32_t p8, pc, p10, p14;     /* +8/+0xc/+0x10/+0x14 as integers */
    const WeaponDef *wd;          /* +8 for gauges */
    const int32_t *ammo;          /* +0xc for K_BAR / K_SEGS (VehState slot +0xc) */
    Obj *obj;                     /* +0xc for K_RADAR / K_BEACON */
    const uint32_t *flags;        /* +8 for K_HELI (obj+0xc) */
    int32_t scale;                /* WeaponDef +0x30, written by the gauge init FUN_004218f0 */
    uint8_t seg[2];               /* K_SEGS per-buffer counts (stored in +8 in the original) */
    int32_t r[11];                /* K_RADAR: copy of def[0x9c..0xa6] (+0x24 is written) */
    const uint8_t *arrow;         /* K_RADAR: def[0xa6] 8 x {x0,y0,x1,y1} */
    int team;
} Slot;

typedef struct {
    uint32_t dirty[2];            /* +0/+4 per screen buffer */
    int32_t x, y, h;              /* +8/+0xc/+0x10 */
    Slot s[10];                   /* +0x14 */
    int32_t y104;                 /* +0x104 current top of sliding panels */
    int team;
} Hud;

static Hud huds[2];
static int hud_n = 1, buf;        /* buf = DAT_0045f3a0 */
static uint32_t cur_mask;         /* DAT_004587f8 */
static int force_ticks;           /* DAT_0044389c */
static int32_t clip_h;            /* DAT_00458884 */
static uint8_t radar[128 * 128];  /* DAT_00440c64 */

/* ---------------------------------------------------------------- data tables (read from RFIRE.BIN) */
static int32_t seg_x[18], seg_y[18];               /* 0x44ab28 / 0x44ab70: weapon-count segment positions */
static void hud_tables_load(void)
{
    for (int i = 0; i < 18; i++) { seg_x[i] = exe_s32(0x44ab28 + 4 * (uint32_t)i); seg_y[i] = exe_s32(0x44ab70 + 4 * (uint32_t)i); }
}
EXE_LOADER(hud_tables_load)
/* RGB555 "PLUT" words 0x44aae8..: a PLUT pointer p reads its colour at p+2. */
#define GAUGE_PLUT(va) ((const uint8_t *)exe_ptr(va))
/* Enemy-arrow sub-rects per direction (def[0xa6] points at 8 x {x0,y0,x1,y1} bytes: tank 0x44b2f8,
   MSV 0x44b318, heli 0x44b2d8). */
static const uint8_t *arrow_rects(uint32_t va)
{
    return va == 0x44b2f8 || va == 0x44b318 || va == 0x44b2d8 ? (const uint8_t *)exe_ptr(va) : NULL;
}
/* BNO radar colours (BNO record +0x34: low byte team 0 / unowned, byte 2 owned). */
static uint32_t bno_radar(int b) { return b >= 0 && b < OBJ_COUNT ? obj_table[b].radar : 0; }
/* Radar blip shapes {dx, dy, colour team 0, colour team 1} (int8 x 4): 9-dot box 0x44b338 (icons 0x44b360 /
   0x44b370), 5-dot plus 0x44b380 (icon 0x44b398), flag 0x44e788. */
typedef struct { int n; const int8_t (*p)[4]; } Blip;
#define BLIP_AT(va) ((const int8_t (*)[4])exe_ptr(va))

/* ---------------------------------------------------------------- slots */
static void mark(Hud *h, uint32_t m) { h->dirty[0] |= m; h->dirty[1] |= m; }
static void mark_next(Hud *h, uint32_t m) { h->dirty[buf ^ 1] |= m; }

void hud_mark_dirty(int p, uint32_t m) { if (p >= 0 && p < 2) mark(&huds[p], m); }

static void set_template(Hud *h, int i)          /* HudSetSlot(.., -1): template 0x44abcc */
{
    Slot *s = &h->s[i];
    memset(s, 0, sizeof *s);
    s->team = h->team;
    switch (i) {
    case 0: s->kind = K_FRAME; s->chain = 0xe; s->p8 = 0; s->pc = 0x10000; break;
    case 1: s->kind = K_CEL; s->p8 = 0x794; s->pc = (int32_t)0xfffd0000; s->p10 = (int32_t)0xfffe0000; break;
    case 2: s->kind = K_WCOUNT; break;
    case 3: s->kind = K_CEL; s->p8 = 0x795; s->pc = 0xd0000; s->p10 = 0x30000; break;
    default: break;
    }
}

/* HudSetSlot 0x422840 for kind >= 0 (the caller fills the typed pointers first); runs the init
   function from 0x44ace8. */
static Slot *set_slot(Hud *h, int i, int kind, int32_t p8, int32_t pc, int32_t p10)
{
    Slot *s = &h->s[i];
    if (kind > 9 || kind < 0) kind = 0;
    s->kind = kind;
    s->p8 = p8; s->pc = pc; s->p10 = p10; s->p14 = 0;
    s->team = h->team;
    switch (kind) {
    case K_SLIDE_IN:                                          /* 0x4216f0 */
        s->p8 += 0x797;
        s->pc = 0;
        s->p10 = h->h;
        h->y104 = h->y + h->h;
        s->chain = 0xe9;
        break;
    case K_BAR: case K_FUEL:                                  /* FUN_004218f0 */
        s->p10 = 0;
        if (s->wd && s->wd->ammo) s->scale = ((s->wd->hud_rect[3] - s->wd->hud_rect[2]) + 0x10000) / s->wd->ammo;
        break;
    case K_RADAR:                                             /* FUN_00421ce0 */
        if (s->obj && s->obj->p60) {
            int32_t hp = veh_state(s->obj)->hp;
            s->p14 = hp ? fixdiv(0x100000, hp) : 0;
        }
        if (s->obj && s->obj->tm) s->obj->tm->arrow_dir = -1;
        break;
    case K_SEGS:                                              /* FUN_00422300 */
        s->seg[0] = s->seg[1] = 0;
        s->p10 = 0;
        break;
    }
    mark(h, 1u << i);
    return s;
}

void hud_init(int nplayers)                                   /* HudInit 0x422640 */
{
    hud_n = nplayers < 2 ? 1 : 2;
    const SpriteBank *sb = ccb_bank();
    int32_t hh = ((sb ? sb->spr[1943].h : 56) - 1) * 0x10000;  /* CCB 0x20420: tank dashboard */
    for (int p = 0; p < 2; p++) {
        Hud *h = &huds[p];
        memset(h, 0, sizeof *h);
        h->team = p;
        h->dirty[0] = h->dirty[1] = 0xf;
        for (int i = 0; i < 10; i++) set_template(h, i);
        h->x = 0x1a0000; h->y = 0xac0000; h->h = hh;
    }
    huds[0].dirty[0] = huds[0].dirty[1] = 0xf;
    if (hud_n == 1) { huds[0].x = 0x570000; huds[0].y = 0xa80000; }   /* (alt layout 0x740000, 0xb90000) */
    else { huds[0].x = 0xe0000; huds[0].y = 0xa20000; huds[1].x = 0xa50000; huds[1].y = 0xa20000; }
    for (int p = 0; p < 2; p++) huds[p].y104 = huds[p].y;
    clip_h = (huds[0].y + hh) >> 16;
    buf = 0;
    force_ticks = 0;
}

void hud_event(int ev, int team, Obj *o)
{
    if (team < 0 || team >= hud_n) return;
    Hud *h = &huds[team];
    BunkerView *bv = &G.views[team];
    switch (ev) {
    case GHUD_SELECT:                                       /* ViewEnterBunkerSelect 0x404d30 */
        if (h->s[4].kind) {
            Slot old = h->s[4];
            set_slot(h, 4, K_SLIDE_OUT, old.p8, h->h, old.p10);
            set_template(h, 2);
            mark(h, 1u << 2);
        }
        h->s[2].pc = bv->sel;
        mark(h, 0xf);
        break;
    case GHUD_DASH:                                         /* FUN_004043a0 */
        for (int i = 5; i <= 9; i++) set_slot(h, i, 0, 0, 0, 0);
        set_slot(h, 4, K_SLIDE_IN, bv->sel, 0, 0);
        break;
    case GHUD_VEH_INIT:
        break;                                                /* radar blips are gathered per frame, see radar_movers */
    case GHUD_VEH_START: {                                  /* VehicleUpdate 0x428490 first frame */
        VehState *s = veh_state(o);
        const VehicleDef *d = s->def;
        Slot *t;
        t = &h->s[6]; t->wd = &d->weapon[0]; t->ammo = &s->slot[0].ammo; set_slot(h, 6, d->weapon[0].hud_kind, 0, 1, 0);
        t = &h->s[7]; t->wd = &d->weapon[1]; t->ammo = &s->slot[1].ammo; set_slot(h, 7, d->weapon[1].hud_kind, 0, 1, 0);
        t = &h->s[9];
        t->obj = o;
        {
            const int32_t r[11] = { d->hud9[0], d->hud9[1], d->hud9[2], d->hud9[3], d->hud9[4], d->hud_icon[0],
                                    d->hud_icon[1], d->unk_a3[0], d->unk_a3[1], d->unk_a3[2], 0 };
            memcpy(t->r, r, sizeof r);
        }
        t->arrow = arrow_rects(d->arrow_enable);
        set_slot(h, 9, d->hud9[0], 0, 0, d->hud_icon[0] + o->team);
        t->pc = 1;
        t = &h->s[5]; t->wd = &d->weapon[2];
        set_slot(h, 5, d->weapon[2].hud_kind, 0, 0, 0);
        t->pc = (s->fuel >> 16) * t->scale;
        if (d->type == VT_HELI) {
            h->s[8].flags = &o->flags;
            set_slot(h, 8, K_HELI, 1, 0, 0);
            h->s[7].p14 = 1;                                  /* local_c[0x34] = 1: missile bar colours */
        }
        break;
    }
    case GHUD_VEH_GONE:                                     /* VehicleDestroy 0x4283c0 */
        h->s[6].ammo = NULL; h->s[6].pc = 0;
        h->s[7].ammo = NULL; h->s[7].pc = 0;
        h->s[9].obj = NULL; h->s[9].pc = 0;
        h->s[5].pc = 0;
        h->s[8].flags = NULL; h->s[8].p8 = 0;
        mark(h, 0x3e0);
        break;
    }
}

/* ---------------------------------------------------------------- drawing helpers */
typedef struct { Framebuffer *fb; ClipRect clip; int scale; } Dc;

static void draw(Dc *dc, Ccb *c) { ccb_draw(dc->fb, &dc->clip, dc->scale, c); }

static void cel_at(Dc *dc, int idx, int32_t x, int32_t y, int mode)
{
    Ccb c = ccb_get(idx);
    c.v[CCB_X] = x; c.v[CCB_Y] = y;
    if (mode >= 0) { c.mode = mode; c.p1 = 0; }
    draw(dc, &c);
}

/* Hud_DrawCelAt 0x421690: digit/icon at (15 + dx, 5 + dy) from the HUD origin, mode 6. */
static void cel_hud(Dc *dc, const Hud *h, int idx, int dx, int dy)
{
    cel_at(dc, idx, (15 + dx) * 0x10000 + h->x, (5 + dy) * 0x10000 + h->y, 6);
}

/* Cel 2141 (0x238b4) scaled to w x hgt as a flat fill (mode 9 colour / mode 10 RGB555 PLUT). */
static void fill(Dc *dc, int32_t x, int32_t y, int32_t w, int32_t hgt, int mode, int32_t col, const uint8_t *plut)
{
    Ccb c = ccb_get(2141);
    c.v[CCB_X] = x; c.v[CCB_Y] = y;
    c.mode = mode; c.p1 = col; c.plut = plut;
    ccb_set_scale(&c, w, hgt);
    draw(dc, &c);
}

static void plot(Dc *dc, uint8_t col, int x, int y)          /* Bitmap_PlotPixel 0x426c90 */
{
    for (int j = 0; j < dc->scale; j++)
        for (int i = 0; i < dc->scale; i++) {
            int px = x * dc->scale + i, py = y * dc->scale + j;
            if (px >= 0 && py >= 0 && px < dc->fb->w && py < dc->fb->h) dc->fb->pixels[py * dc->fb->w + px] = col;
        }
}

/* ---------------------------------------------------------------- widgets */
static void w_weapon_counts(Dc *dc, Hud *h, Slot *s)         /* Hud_DrawWeaponCounts 0x421220 */
{
    static const int icon_stock[4] = { 0x873, 0x871, 0x874, 0x872 };
    static const int icon_ammo[4] = { 0x870, 0x86c, 0x86d, 0x870 };
    static const int ammo_max[4] = { 0x96, 0x10, 100, 100 };
    static const int icon_extra[4] = { 0, 0, 0x86e, 0x86f };
    static const int extra_val[4] = { 0, 0, 0, 0x32 };
    const Team *tm = &G.teams[h->team];
    int t = s->pc;
    if (t < 0 || t > 3) return;
    cel_hud(dc, h, icon_stock[t], 6, 1);
    int v = TEAM_STOCK(tm, t), x = 0x1d, d = 0;
    if (v > 99) { cel_hud(dc, h, 0x863, 0x1d, 3); x = 0x23; }
    if (v > 9) {
        d = (v / 10) % 10;
        cel_hud(dc, h, 0x862 + d, x, 3);
        x += (d == 1 ? -2 : 0) + 8;
        v %= 10;
    }
    cel_hud(dc, h, 0x862 + v, x, 3);
    cel_hud(dc, h, icon_ammo[t], 6, 0x1a);
    v = ammo_max[t]; x = 0x11;
    if (v > 99) { cel_hud(dc, h, 0x863, 0x11, 0x1c); x = 0x17; }
    if (v > 9) {
        d = (v / 10) % 10;
        cel_hud(dc, h, 0x862 + d, x, 0x1c);
        x += (d == 1 ? -2 : 0) + 8;
        v %= 10;
    }
    cel_hud(dc, h, 0x862 + v, x, 0x1c);
    if (icon_extra[t] && (t != 2 || G.nplayers > 1)) {
        cel_hud(dc, h, icon_extra[t], 0x26, 0x1b);
        v = t == 2 ? TEAM_MINES(tm) : extra_val[t];
        x = 0x32;
        if (v > 99) { x = 0x38; cel_hud(dc, h, 0x863, 0x32, 0x1c); }
        if (v > 9) {
            d = (v / 10) % 10;
            cel_hud(dc, h, 0x862 + d, x, 0x1c);
            x += (d == 1 ? -2 : 0) + 8;
            v %= 10;
        }
        cel_hud(dc, h, 0x862 + v, x, 0x1c);
    }
}

/* FUN_00421920 (weapon bar) / FUN_00421af0 (fuel). */
static void w_gauge(Dc *dc, Hud *h, Slot *s, uint32_t bit, bool fuel)
{
    const WeaponDef *wd = s->wd;
    if (!wd) return;
    int32_t target = fuel ? s->pc : (s->ammo ? *s->ammo * s->scale + 0xe666 : 0);
    int32_t cur = approach(s->p10, target, wd->hud_scale * g_dt);
    if (s->p10 != cur) { s->p10 = cur; mark_next(h, bit); }
    int32_t y0 = wd->hud_rect[0], x0 = wd->hud_rect[2];
    int32_t len = (int32_t)((uint32_t)(cur + 0xe666) & 0xffff0000u);
    int32_t hgt = (wd->hud_rect[1] - y0) + 0x10000;
    int32_t span = wd->hud_rect[3] - x0;
    if (span < 0) span = -span;
    int lvl;
    if (!fuel) {
        if (len < span >> 2) lvl = len < span >> 4 ? 0 : 2;
        else lvl = len < (span >> 4) * 3 ? 4 : 6;
    } else if (len < span >> 2) {
        if (len < span >> 3) {
            lvl = 0;
            if (len < span >> 4) { lvl = (g_tick & 0x20) == 0 ? 8 : 0; mark_next(h, bit); }
        } else lvl = 2;
    } else lvl = len < (span >> 3) * 3 ? 4 : 6;
    uint32_t tab = (!fuel && s->p14) ? 0x44ab08 : 0x44aaf0;
    int32_t fx = x0, fw = len;
    if (len < 0) { fw = -len; fx += len; }
    if (fw > 0xffff) fill(dc, h->x + fx, h->y104 + y0, fw, hgt, 10, 0, GAUGE_PLUT(tab + lvl * 2));
    int32_t ex = x0 + len, ew = ((wd->hud_rect[3] - len) - x0) + 0x10000;
    if (ew < 0) { ex += ew; ew = -ew; }
    if (ew > 0xffff) fill(dc, h->x + ex, h->y104 + y0, ew, hgt, 10, 0, GAUGE_PLUT(0x44aae8));
}

static void w_segments(Dc *dc, Hud *h, Slot *s, uint32_t bit)    /* FUN_00422320 (jeep grenades) */
{
    int32_t target = s->ammo ? *s->ammo << 16 : 0;
    int have = (cur_mask & 0x10) ? 0 : (int8_t)s->seg[buf];
    int32_t cur = approach(s->p10, target, g_dt * 0x9999);
    s->p10 = cur;
    if (have > 16) have = 16;
    int n = cur >> 16;
    if (n > 16) n = 16;
    if (n == have) return;
    if (have < n) {
        for (int i = have + 1; i <= n; i++) cel_at(dc, 1968, seg_x[i] + h->x, seg_y[i] + h->y104, -1);
    } else {
        int i = have < 16 ? have + 1 : have;
        while (n < i) {
            int cel = (n - i == -1) ? 0x7ae : 0x7af;
            if (i == 9 || i == 1) cel = 0x7af;
            cel_at(dc, cel, seg_x[i] + h->x, seg_y[i] + h->y104, -1);
            i--;
        }
    }
    mark_next(h, bit);
    force_ticks = 2;
    s->seg[buf] = (uint8_t)n;
}

static void w_beacon(Dc *dc, Hud *h, Slot *s)                 /* FUN_004224d0 (jeep flag beacon) */
{
    /* Plut_BuildFadeRamp 0x408f30: 17 remaps lerping PLUT(1970)->PLUT(1969) / PLUT(1972)->PLUT(1971). */
    static uint8_t ramp[2][17][16];
    static bool built;
    const SpriteBank *sb = ccb_bank();
    if (!built) {
        const uint8_t *lp = sb->data + (sb->data[0x10 + 0xc] | sb->data[0x10 + 0xd] << 8 | sb->data[0x10 + 0xe] << 16) + 0x400 + 4;
        for (int r = 0; r < 2; r++) {
            const uint8_t *a = sb->data + sb->spr[1970 + 2 * r].plut, *b = sb->data + sb->spr[1969 + 2 * r].plut;
            for (int k = 0; k < 17; k++)
                for (int i = 1; i < 16; i++) {
                    const uint8_t *ca = lp + a[i] * 4, *cb = lp + b[i] * 4;
                    int rr = (uint8_t)(ca[0] + (((int)cb[0] - ca[0]) * k >> 4));
                    int gg = (uint8_t)(ca[1] + (((int)cb[1] - ca[1]) * k >> 4));
                    int bb = (uint8_t)(ca[2] + (((int)cb[2] - ca[2]) * k >> 4));
                    int best = 0, bd = 1 << 30;
                    for (int j = 0; j < 256; j++) {
                        const uint8_t *c = lp + j * 4;
                        int dd = (c[0] - rr) * (c[0] - rr) + (c[1] - gg) * (c[1] - gg) + (c[2] - bb) * (c[2] - bb);
                        if (dd < bd) { bd = dd; best = j; }
                    }
                    ramp[r][k][i] = (uint8_t)best;
                }
        }
        built = true;
    }
    int lvl = 0;
    bool neg = false;
    if (s->obj && s->obj->p60) {
        lvl = veh_state(s->obj)->beacon;
        neg = lvl < 0;
        if (neg) lvl = -lvl;
        if (lvl > 16) lvl = 16;
    }
    Ccb c = ccb_get(neg ? 1969 : 1971);
    c.plut = ramp[neg ? 0 : 1][lvl];
    c.v[CCB_X] = 0x450000 + h->x;
    c.v[CCB_Y] = h->y104 + 0x60000;
    draw(dc, &c);
}

static void w_heli(Dc *dc, Hud *h, Slot *s)                   /* FUN_00422570 */
{
    int a, b;
    if (!s->flags) { a = 0x7ba; b = 0x7bc; }
    else if (*s->flags & 0x10000000) { a = 0x7b9; b = 0x7bc; } /* TODO: the sim never sets this obj flag yet */
    else { a = 0x7ba; b = 0x7bb; }
    Ccb c = ccb_get(a);
    c.v[CCB_X] = h->x + 0x5b0000; c.v[CCB_Y] = h->y104 + 0x140000;
    c.v[CCB_HDX] -= 0xe353; c.v[CCB_VDY] -= 0xccc;
    draw(dc, &c);
    c = ccb_get(b);
    c.v[CCB_X] = h->x + 0x240000; c.v[CCB_Y] = h->y104 + 0x240000;
    c.v[CCB_HDX] -= 0xe353; c.v[CCB_VDY] -= 0xccc;
    draw(dc, &c);
}

/* ---------------------------------------------------------------- radar */
typedef struct { Obj *o; uint32_t id; Blip shape; int pri; int32_t lx, ly; int off; } Mover;
static Mover movers[16];
static int nmovers;

static int water_cell(uint32_t w)                             /* FUN_00418a30 */
{
    uint32_t b = CELL_BNO(w), t = w & 0x7f;
    if (t > 0x33 || b == 0x4a || b == 0x4b) return 0;
    if (t == 1 || t == 2) return (int)t;
    if (t < 4) return 0;
    return 1;
}

static uint8_t radar_colour(uint32_t w)                       /* RadarUpdateCell 0x422930 */
{
    int b = (int)CELL_BNO(w);
    if (b) {
        uint32_t c = bno_radar(b);
        if (!c) return 0x87;
        return (w & 0xc000) ? (uint8_t)(c >> 16) : (uint8_t)c;
    }
    if (water_cell(w)) return 0x91;
    return (w & 0x80000000u) ? 0xc9 : 0x87;                   /* bit 31: FUN_00422e40 (class-10 object on the cell) */
}

static int plot_blip(const Blip *b, int team, int32_t x, int32_t y)   /* Radar_PlotBlip 0x4233d0 */
{
    int off = 0;
    for (int i = 0; i < b->n; i++) {
        int8_t c = team == 1 ? b->p[i][3] : b->p[i][2];
        int cx = b->p[i][0] + (x >> 21), cy = b->p[i][1] + (y >> 21);
        if (cx < 0 || cx > 127 || cy < 0 || cy > 127) off = 1;
        else radar[cy * 128 + cx] = (uint8_t)c;
    }
    return off;
}

/* Registered movers (FUN_00422c70 from VehicleInit with def[0x9a + team] / def[0x99]). Vehicles are
   picked up from G.cur_vehicle; flag objects are added below (0x44e7a8, pri 10000);
   the 0x4557e0 movers (pri 1000, 0x437f..) are not simulated yet. */
static void gather_movers(void)
{
    nmovers = 0;
    for (int t = 0; t < 2; t++) {
        Obj *o = G.cur_vehicle[t];
        if (!o || o->id != G.cur_vehicle_id[t] || !o->p60 || o->cls->id != 1) continue;
        const VehicleDef *d = veh_state(o)->def;
        Mover *m = &movers[nmovers++];
        m->o = o; m->id = o->id; m->pri = d->radar_99;
        uint32_t va = d->radar_icon[t];
        if (va == 0x44b398) m->shape = (Blip){ 5, BLIP_AT(0x44b380) };
        else m->shape = (Blip){ 9, BLIP_AT(0x44b338) };
    }
    if (nmovers == 2 && movers[0].pri > movers[1].pri) { Mover x = movers[0]; movers[0] = movers[1]; movers[1] = x; }
    /* Flag objects: FUN_00422c70(flag, 0x44e7a8, 10000) from FlagBuildingDestroyed; FlagUpdate blinks the blip
       between the flag shape 0x44e7a8 and the empty 0x44e7b8 every 15 ticks. Priority 10000: plotted last. */
    const int8_t (*blip_flag)[4] = BLIP_AT(0x44e788);
    for (int t = 0; t < 2; t++) {
        Obj *f = G.flag_obj[t];
        if (!f || nmovers >= 16) continue;
        Mover *m = &movers[nmovers++];
        m->o = f; m->id = f->id; m->pri = 10000;
        m->shape = FLAG_BLIP(f) ? (Blip){ 8, blip_flag } : (Blip){ 0, blip_flag };
    }
}

void hud_radar_update(void)                                   /* RadarUpdateMovers 0x422fb0 */
{
    /* The original keeps the image incrementally (RadarUpdateCell on every cell change, movers
       erased/replotted); rebuilding it from the cells each frame gives the same picture. */
    for (int i = 0; i < 128 * 128; i++) radar[i] = radar_colour(G.cell[i]);
    for (int t = 0; t < 2; t++) {                             /* Radar_MarkSpecialCells 0x422ae0 */
        uint8_t c = t ? 0x70 : 0xd3;
        for (int k = 0; k < G.nflags[t]; k++) {
            uint32_t *cell = G.flag_sites[t][k];
            if (!cell || (*cell & 0x3f80) != 0xb00 || !(*cell & 0xe000000)) continue;
            int i = cell_index(cell);
            if (i - 128 >= 0) radar[i - 128] = c;
            if (i + 128 < 128 * 128) radar[i + 128] = c;
            if (i > 0) radar[i - 1] = c;
            if (i + 1 < 128 * 128) radar[i + 1] = c;
        }
    }
    gather_movers();
    for (int k = 0; k < nmovers; k++) {
        Mover *m = &movers[k];
        m->off = plot_blip(&m->shape, m->o->team, m->o->pos[0], m->o->pos[1]);
        m->lx = m->o->pos[0]; m->ly = m->o->pos[1];
    }
    for (int t = 0; t < 2; t++) {                             /* FUN_00432020: ring round each home pad */
        uint8_t c = t ? 0x6c : 0xcf;
        const Team *tm = &G.teams[t];
        for (int p = 0; p < tm->npads && p < 4; p++) {
            if (!tm->pads[p].cell) continue;
            int base = cell_index(tm->pads[p].cell);
            for (int dy = -1; dy < 2; dy++)
                for (int dx = -1; dx < 2; dx++) {
                    int i = base + dy * 128 + dx;
                    if ((dx || dy) && i >= 0 && i < 128 * 128) radar[i] = c;
                }
        }
    }
}

/* Hud_DrawRadarWindow 0x421d20: blips of movers partly off the map, plotted straight to the screen. */
static void radar_window_blips(Dc *dc, const int32_t *r, const int32_t *pos, int px, int py)
{
    int w = r[3], hh = r[4];
    for (int k = 0; k < nmovers; k++) {
        const Mover *m = &movers[k];
        if (!m->off || m->o->id != m->id) continue;
        for (int i = 0; i < m->shape.n; i++) {
            int sx = ((m->o->pos[0] >> 21) - ((pos[0] >> 21) - (w >> 1))) + m->shape.p[i][0];
            int sy = ((m->o->pos[1] >> 21) - ((pos[1] >> 21) - (hh >> 1))) + m->shape.p[i][1];
            if (sx < 0 || sx >= w || sy < 0 || sy >= hh) continue;
            plot(dc, (uint8_t)(m->o->team == 1 ? m->shape.p[i][3] : m->shape.p[i][2]), sx + px, sy + py);
        }
    }
}

static void w_radar(Dc *dc, Hud *h, Slot *s, uint32_t bit)    /* FUN_00421e40 */
{
    int32_t *r = s->r;
    mark_next(h, bit);
    Obj *o = s->obj;
    if (!o) {
        if (r[6] == 0) return;
        if (r[6] < 0) { r[9] = 1 - r[6]; return; }
        fill(dc, h->x + r[1], h->y104 + r[2], r[3] << 16, r[4] << 16, 9, 0x91, NULL);
        cel_at(dc, r[6], h->x + r[1], h->y104 + r[2], -1);
        r[9] = r[6] + 1;
        return;
    }
    int w = r[3], hh = r[4];
    int32_t ly = r[2], lx;
    int x0 = (o->pos[0] >> 21) - (w >> 1), y0 = (o->pos[1] >> 21) - (hh >> 1), yy = y0;
    bool cx = w + x0 > 0x7f, cy = hh + y0 > 0x7f;
    if (cx) w = 0x80 - x0;
    if (cy) hh = 0x80 - y0;
    bool okx = x0 >= 0;
    if (okx) lx = r[1];
    else { w += x0; lx = r[1] - x0 * 0x10000; x0 = 0; }
    if (y0 < 0) { yy = 0; hh += y0; ly -= y0 * 0x10000; }
    if (y0 < 0 || !okx || cy || cx) {
        fill(dc, h->x + r[1], h->y104 + r[2], r[3] << 16, r[4] << 16, 9, 0x91, NULL);
        radar_window_blips(dc, r, o->pos, (h->x + r[1]) >> 16, (h->y104 + r[2]) >> 16);
    }
    if (w > 0 && hh > 0) {                                    /* the radar cel (1981..1986) over DAT_00440c64 */
        Ccb c = ccb_get(s->p10);
        c.src = radar; c.w = 128; c.h = 128;
        ccb_subrect(&c, x0, yy, w, hh);
        c.flags |= CCB_F_OPAQUE;
        c.v[CCB_X] = h->x + lx + 0x8000;
        c.v[CCB_Y] = h->y + ly + 0x8000;
        draw(dc, &c);
    }
    if (o->cls->id == 1 && o->p60) {                          /* hit flash, sized by the damage taken */
        VehState *vs = veh_state(o);
        int32_t dt = g_tick - vs->_4c;
        if (dt > -10 && dt < 0x40) {
            int k = 15 - (fix_mul(vs->hp, s->p14) >> 16);
            if (k > 15) k = 15;
            if (k < 0) k = 0;
            const Sprite *sp = &ccb_bank()->spr[1947 + k];
            cel_at(dc, 1947 + k, (r[3] - sp->w) * 0x8000 + h->x + r[1], (r[4] - sp->h) * 0x8000 + h->y104 + r[2], 0xe);
        }
    }
    int v = r[6];
    if (v < 0) {
        Ccb c = ccb_get(-v);
        c.v[CCB_X] = h->x + r[1] - 0x40000; c.v[CCB_Y] = h->y104 + r[2] - 0x30000;
        c.mode = (c.mode == 3) + 0xe;
        draw(dc, &c);
        r[9] = -v + 1;
    } else if (v > 0) r[9] = v + 1;
    if (r[9] && o->tm && o->tm->arrow_dir >= 0) {             /* enemy direction marker */
        Ccb c = ccb_get(r[9]);
        c.v[CCB_X] = h->x + r[7]; c.v[CCB_Y] = h->y + r[8];
        int dir = o->tm->arrow_dir;
        if (dir < 8 && s->arrow) {
            const uint8_t *b = s->arrow + dir * 4;
            ccb_subrect(&c, b[0], b[1], b[2] - b[0] + 1, b[3] - b[1] + 1);
            c.v[CCB_X] += b[0] * 0x10000; c.v[CCB_Y] += b[1] * 0x10000;
        } else c.mode = 0xe;
        draw(dc, &c);
    }
    if (r[6] > 0) cel_at(dc, r[6], h->x + r[1] + 0x8000, h->y104 + r[2] + 0x8000, 0xb);
}

static void run_widget(Dc *dc, Hud *h, int i, uint32_t bit)
{
    Slot *s = &h->s[i];
    switch (s->kind) {
    case K_FRAME: {                                           /* Hud_DrawFrame: CCB 0x203d8, opaque mode 6 */
        Ccb c = ccb_get(1942);
        c.flags |= CCB_F_OPAQUE;
        c.v[CCB_X] = h->x + 0x90000; c.v[CCB_Y] = h->y + 0x20000;
        c.mode = 6; c.p1 = 0;
        draw(dc, &c);
        break;
    }
    case K_CEL:
        if (s->p8 != 0x795) {
            Ccb c = ccb_get(s->p8);
            c.v[CCB_X] = h->x + s->pc; c.v[CCB_Y] = s->p10 + h->y;
            c.mode = (c.mode == 3) + 0xe;
            draw(dc, &c);
        }
        break;
    case K_WCOUNT: w_weapon_counts(dc, h, s); break;
    case K_SLIDE_IN:
        if (s->pc != s->p10) {
            int32_t v = approach(s->p10, s->pc, g_dt * 0xcccc);
            if (s->p10 != v) { s->p10 = v; h->y104 = h->y + v; }
            mark_next(h, 0x10);
            force_ticks = 4;
        }
        cel_at(dc, s->p8, h->x, h->y104, 6);
        break;
    case K_SLIDE_OUT: {
        force_ticks = 4;
        h->y104 = h->y + s->p10;
        cel_at(dc, s->p8, h->x, h->y104, 6);
        int32_t v = approach(s->p10, s->pc, g_dt * 0x11999);
        if (s->p10 != v) { s->p10 = v; mark_next(h, 0x1f); } else mark_next(h, 0xf);
        break;
    }
    case K_BAR: w_gauge(dc, h, s, bit, false); break;
    case K_FUEL: w_gauge(dc, h, s, bit, true); break;
    case K_RADAR: w_radar(dc, h, s, bit); break;
    case K_SEGS: w_segments(dc, h, s, bit); break;
    case K_BEACON: w_beacon(dc, h, s); break;
    case K_HELI: w_heli(dc, h, s); break;
    default: break;
    }
}

void hud_update_all(Framebuffer *fb, int scale, int nplayers)   /* GameFrame1P tail + HudUpdateAll */
{
    if (!ccb_bank()) return;
    if (force_ticks) { mark(&huds[0], 0x3f0); if (nplayers > 1) mark(&huds[1], 0x3f0); force_ticks--; }
    Dc dc = { fb, { 0, 0, 320 * scale - 1, clip_h * scale - 1 }, scale };
    if (dc.clip.x1 >= fb->w) dc.clip.x1 = fb->w - 1;
    if (dc.clip.y1 >= fb->h) dc.clip.y1 = fb->h - 1;
    for (int p = 0; p < nplayers && p < 2; p++) {
        Hud *h = &huds[p];
        /* The fuel target is written by VehicleUpdate whenever the whole-unit value changes. */
        Slot *f = &h->s[5];
        if (h->s[9].obj && h->s[9].obj->p60 && f->kind == K_FUEL) {
            int32_t t = (veh_state(h->s[9].obj)->fuel >> 16) * f->scale;
            if ((f->p10 ^ t) & 0xffff0000) { mark(h, 0x20); f->pc = t; }
        }
        /* ViewModeBunkerSelect keeps the weapon-count widget on the highlighted type. */
        if (G.views[p].mode == BV_SELECT) { h->s[2].pc = G.views[p].sel; mark(h, 0xf); }
        uint32_t m = h->dirty[buf];
        cur_mask = m;
        h->dirty[buf] = 0;
        for (uint32_t bit = 1, i = 0; i < 10 && (int32_t)bit <= (int32_t)m; bit <<= 1, i++) {
            if (!(m & bit) || h->s[i].kind == K_NONE) continue;
            run_widget(&dc, h, (int)i, bit);
            m |= h->s[i].chain;
        }
    }
    buf ^= 1;                                                 /* FrameTiming toggles DAT_0045f3a0 */
}
