/* Development map viewer: the perspective world view (src/render) at the bunker camera
   (View_EnterBunkerCam 0x404ee0: pitch 0x180000, H 0), 1P viewport 320x152 rendered at 2x like the
   original hi-res mode, with the 1P status-bar background under it (DrawStatusBarBackground 0x437f90).
   Arrows move the tracked point (Shift = faster), [ and ] switch map, Esc returns. */
#include "viewer.h"
#include "world.h"
#include "sprites.h"
#include "assets.h"
#include "music.h"
#include "clock.h"
#include "render/render.h"
#include <SDL3/SDL_scancode.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int map_paths(char paths[][64], int max)
{
    int n = 0;
    const char *groups[] = { "WORLDS/1PLAYER", "WORLDS/2PLAYER" };
    for (int g = 0; g < 2; g++)
        for (int lv = 1; lv <= 9; lv++) {
            char dir[64];
            snprintf(dir, sizeof dir, "%s/LEVEL%d", groups[g], lv);
            DIR *d = opendir(assets_path(dir));
            struct dirent *e;
            while (d && (e = readdir(d)) && n < max)
                if (strcasestr(e->d_name, ".RFM")) snprintf(paths[n++], 64, "%s/%s", dir, e->d_name);
            if (d) closedir(d);
        }
    qsort(paths, (size_t)n, 64, (int (*)(const void *, const void *))strcasecmp);
    return n;
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

bool viewer_run(const SpriteBank *sb)
{
    static char paths[256][64];
    int n = map_paths(paths, 256), cur = 0;
    if (!n) { fprintf(stderr, "no maps found\n"); return true; }
    World *w = malloc(sizeof *w);
    CellMap *cm = malloc(sizeof *cm);
    Framebuffer *fb = plat_fb();
    int scale = fb->w >= 640 ? 2 : 1;
    memcpy(fb->palette, sb->palette, sizeof fb->palette);
    Image8 bar;
    bool have_bar = image_load_bmp8(scale == 2 ? "ART/1PBSCRH.RFA" : "ART/1PBSCRL.RFA", &bar);
    render_init(sb);
    View v = { 0 };
    int32_t target[3] = { 0, 0, 0 };
    int loaded = -1;
    bool prev_l = false, prev_r = false, quit = false;
    Clock clk;
    clock_start(&clk);
    for (;;) {
        if (loaded != cur) {
            if (!world_load(w, paths[cur]) || !cellmap_load(cm, paths[cur])) fprintf(stderr, "bad map %s\n", paths[cur]);
            fprintf(stderr, "%s: \"%s\" by %s, %dP, level %d\n", paths[cur], w->name, w->author, w->players, w->level);
            int t = cm->npads[0] ? 0 : 1;
            int px = cm->npads[t] ? cm->pad_x[t][0] : 64, py = cm->npads[t] ? cm->pad_y[t][0] : 64;
            target[0] = (px * 32 + 16) << 16;
            target[1] = (py * 32 + 16) << 16;
            view_init(&v, 0, 0, 320, 152);
            camera_add_tracker(&v, 0, target, NULL, 0, 0x180000, 0, true, NULL);
            camera_snap(&v, target, 0, 0x180000);
            loaded = cur;
            music_request(MUS_BUNKER, 0x80, 0);   /* game start (FUN_0040cbb0) */
        }
        if (!plat_poll()) { quit = true; break; }
        if (plat_key_down(SDL_SCANCODE_ESCAPE)) break;
        int dt = clock_frame(&clk);
        int32_t sp = (plat_key_down(SDL_SCANCODE_LSHIFT) ? 12 : 4) * dt << 16;
        if (plat_key_down(SDL_SCANCODE_LEFT)) target[0] -= sp;
        if (plat_key_down(SDL_SCANCODE_RIGHT)) target[0] += sp;
        if (plat_key_down(SDL_SCANCODE_UP)) target[1] -= sp;
        if (plat_key_down(SDL_SCANCODE_DOWN)) target[1] += sp;
        bool l = plat_key_down(SDL_SCANCODE_LEFTBRACKET), r = plat_key_down(SDL_SCANCODE_RIGHTBRACKET);
        if (l && !prev_l) cur = (cur + n - 1) % n;
        if (r && !prev_r) cur = (cur + 1) % n;
        prev_l = l; prev_r = r;
        music_service();
        camera_update(&v, dt);
        RenderMap rm = cellmap_view(cm);
        memset(fb->pixels, 0, (size_t)fb->w * fb->h);
        render_world(&v, &rm, fb, scale);
        if (have_bar) draw_statusbar(&bar, fb);
        plat_present();
    }
    if (have_bar) image_free(&bar);
    view_free(&v);
    free(cm);
    free(w);
    return !quit;
}
