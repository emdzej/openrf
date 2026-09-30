/* Development map viewer: the perspective world view (src/render) at the bunker camera
   (View_EnterBunkerCam 0x404ee0: pitch 0x180000, H 0), 1P viewport 320x152 rendered at 2x like the
   original hi-res mode, with the 1P status-bar background under it (DrawStatusBarBackground 0x437f90).
   Arrows (pad: d-pad) move the tracked point (Shift = faster), [ and ] (L / R) switch map, Esc
   (START + SELECT) returns. */
#include "viewer.h"
#include "world.h"
#include "sprites.h"
#include "assets.h"
#include "music.h"
#include "clock.h"
#include "render/render.h"
#include "input.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct { char (*paths)[64]; int n, max; const char *dir; } MapList;

static bool contains_rfm(const char *s)             /* strcasestr(s, ".RFM"), which not every libc has */
{
    for (; *s; s++) if (!strncasecmp(s, ".RFM", 4)) return true;
    return false;
}

static void add_map(const char *name, bool is_dir, void *user)
{
    MapList *l = user;
    if (!is_dir && contains_rfm(name) && l->n < l->max) snprintf(l->paths[l->n++], 64, "%s/%s", l->dir, name);
}

static int map_paths(char paths[][64], int max)
{
    MapList l = { paths, 0, max, NULL };
    const char *groups[] = { "WORLDS/1PLAYER", "WORLDS/2PLAYER" };
    for (int g = 0; g < 2; g++)
        for (int lv = 1; lv <= 9; lv++) {
            char dir[64];
            snprintf(dir, sizeof dir, "%s/LEVEL%d", groups[g], lv);
            l.dir = dir;
            vfs_list(dir, add_map, &l);
        }
    qsort(paths, (size_t)l.n, 64, (int (*)(const void *, const void *))strcasecmp);
    return l.n;
}

/* The status-bar RFA is blitted to the bottom of the back buffer. Its pixels index the game
   palette, which the DirectDraw palette holds shifted by +10 (LoadBmpSurfaceCached(..., 10)). */
static void draw_statusbar(const Image8 *img, Framebuffer *fb)
{
    int y0 = fb->h - img->h;
    for (int j = 0; j < img->h; j++) {
        if (y0 + j < 0) continue;
        const uint8_t *s = img->pixels + j * img->w;
        uint8_t *d = fb->pixels + (y0 + j) * fb->w;
        for (int i = 0; i < img->w && i < fb->w; i++) d[i] = (uint8_t)(s[i] < 236 ? s[i] + 10 : s[i]);
    }
}

static struct {
    char paths[256][64];
    int n, cur, loaded, scale;
    World *w;
    CellMap *cm;
    Image8 bar;
    bool have_bar, prev_l, prev_r;
    View v;
    int32_t target[3];
    Clock clk;
} V;

bool viewer_begin(const SpriteBank *sb)
{
    V.n = map_paths(V.paths, 256);
    V.cur = 0;
    if (!V.n) { fprintf(stderr, "no maps found\n"); return false; }
    V.w = malloc(sizeof *V.w);
    V.cm = malloc(sizeof *V.cm);
    Framebuffer *fb = plat_fb();
    V.scale = fb->w >= 640 ? 2 : 1;
    memcpy(fb->palette, sb->palette, sizeof fb->palette);
    V.have_bar = image_load_bmp8(V.scale == 2 ? "ART/1PBSCRH.RFA" : "ART/1PBSCRL.RFA", &V.bar);
    render_init(sb);
    memset(&V.v, 0, sizeof V.v);
    memset(V.target, 0, sizeof V.target);
    V.loaded = -1;
    V.prev_l = V.prev_r = false;
    clock_start(&V.clk);
    return true;
}

Step viewer_step(void)
{
    Framebuffer *fb = plat_fb();
    View *v = &V.v;
    int32_t *target = V.target;
    if (V.loaded != V.cur) {
        const char *path = V.paths[V.cur];
        if (!world_load(V.w, path) || !cellmap_load(V.cm, path)) fprintf(stderr, "bad map %s\n", path);
        fprintf(stderr, "%s: \"%s\" by %s, %dP, level %d\n", path, V.w->name, V.w->author, V.w->players, V.w->level);
        int t = V.cm->npads[0] ? 0 : 1;
        int px = V.cm->npads[t] ? V.cm->pad_x[t][0] : 64, py = V.cm->npads[t] ? V.cm->pad_y[t][0] : 64;
        target[0] = (px * 32 + 16) << 16;
        target[1] = (py * 32 + 16) << 16;
        view_init(v, 0, 0, 320, 152);
        camera_add_tracker(v, 0, target, NULL, 0, 0x180000, 0, true, NULL);
        camera_snap(v, target, 0, 0x180000);
        V.loaded = V.cur;
        music_request(MUS_BUNKER, 0x80, 0);   /* game start (FUN_0040cbb0) */
    }
    if (input_ui(UI_BACK)) return STEP_DONE;
    int dt = clock_frame(&V.clk);
    int32_t sp = (plat_key_down(KEY_LSHIFT) ? 12 : 4) * dt << 16;
    uint32_t pad = plat_pad(0);
    if (plat_key_down(KEY_LEFT) || (pad & PAD_LEFT)) target[0] -= sp;
    if (plat_key_down(KEY_RIGHT) || (pad & PAD_RIGHT)) target[0] += sp;
    if (plat_key_down(KEY_UP) || (pad & PAD_UP)) target[1] -= sp;
    if (plat_key_down(KEY_DOWN) || (pad & PAD_DOWN)) target[1] += sp;
    bool l = plat_key_down(KEY_LEFTBRACKET) || (pad & PAD_L), r = plat_key_down(KEY_RIGHTBRACKET) || (pad & PAD_R);
    if (l && !V.prev_l) V.cur = (V.cur + V.n - 1) % V.n;
    if (r && !V.prev_r) V.cur = (V.cur + 1) % V.n;
    V.prev_l = l; V.prev_r = r;
    music_service();
    camera_update(v, dt);
    RenderMap rm = cellmap_view(V.cm);
    memset(fb->pixels, 0, (size_t)fb->w * fb->h);
    render_world(v, &rm, fb, V.scale);
    if (V.have_bar) draw_statusbar(&V.bar, fb);
    plat_present();
    return STEP_FRAME;
}

void viewer_end(void)
{
    if (V.have_bar) image_free(&V.bar);
    V.have_bar = false;
    view_free(&V.v);
    free(V.cm);
    free(V.w);
    V.cm = NULL;
    V.w = NULL;
}
