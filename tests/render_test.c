/* Renderer verification: renders the tools/view.py sample scenes with src/render into an 8-bit
   320x240 buffer (same as view.py's Raster), writes out/view_c/<scene>.raw (index buffer) and
   <scene>.png (+ <scene>_hi.png rendered at scale 2 into 640x480), and compares each .raw with
   out/view_py/<scene>.raw when present (written by tools/render_cmp.py).

   Usage: render_test [cd-dir] [--bench N] [--search MAP target.rgb]   (run from the project root) */
#include "../src/render/render.h"
#include "../src/assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "../src/exe.h"
#include "../src/vfs_host.h"

#ifndef TOWN_X
#define TOWN_X 1808   /* cell (56,45) centre, as tools/render_cmp.py TOWN */
#define TOWN_Y 1456
#endif

typedef struct { const char *name, *map, *mode; int at; int x, y; } Scene;

/* keep in sync with SCENES in tools/render_cmp.py */
static const Scene scenes[] = {
    { "RFMAP001_bunker", "WORLDS/1PLAYER/LEVEL1/RFMAP001.RFM", "bunker", 0, 0, 0 },
    { "RFMAP001_drive", "WORLDS/1PLAYER/LEVEL1/RFMAP001.RFM", "drive", 0, 0, 0 },
    { "RFMAP051_town_bunker", "WORLDS/1PLAYER/LEVEL6/RFMAP051.RFM", "bunker", 1, TOWN_X, TOWN_Y },
    { "RFMAP051_town_drive", "WORLDS/1PLAYER/LEVEL6/RFMAP051.RFM", "drive", 1, TOWN_X, TOWN_Y },
    { "RFMAP051_town_heli", "WORLDS/1PLAYER/LEVEL6/RFMAP051.RFM", "heli", 1, TOWN_X, TOWN_Y },
    { "RFMAP051_town_lift", "WORLDS/1PLAYER/LEVEL6/RFMAP051.RFM", "lift", 1, TOWN_X, TOWN_Y },
    { "RFMAP051_town_intro", "WORLDS/1PLAYER/LEVEL6/RFMAP051.RFM", "intro", 1, TOWN_X, TOWN_Y },
};

static void mode_params(const char *m, int32_t *pitch, int32_t *H, int32_t *zoff)
{
    /* view.py MODES: trackers created by the game */
    if (!strcmp(m, "drive")) { *pitch = 0x180000; *H = -0xaa0000; *zoff = 0xa0000; }
    else if (!strcmp(m, "heli")) { *pitch = 0x180000; *H = -0x640000; *zoff = 0xa0000; }
    else if (!strcmp(m, "lift")) { *pitch = 0x400000; *H = 0xfa0000; *zoff = 0xa0000; }
    else if (!strcmp(m, "intro")) { *pitch = 0x200000; *H = 0x10e0000; *zoff = 0; }
    else { *pitch = 0x180000; *H = 0; *zoff = 0; }
}

/* ---- minimal PNG writer (stored deflate) ---- */
static uint32_t crc_tab[256];
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t k = i;
            for (int j = 0; j < 8; j++) k = k & 1 ? 0xedb88320u ^ (k >> 1) : k >> 1;
            crc_tab[i] = k;
        }
    c = ~c;
    while (n--) c = crc_tab[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *t, const uint8_t *d, size_t n)
{
    uint8_t h[8];
    be32(h, (uint32_t)n); memcpy(h + 4, t, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uint32_t c = crc(crc(0, (const uint8_t *)t, 4), d, n);
    be32(h, c);
    fwrite(h, 1, 4, f);
}
static void write_png(const char *path, const uint8_t *px, int w, int h, const RGB *pal)
{
    size_t raw_n = (size_t)(w * 3 + 1) * h;
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < h; y++) {
        uint8_t *r = raw + (size_t)y * (w * 3 + 1);
        r[0] = 0;
        for (int x = 0; x < w; x++) {
            RGB c = pal[px[y * w + x]];
            r[1 + x * 3] = c.r; r[2 + x * 3] = c.g; r[3 + x * 3] = c.b;
        }
    }
    size_t nblk = (raw_n + 65534) / 65535, zn = 2 + raw_n + nblk * 5 + 4;
    uint8_t *z = malloc(zn), *q = z;
    *q++ = 0x78; *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t o = 0; o < raw_n; o += 65535) {
        size_t n = raw_n - o < 65535 ? raw_n - o : 65535;
        *q++ = o + n == raw_n;
        *q++ = n & 0xff; *q++ = n >> 8; *q++ = ~n & 0xff; *q++ = (~n >> 8) & 0xff;
        memcpy(q, raw + o, n); q += n;
    }
    be32(q, b << 16 | a); q += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ih[13];
    be32(ih, (uint32_t)w); be32(ih + 4, (uint32_t)h);
    ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13);
    chunk(f, "IDAT", z, (size_t)(q - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw); free(z);
}

static void paste_statusbar(Framebuffer *fb, const char *rel, int y)
{
    Image8 img;
    if (!image_load_bmp8(rel, &img)) return;
    /* RFA pixels index the game palette; the DirectDraw palette holds it shifted by +10 */
    for (int j = 0; j < img.h && y + j < fb->h; j++)
        for (int i = 0; i < img.w && i < fb->w; i++) {
            int p = img.pixels[j * img.w + i];
            fb->pixels[(y + j) * fb->w + i] = (uint8_t)(p < 236 ? p + 10 : p);
        }
    image_free(&img);
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void setup_view(View *v, const CellMap *cm, const Scene *s)
{
    int32_t pitch, H, zoff;
    mode_params(s->mode, &pitch, &H, &zoff);
    view_init(v, 0, 0, 320, 152);
    view_set_pitch_height(v, pitch, H);
    int32_t tx, ty;
    if (s->at) { tx = s->x << 16; ty = s->y << 16; }
    else { tx = (cm->pad_x[0][0] * 32 + 16) << 16; ty = (cm->pad_y[0][0] * 32 + 16) << 16; }
    view_look_at(v, tx, ty, 0, zoff);
}

static void render_scene(View *v, const CellMap *cm, Framebuffer *fb, int scale)
{
    RenderMap rm = cellmap_view(cm);
    memset(fb->pixels, 0, (size_t)fb->w * fb->h);
    render_world(v, &rm, fb, scale);
}

/* ---- vehicle sheets: every vehicle type through its draw wrapper (out/vehicles/<name>.png) ---- */
typedef struct {
    const ModelPart *model;
    int cls;                    /* 1 vehicle, 5 lift, 6 wreck */
    int32_t dx, dy, z;          /* offset from the scene centre (px), altitude (16.16) */
    int32_t heading;
    RenderVeh s;
    int32_t u68, u70;
    const ModelPart *lift;      /* Storage: vehicle on the lift */
    int heli;                   /* add the heli ground-shadow object */
    int32_t H;                  /* camera height (0 = driving -170) */
} VehShot;

static void vehicle_sheet(const char *name, CellMap *cm, const VehShot *shots, int n, const SpriteBank *sb)
{
    const int W = 640, VH = 304, cols = 2, rows = (n + 1) / 2;
    uint8_t *sheet = calloc((size_t)W * cols * VH * rows, 1);
    Framebuffer hi = { 640, 480, calloc(640, 480), { { 0 } } };
    int32_t cx = (cm->pad_x[0][0] * 32 + 16) << 16, cy = (cm->pad_y[0][0] * 32 + 16) << 16;
    int ci = cm->pad_y[0][0] * 128 + cm->pad_x[0][0];
    uint32_t saved = cm->cell[ci];
    for (int i = 0; i < n; i++) {
        const VehShot *q = &shots[i];
        View v = { 0 };
        view_init(&v, 0, 0, 320, 152);
        view_set_pitch_height(&v, 0x180000, q->H ? q->H : -0xaa0000);
        RenderObj o = { .model = q->model, .pos = { cx + (q->dx << 16), cy + (q->dy << 16), q->z }, .yaw = q->heading };
        o.cls = q->cls;
        o.id = 0x201;
        o.tick = 1000;
        o.u68 = q->u68;
        o.u70 = q->u70;
        RenderVeh s = q->s;
        if (q->cls == 1 || q->cls == 6) o.veh = &s;
        if (q->cls == 5) {
            o.lift_model = q->lift;
            cm->cell[ci] = (saved & ~0x7fu) | 92;              /* TERR_LIFT while the lift is up */
        } else cm->cell[ci] = saved;
        view_look_at(&v, o.pos[0], o.pos[1], q->z > 0 ? q->z : 0, 0xa0000);
        RenderObj sh;
        render_clear_objects(&v);
        render_add_object(&v, &o);
        if (q->heli) { render_heli_shadow(&sh, &o); render_add_object(&v, &sh); }
        render_scene(&v, cm, &hi, 2);
        render_clear_objects(&v);
        int ox = (i % cols) * W, oy = (i / cols) * VH;
        for (int y = 0; y < VH; y++) memcpy(sheet + (size_t)(oy + y) * W * cols + ox, hi.pixels + y * 640, W);
        view_free(&v);
    }
    cm->cell[ci] = saved;
    char path[256];
    snprintf(path, sizeof path, "out/vehicles/%s.png", name);
    write_png(path, sheet, W * cols, VH * rows, sb->palette);
    printf("vehicles: %s (%d views)\n", path, n);
    free(sheet);
    free(hi.pixels);
}

static void vehicle_sheets(CellMap *cm, const SpriteBank *sb)
{
    if (!cellmap_load(cm, "WORLDS/1PLAYER/LEVEL1/RFMAP001.RFM")) return;
    mkdir("out/vehicles", 0755);
    const RenderVeh idle = { .sink_depth = 0xe0000 };
    const VehShot tank[] = {
        { MODEL_TANK_BODY, 1, 64, 0, 0, 0, idle, 0, 0, NULL, 0, 0 },
        { MODEL_TANK_BODY, 1, 64, 0, 0, 0x100000, { .turret = 0x80000 }, 0, 0, NULL, 0, 0 },
        { MODEL_TANK_BODY, 1, 64, 0, 0, 0x280000, { .turret = 0x300000, .elev = 0x3b8e39 }, 0, 0, NULL, 0, 0 },
        { MODEL_TANK_BODY, 1, 64, 0, 0, 0x0c0000, { .flash_tick = 1010, .turret = 0x3c0000 }, 0, 0, NULL, 0, 0 },
        { MODEL_TANK_SHADOW, 1, 64, 0, 0, 0x340000, { .water_anim = 0x20000, .turret = 0x40000 }, 0, 0, NULL, 0, 0 },
        { MODEL_TANK_PART_0X55, 1, 64, 0, -0x40000, 0x080000, { .sink_depth = 0xe0000 }, 0, 0, NULL, 0, 0 },
        { MODEL_TANK_BODY, 6, 64, 0, 0, 0x180000, idle, 0, 0, NULL, 0, 0 },
        { MODEL_TANK_WRECK_A, 6, 64, 0, 0, 0x180000, idle, 0, 0, NULL, 0, 0 },
    };
    const VehShot heli[] = {
        { MODEL_HELI, 1, 64, 0, 0, 0x180000, idle, 0, 0, NULL, 1, -0x640000 },
        { MODEL_HELI, 1, 64, 0, 0, 0x180000, { .turret = 0x8000, .rotor = 0x100000, .rotor_speed = 0x20000 }, 0, 0, NULL, 1, -0x640000 },
        { MODEL_HELI, 1, 64, 0, 0x320000, 0, { .turret = 0x10000, .rotor = 0x123456, .rotor_speed = 0x40000 }, 0, 0, NULL, 1, -0x640000 },
        { MODEL_HELI, 1, 64, 0, 0x320000, 0x2c0000, { .turret = 0x10000, .rotor = 0x2a0000, .rotor_speed = 0x40000, .bank = 0x30000 }, 0, 0x18000, NULL, 1, -0x640000 },
        { MODEL_HELI, 1, 64, 0, 0x180000, 0x100000, { .turret = 0x10000, .rotor = 0x80000, .rotor_speed = 0x30000, .bank = -0x30000 }, 0, 0, NULL, 1, -0x640000 },
        { MODEL_HELI, 1, 64, 0, 0x320000, 0x0c0000, { .turret = 0x10000, .rotor = 0x300000, .rotor_speed = 0x20000 }, 0, -0x18000, NULL, 1, -0x640000 },
        { MODEL_HELI_WRECK_A, 6, 64, 0, 0, 0x100000, idle, 0, 0, NULL, 0, -0x640000 },
        { MODEL_HELI_WRECK_B, 6, 64, 0, 0, 0x100000, idle, 0, 0, NULL, 0, -0x640000 },
    };
    const VehShot jeep[] = {
        { MODEL_JEEP_BODY, 1, 64, 0, 0, 0, idle, 0, 0, NULL, 0, 0 },
        { MODEL_JEEP_BODY, 1, 65, 0, 0, 0x140000, idle, 0, 0, NULL, 0, 0 },
        { MODEL_JEEP_BODY, 1, 66, 0, 0, 0x2c0000, idle, 0, 0, NULL, 0, 0 },
        { MODEL_JEEP_SHADOW, 1, 64, 0, 0, 0x0c0000, { .water_anim = 0x30000 }, 0, 0, NULL, 0, 0 },
        { MODEL_JEEP_SHADOW, 1, 64, 0, 0, 0x0c0000, { .water_anim = 0x30000, .rotor = 0x6000 }, 0, 0, NULL, 0, 0 },
        { MODEL_JEEP_SHADOW, 1, 64, 0, 0, 0x0c0000, { .water_anim = 0x60000, .rotor = 0x10000 }, 0, 0, NULL, 0, 0 },
        { MODEL_JEEP_PART_0X55, 1, 64, 0, -0x30000, 0x300000, { .sink_depth = 0xd0000 }, 0, 0, NULL, 0, 0 },
        { MODEL_JEEP_WRECK_A, 6, 64, 0, 0, 0x300000, idle, 0, 0, NULL, 0, 0 },
    };
    const VehShot msv[] = {
        { MODEL_MSV_BODY, 1, 64, 0, 0, 0, idle, 0, 0, NULL, 0, 0 },
        { MODEL_MSV_BODY, 1, 64, 0, 0, 0x100000, { .elev = 0x3b8e39 }, 0, 0, NULL, 0, 0 },
        { MODEL_MSV_BODY, 1, 64, 0, 0, 0x280000, { .turret = -0x40000 }, 0, 0, NULL, 0, 0 },
        { MODEL_MSV_WRECK_A, 6, 64, 0, 0, 0x280000, idle, 0, 0, NULL, 0, 0 },
    };
    const VehShot lift[] = {
        { MODEL_CLASS_STORAGE_RISING, 5, 0, 0, -0x200000, 0x200000, idle, 0, 0, MODEL_TANK_BODY, 0, 0 },
        { MODEL_CLASS_STORAGE_RISING, 5, 0, 0, -0x100000, 0x200000, idle, 30, 0, MODEL_TANK_BODY, 0, 0 },
        { MODEL_CLASS_STORAGE_RISING, 5, 0, 0, -0x80000, 0x180000, idle, 45, 0, MODEL_HELI, 0, 0 },
        { MODEL_CLASS_STORAGE_RISING, 5, 0, 0, 0, 0x200000, idle, 107, 0, MODEL_JEEP_BODY, 0, 0 },
    };
    vehicle_sheet("tank", cm, tank, 8, sb);
    vehicle_sheet("heli", cm, heli, 8, sb);
    vehicle_sheet("jeep", cm, jeep, 8, sb);
    vehicle_sheet("msv", cm, msv, 4, sb);
    vehicle_sheet("lift", cm, lift, 4, sb);
}

/* brute-force the tracked point of a reference frame (cell centres), target = 320x152 RGB */
static int search(const SpriteBank *sb, const char *map, const char *mode, const char *target)
{
    size_t n = 0;
    uint8_t *t = NULL;
    FILE *f = fopen(target, "rb");
    if (f) { fseek(f, 0, SEEK_END); n = (size_t)ftell(f); fseek(f, 0, SEEK_SET); t = malloc(n); fread(t, 1, n, f); fclose(f); }
    if (!t || n != 320 * 152 * 3) { fprintf(stderr, "bad target\n"); return 1; }
    CellMap *cm = malloc(sizeof *cm);
    if (!cellmap_load(cm, map)) return 1;
    Framebuffer fb = { 320, 240, calloc(320, 240), { { 0 } } };
    View v = { 0 };
    int best = -1, bx = 0, by = 0;
    for (int cy = 0; cy < 128; cy++)
        for (int cx = 0; cx < 128; cx++) {
            Scene s = { "", map, mode, 1, cx * 32 + 16, cy * 32 + 16 };
            setup_view(&v, cm, &s);
            render_scene(&v, cm, &fb, 1);
            int ok = 0;
            for (int i = 0; i < 320 * 152; i++) {
                RGB c = sb->palette[fb.pixels[i]];
                ok += c.r == t[i * 3] && c.g == t[i * 3 + 1] && c.b == t[i * 3 + 2];
            }
            if (ok > best) { best = ok; bx = s.x; by = s.y; }
        }
    printf("best %d/%d at cell centre %d %d\n", best, 320 * 152, bx, by);
    int cx0 = bx, cy0 = by;   /* refine to the pixel */
    for (int y = cy0 - 32; y <= cy0 + 32; y++)
        for (int x = cx0 - 32; x <= cx0 + 32; x++) {
            Scene s = { "", map, mode, 1, x, y };
            setup_view(&v, cm, &s);
            render_scene(&v, cm, &fb, 1);
            int ok = 0;
            for (int i = 0; i < 320 * 152; i++) {
                RGB c = sb->palette[fb.pixels[i]];
                ok += c.r == t[i * 3] && c.g == t[i * 3 + 1] && c.b == t[i * 3 + 2];
            }
            if (ok > best) { best = ok; bx = x; by = y; }
        }
    printf("best %d/%d at %d %d\n", best, 320 * 152, bx, by);
    return 0;
}

int main(int argc, char **argv)
{
    const char *root = "cd";
    int bench = 0;
    const char *smap = NULL, *starget = NULL, *smode = "bunker";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bench") && i + 1 < argc) bench = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--search") && i + 3 < argc) { smap = argv[++i]; smode = argv[++i]; starget = argv[++i]; }
        else root = argv[i];
    }
    if (strcmp(root, "cd") ? !vfs_mount_path(root) : !vfs_mount_default()) { fprintf(stderr, "cannot mount %s\n", root); return 1; }
    if (!exe_load()) { fprintf(stderr, "%s\n", exe_error()); return 1; }   /* tables from RFIRE.BIN */
    SpriteBank *sb = malloc(sizeof *sb);
    if (!sprites_load(sb, "ART/ART.CAR")) { fprintf(stderr, "cannot load ART.CAR from %s\n", root); return 1; }
    render_init(sb);
    if (smap) return search(sb, smap, smode, starget);
    mkdir("out", 0755);
    mkdir("out/view_c", 0755);
    Framebuffer lo = { 320, 240, calloc(320, 240), { { 0 } } };
    Framebuffer hi = { 640, 480, calloc(640, 480), { { 0 } } };
    CellMap *cm = malloc(sizeof *cm);
    View v = { 0 };
    int fails = 0, compared = 0;
    for (size_t k = 0; k < sizeof scenes / sizeof *scenes; k++) {
        const Scene *s = &scenes[k];
        if (!cellmap_load(cm, s->map)) { fprintf(stderr, "cannot load %s\n", s->map); return 1; }
        setup_view(&v, cm, s);
        render_scene(&v, cm, &lo, 1);
        char path[256];
        snprintf(path, sizeof path, "out/view_c/%s.raw", s->name);
        FILE *f = fopen(path, "wb");
        fwrite(lo.pixels, 1, 320 * 240, f);
        fclose(f);
        /* compare with view.py's buffer */
        snprintf(path, sizeof path, "out/view_py/%s.raw", s->name);
        f = fopen(path, "rb");
        int mism = -1, first = -1;
        if (f) {
            uint8_t *ref = malloc(320 * 240);
            if (fread(ref, 1, 320 * 240, f) == 320 * 240) {
                mism = 0;
                for (int i = 0; i < 320 * 240; i++)
                    if (ref[i] != lo.pixels[i]) { if (first < 0) first = i; mism++; }
            }
            free(ref);
            fclose(f);
            compared++;
            if (mism) fails++;
        }
        int items = v.nitems, cels = v.ncels;
        paste_statusbar(&lo, "ART/1PBSCRL.RFA", 152);
        snprintf(path, sizeof path, "out/view_c/%s.png", s->name);
        write_png(path, lo.pixels, 320, 240, sb->palette);
        /* hi-res (scale 2) */
        double t0 = now_ms();
        int reps = bench > 0 ? bench : 1;
        for (int r = 0; r < reps; r++) render_scene(&v, cm, &hi, 2);
        double ms = (now_ms() - t0) / reps;
        paste_statusbar(&hi, "ART/1PBSCRH.RFA", 304);
        snprintf(path, sizeof path, "out/view_c/%s_hi.png", s->name);
        write_png(path, hi.pixels, 640, 480, sb->palette);
        printf("%-24s cam=(%.1f,%.1f) top=%d left=%d cels=%d models=%d  mismatch=%d", s->name,
               v.camx / 65536.0, v.camy / 65536.0, v.top, v.left, cels, items, mism);
        if (mism > 0) printf(" (first at %d,%d)", first % 320, first / 320);
        printf("  hi-res %.3f ms/frame\n", ms);
    }
    /* live vehicles / objects through the draw wrappers and depth hooks -> out/vehicles/ */
    vehicle_sheets(cm, sb);
    /* Camera_Update: a tracker 5 cells away must converge to the view.py steady state (look_at) */
    {
        View c = { 0 };
        view_init(&c, 0, 0, 320, 152);
        int32_t a[3] = { 2000 << 16, 2000 << 16, 0 }, b[3] = { 2160 << 16, 1840 << 16, 0 };
        camera_add_tracker(&c, 0, b, NULL, 0xa0000, 0x180000, -0xaa0000, true, NULL);
        camera_snap(&c, a, 0, 0x200000);
        int frames = 0, settled = -1;
        for (; frames < 1000; frames++) {
            int32_t px = c.camx, py = c.camy, ph = c.H, pp = c.pitch;
            camera_update(&c, 2);
            if (px == c.camx && py == c.camy && ph == c.H && pp == c.pitch) { if (settled < 0) settled = frames; }
            else settled = -1;
        }
        frames = settled;
        View ref = { 0 };
        view_init(&ref, 0, 0, 320, 152);
        view_set_pitch_height(&ref, 0x180000, -0xaa0000);
        view_look_at(&ref, b[0], b[1], 0, 0xa0000);
        /* the original stops following once (smoothed - target) >> 16 == 0 and Camera_StepAxis stops
           once the integer parts match, so it settles up to 1 unit short of view.py's ideal */
        bool ok = settled >= 0 && abs(c.camx - ref.camx) < 0x20000 && abs(c.camy - ref.camy) < 0x20000 &&
                  (c.pitch >> 16) == (ref.pitch >> 16) && (c.H >> 16) == (ref.H >> 16);
        printf("camera follow: settled after %d frames (dt 2) cam=(%.2f,%.2f) ref=(%.2f,%.2f) pitch=0x%x H=%.2f %s\n",
               frames, c.camx / 65536.0, c.camy / 65536.0, ref.camx / 65536.0, ref.camy / 65536.0, c.pitch,
               c.H / 65536.0, ok ? "ok" : "MISMATCH");
        if (!ok) fails++;
        view_free(&c);
        view_free(&ref);
    }
    /* random scenes listed by tools/render_cmp.py --fuzz */
    FILE *fz = fopen("out/view_py/fuzz.txt", "r");
    char name[64], map[128], mode[16];
    int fx, fy, nf = 0, ff = 0;
    long total_mism = 0;
    while (fz && fscanf(fz, "%63s %127s %15s %d %d", name, map, mode, &fx, &fy) == 5) {
        Scene s = { name, map, mode, 1, fx, fy };
        if (!cellmap_load(cm, map)) continue;
        setup_view(&v, cm, &s);
        render_scene(&v, cm, &lo, 1);
        char path[256];
        snprintf(path, sizeof path, "out/view_py/%s.raw", name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        uint8_t *ref = malloc(320 * 240);
        int mism = 0;
        if (fread(ref, 1, 320 * 240, f) == 320 * 240)
            for (int i = 0; i < 320 * 240; i++) mism += ref[i] != lo.pixels[i];
        fclose(f);
        free(ref);
        nf++;
        if (mism) { ff++; total_mism += mism; printf("%s %s %s %d %d: mismatch=%d\n", name, map, mode, fx, fy, mism); }
    }
    if (fz) fclose(fz);
    if (nf) printf("fuzz: %d scenes, %d with mismatches (%ld px)\n", nf, ff, total_mism);
    fails += ff;
    printf("%d/%d scenes compared, %d with mismatches\n", compared, (int)(sizeof scenes / sizeof *scenes), fails);
    return fails ? 1 : 0;
}
