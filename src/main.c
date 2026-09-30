/* OpenRF — native reimplementation of Return Fire (1996, Silent Software).
   Requires the original game data (the extracted CD). */
#include "platform.h"
#include "assets.h"
#include "exe.h"
#include "movie.h"
#include "sprites.h"
#include "viewer.h"
#include "music.h"
#include "sfx.h"
#include "play.h"
#include "play_rules.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <libgen.h>

/* Look for data next to the executable (inside the .app: Contents/Resources/data),
   then ./cd, or a path given on the command line. */
static bool find_data_root(int argc, char **argv)
{
    char cand[PATH_MAX];
    if (argc > 1 && argv[1][0] != '-') { assets_set_root(argv[1]); return access(assets_path("ART/ART.CAR"), R_OK) == 0; }
    char exe[PATH_MAX]; uint32_t n = sizeof exe;
    const char *tries[] = { "../Resources/data", "../../../cd", "../../../../cd", "cd", "../cd" };
    if (_NSGetExecutablePath(exe, &n) == 0) {
        char *dir = dirname(exe);
        for (size_t i = 0; i < sizeof tries / sizeof *tries; i++) {
            snprintf(cand, sizeof cand, "%s/%s", dir, tries[i]);
            assets_set_root(cand);
            if (access(assets_path("ART/ART.CAR"), R_OK) == 0) return true;
        }
    }
    assets_set_root("cd");
    return access(assets_path("ART/ART.CAR"), R_OK) == 0;
}

/* Show an 8-bit image centred, until a key is pressed or timeout expires.
   If wait_key is nonzero only that scancode dismisses it. */
static bool show_still_key(const char *rel, uint32_t timeout_ms, int wait_key)
{
    Image8 img;
    if (!image_load_bmp8(rel, &img)) { fprintf(stderr, "cannot load %s\n", rel); return true; }
    Framebuffer *fb = plat_fb();
    memcpy(fb->palette, img.palette, sizeof fb->palette);
    memset(fb->pixels, 0, (size_t)fb->w * fb->h);
    image_blit(&img, fb, (fb->w - img.w) / 2, (fb->h - img.h) / 2);
    image_free(&img);
    uint64_t t0 = plat_ticks_ms();
    while (plat_ticks_ms() - t0 < timeout_ms) {
        if (!plat_poll()) return false;
        if (wait_key ? plat_key_down(wait_key) : plat_any_key_pressed()) break;
        music_service();
        plat_present();
    }
    return true;
}

static bool show_still(const char *rel, uint32_t timeout_ms) { return show_still_key(rel, timeout_ms, 0); }

/* 1-player maps WORLDS/1PLAYER/LEVELk/RFMAPnnn.RFM, n = 1..100 over the nine difficulty levels, 2-player
   maps WORLDS/2PLAYER/LEVELk/RFMAPnnn.RFM, n = 101..204. The Win32 front end picks maps in the level-select
   dialog (0x3f4; the player count comes from the map header, LoadLevelMap). Here: after a 1-player win the
   next map, keys 1-9 on the title screen start difficulty level k, Shift+1-9 the 2-player level k, F2 the
   current map, F3 the current 2-player map, --level <n|path>. */
static bool map_path(int n, char *out, size_t sz)
{
    const char *dir = n > 100 ? "2PLAYER" : "1PLAYER";
    for (int k = 1; k <= 9; k++) {
        snprintf(out, sz, "WORLDS/%s/LEVEL%d/RFMAP%03d.RFM", dir, k, n);
        if (access(assets_path(out), R_OK) == 0) return true;
    }
    return false;
}

static int first_map_of_level(int k, bool two)
{
    char p[128];
    for (int n = two ? 101 : 1; n <= (two ? 204 : 100); n++) {
        snprintf(p, sizeof p, "WORLDS/%s/LEVEL%d/RFMAP%03d.RFM", two ? "2PLAYER" : "1PLAYER", k, n);
        if (access(assets_path(p), R_OK) == 0) return n;
    }
    return 0;
}

/* Title / back screen (State_EnterBackScreen: HiBckEng.RFA + Drums). Returns 0 quit, -1 F2 (current map),
   -2 F3 (current 2-player map), 1..9 a level key, 11..19 Shift + a level key (2 players). */
static int title_screen(void)
{
    music_set_enabled(true);
    music_request(MUS_DRUMS, 0x32, 0);   /* front-end theme (FUN_0040ed70) */
    Image8 img;
    if (!image_load_bmp8("ART/HIBCKENG.RFA", &img)) return 0;
    Framebuffer *fb = plat_fb();
    memcpy(fb->palette, img.palette, sizeof fb->palette);
    memset(fb->pixels, 0, (size_t)fb->w * fb->h);
    image_blit(&img, fb, (fb->w - img.w) / 2, (fb->h - img.h) / 2);
    image_free(&img);
    for (;;) {
        if (!plat_poll()) return 0;
        if (plat_key_down(SDL_SCANCODE_F2)) return -1;
        if (plat_key_down(SDL_SCANCODE_F3)) return -2;
        bool shift = plat_key_down(SDL_SCANCODE_LSHIFT) || plat_key_down(SDL_SCANCODE_RSHIFT);
        for (int k = 1; k <= 9; k++) if (plat_key_down(SDL_SCANCODE_1 + k - 1)) return shift ? 10 + k : k;
        music_service();
        plat_present();
    }
}

static bool has_flag(int argc, char **argv, const char *f)
{
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], f)) return true;
    return false;
}

int main(int argc, char **argv)
{
    if (!find_data_root(argc, argv)) {
        const char *msg = "Return Fire game data was not found.\n\n"
                          "Put the extracted CD (the folder containing RFIRE.BIN, ART, SOUND, TITLE and WORLDS) at "
                          "Return Fire.app/Contents/Resources/data, or pass its path as an argument.";
        fprintf(stderr, "%s\n", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Return Fire", msg, NULL);
        return 1;
    }
    /* The original game program (RFIRE.BIN, CD root) supplies the models, shapes and descriptor tables. */
    if (!exe_load()) {
        fprintf(stderr, "%s\n", exe_error());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Return Fire", exe_error(), NULL);
        return 1;
    }
    if (!plat_init("Return Fire", 640, 480)) return 1;
    if (sfx_init() && plat_sfx_open(sfx_render)) sfx_set_lock_fns(plat_sfx_lock, plat_sfx_unlock);

    /* Intro sequence, from the table at 0x455A40 in the original. */
    bool ok = has_flag(argc, argv, "--skip-intro") || (show_still("TITLE/EMI.RFA", 3000)
           && movie_play("TITLE/TWI.STM")
           && movie_play("TITLE/PROLIFIC.STM")
           && show_still("TITLE/SILENT.RFA", 2500)
           && movie_play("TITLE/RF.STM"));
    SpriteBank sb;
    if (!sprites_load(&sb, "ART/ART.CAR")) { fprintf(stderr, "cannot load ART.CAR\n"); ok = false; }
    static char level[512] = "WORLDS/1PLAYER/LEVEL1/RFMAP001.RFM";
    static char level2[512] = "WORLDS/2PLAYER/LEVEL1/RFMAP101.RFM";   /* "Driving School" */
    int mapno = 1;                                   /* 0: a custom path or a 2-player map (no progression) */
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--level")) {
            char *end, buf[512];
            long n = strtol(argv[i + 1], &end, 10);
            if (*end == 0 && n >= 1 && n <= 204 && map_path((int)n, buf, sizeof buf)) {
                if (n > 100) snprintf(level2, sizeof level2, "%s", buf);
                snprintf(level, sizeof level, "%s", buf);
                mapno = n > 100 ? 0 : (int)n;
            } else {
                snprintf(level, sizeof level, "%s", argv[i + 1]);
                const char *q = strstr(level, "RFMAP");
                mapno = q && strstr(level, "1PLAYER") ? atoi(q + 5) : 0;
                if (strstr(level, "2PLAYER") || strstr(level, "2Player")) snprintf(level2, sizeof level2, "%s", level);
            }
        }
    if (ok && has_flag(argc, argv, "--viewer")) ok = viewer_run(&sb);
    music_init();
    /* --play: straight into the --level map (1 or 2 players by its header); --play2: the 2-player map. */
    int play_now = has_flag(argc, argv, "--play2") ? 2 : has_flag(argc, argv, "--play") ? 1 : 0;
    while (ok) {
        const char *map = level;
        if (play_now == 2) map = level2;
        else if (!play_now) {
            int k = title_screen();
            if (k == 0) break;
            if (k == -2 || k > 10) {                     /* 2 players */
                int n = k > 10 ? first_map_of_level(k - 10, true) : 0;
                if (n) map_path(n, level2, sizeof level2);
                map = level2;
            } else {
                int n = k > 0 ? first_map_of_level(k, false) : 0;
                if (n && map_path(n, level, sizeof level)) mapno = n;
            }
        }
        play_now = 0;
        fprintf(stderr, "starting %s\n", map);
        if (!play_run(&sb, map)) break;
        char next[512];                              /* won: on to the next 1-player map */
        if (map == level && play_outcome.winner == 0 && play_outcome.nplayers == 1 && mapno > 0 && mapno < 100 &&
            map_path(mapno + 1, next, sizeof next)) {
            mapno++;
            snprintf(level, sizeof level, "%s", next);
            fprintf(stderr, "next map: %s\n", level);
        }
    }
    sprites_free(&sb);

    plat_sfx_close();
    sfx_shutdown();
    plat_shutdown();
    return 0;
}
