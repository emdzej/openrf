/* OpenRF — native reimplementation of Return Fire (1996, Silent Software).
   Requires the original game data (the CD, extracted or as a disc image).

   The front end as a frame-driven state machine (app.h): intro (stills and movies, the table at
   0x455A40 in the original), title / back screen, levels with their end sequence, the 1-player map
   progression, and the development map viewer. Each state's step function is one iteration of the
   blocking loop the port used to run for it. */
#include "app.h"
#include "platform.h"
#include "assets.h"
#include "audio.h"
#include "exe.h"
#include "input.h"
#include "movie.h"
#include "sprites.h"
#include "viewer.h"
#include "music.h"
#include "sfx.h"
#include "play.h"
#include "play_rules.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        if (vfs_exists(out)) return true;
    }
    return false;
}

static int first_map_of_level(int k, bool two)
{
    char p[128];
    for (int n = two ? 101 : 1; n <= (two ? 204 : 100); n++) {
        snprintf(p, sizeof p, "WORLDS/%s/LEVEL%d/RFMAP%03d.RFM", two ? "2PLAYER" : "1PLAYER", k, n);
        if (vfs_exists(p)) return n;
    }
    return 0;
}

static bool has_flag(int argc, char **argv, const char *f)
{
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], f)) return true;
    return false;
}

/* Blit an 8-bit image centred on a black framebuffer, with its palette. */
static bool show_image(const char *rel)
{
    Image8 img;
    if (!image_load_bmp8(rel, &img)) return false;
    Framebuffer *fb = plat_fb();
    memcpy(fb->palette, img.palette, sizeof fb->palette);
    memset(fb->pixels, 0, (size_t)fb->w * fb->h);
    image_blit(&img, fb, (fb->w - img.w) / 2, (fb->h - img.h) / 2);
    image_free(&img);
    return true;
}

enum { A_INTRO, A_START, A_VIEWER, A_MENU, A_TITLE, A_PLAY, A_QUIT };

/* Intro sequence, from the table at 0x455A40 in the original: stills (shown until a key or the timeout)
   and movies. */
static const struct { const char *rel; uint32_t still_ms; } intro[] = {
    { "TITLE/EMI.RFA", 3000 }, { "TITLE/TWI.STM", 0 }, { "TITLE/PROLIFIC.STM", 0 },
    { "TITLE/SILENT.RFA", 2500 }, { "TITLE/RF.STM", 0 },
};

static struct {
    int st, argc;
    char **argv;
    int intro;                   /* current intro item */
    bool item_open;
    uint64_t still_t0;
    Movie *mv;
    SpriteBank sb;
    bool have_sb, sfx_on;
    char level[512], level2[512];
    int mapno;                   /* 0: a custom path or a 2-player map (no progression) */
    int play_now;                /* --play (1) / --play2 (2): straight into the map, once */
    const char *map;
    Play *play;
} A;

/* ------------------------------------------------------------------ intro */

static Step intro_step(void)
{
    if (A.intro >= (int)(sizeof intro / sizeof *intro)) { A.st = A_START; return STEP_AGAIN; }
    uint32_t still = intro[A.intro].still_ms;
    if (!A.item_open) {
        A.item_open = true;
        if (still) {
            if (!show_image(intro[A.intro].rel)) { fprintf(stderr, "cannot load %s\n", intro[A.intro].rel); still = 0; }
            A.still_t0 = plat_ticks_ms();
        } else A.mv = movie_open(intro[A.intro].rel, NULL);
    }
    Step s = STEP_DONE;
    if (still) {                                       /* until a key is pressed or the timeout expires */
        if (plat_ticks_ms() - A.still_t0 < still && !plat_any_key_pressed()) {
            music_service();
            plat_present();
            s = STEP_FRAME;
        }
    } else if (A.mv) s = movie_step(A.mv);
    if (s != STEP_DONE) return s;
    movie_close(A.mv);
    A.mv = NULL;
    A.item_open = false;
    A.intro++;
    return STEP_AGAIN;
}

/* ------------------------------------------------------------------ title and levels */

/* Title / back screen (State_EnterBackScreen: HiBckEng.RFA + Drums). */
static bool title_enter(void)
{
    music_set_enabled(true);
    music_request(MUS_DRUMS, 0x32, 0);   /* front-end theme (FUN_0040ed70) */
    return show_image("ART/HIBCKENG.RFA");
}

static void start_play(void)
{
    A.play_now = 0;
    fprintf(stderr, "starting %s\n", A.map);
    A.play = play_begin(&A.sb, A.map);
    A.st = A_PLAY;
}

/* F2 (-1): current map, F3 (-2): current 2-player map, 1..9 a level key, 11..19 Shift + a level key. */
static void title_pick(int k)
{
    A.map = A.level;
    if (k == -2 || k > 10) {                           /* 2 players */
        int n = k > 10 ? first_map_of_level(k - 10, true) : 0;
        if (n) map_path(n, A.level2, sizeof A.level2);
        A.map = A.level2;
    } else {
        int n = k > 0 ? first_map_of_level(k, false) : 0;
        if (n && map_path(n, A.level, sizeof A.level)) A.mapno = n;
    }
    start_play();
}

static Step title_step(void)
{
    int k = 0;
    if (input_ui(UI_PLAY1)) k = -1;
    else if (input_ui(UI_PLAY2)) k = -2;
    else {
        bool shift = plat_key_down(KEY_LSHIFT) || plat_key_down(KEY_RSHIFT);
        for (int i = 1; i <= 9 && !k; i++) if (plat_key_down(KEY_1 + i - 1)) k = shift ? 10 + i : i;
    }
    if (k) { title_pick(k); return STEP_AGAIN; }
    music_service();
    plat_present();
    return STEP_FRAME;
}

static void menu(void)
{
    A.map = A.level;
    if (A.play_now == 2) { A.map = A.level2; start_play(); }
    else if (A.play_now) start_play();
    else if (title_enter()) A.st = A_TITLE;
    else A.st = A_QUIT;
}

static void level_over(void)
{
    char next[512];                                    /* won: on to the next 1-player map */
    if (A.map == A.level && play_outcome.winner == 0 && play_outcome.nplayers == 1 && A.mapno > 0 && A.mapno < 100 &&
        map_path(A.mapno + 1, next, sizeof next)) {
        A.mapno++;
        snprintf(A.level, sizeof A.level, "%s", next);
        fprintf(stderr, "next map: %s\n", A.level);
    }
}

/* After the intro: sprites, options, viewer. */
static void start(void)
{
    int argc = A.argc;
    char **argv = A.argv;
    A.have_sb = sprites_load(&A.sb, "ART/ART.CAR");
    if (!A.have_sb) fprintf(stderr, "cannot load ART.CAR\n");
    snprintf(A.level, sizeof A.level, "WORLDS/1PLAYER/LEVEL1/RFMAP001.RFM");
    snprintf(A.level2, sizeof A.level2, "WORLDS/2PLAYER/LEVEL1/RFMAP101.RFM");   /* "Driving School" */
    A.mapno = 1;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--level")) {
            char *end, buf[512];
            long n = strtol(argv[i + 1], &end, 10);
            if (*end == 0 && n >= 1 && n <= 204 && map_path((int)n, buf, sizeof buf)) {
                if (n > 100) snprintf(A.level2, sizeof A.level2, "%s", buf);
                snprintf(A.level, sizeof A.level, "%s", buf);
                A.mapno = n > 100 ? 0 : (int)n;
            } else {
                snprintf(A.level, sizeof A.level, "%s", argv[i + 1]);
                const char *q = strstr(A.level, "RFMAP");
                A.mapno = q && strstr(A.level, "1PLAYER") ? atoi(q + 5) : 0;
                if (strstr(A.level, "2PLAYER") || strstr(A.level, "2Player")) snprintf(A.level2, sizeof A.level2, "%s", A.level);
            }
        }
    /* --play: straight into the --level map (1 or 2 players by its header); --play2: the 2-player map. */
    A.play_now = has_flag(argc, argv, "--play2") ? 2 : has_flag(argc, argv, "--play") ? 1 : 0;
    if (!A.have_sb) { A.st = A_QUIT; return; }
    if (has_flag(argc, argv, "--viewer") && viewer_begin(&A.sb)) { A.st = A_VIEWER; return; }
    music_init();
    A.st = A_MENU;
}

static Step app_step(void)
{
    Step s;
    switch (A.st) {
    case A_INTRO:
        return intro_step();
    case A_START:
        start();
        return STEP_AGAIN;
    case A_VIEWER:
        if ((s = viewer_step()) != STEP_DONE) return s;
        viewer_end();
        music_init();
        A.st = A_MENU;
        return STEP_AGAIN;
    case A_MENU:
        menu();
        return STEP_AGAIN;
    case A_TITLE:
        return title_step();
    case A_PLAY:
        if (A.play && (s = play_step(A.play)) != STEP_DONE) return s;
        play_end(A.play);                              /* NULL: the level didn't load */
        A.play = NULL;
        level_over();
        A.st = A_MENU;
        return STEP_AGAIN;
    }
    return STEP_DONE;
}

/* ------------------------------------------------------------------ app.h */

bool app_init(int argc, char **argv)
{
    memset(&A, 0, sizeof A);
    A.argc = argc;
    A.argv = argv;
    if (!vfs_exists("ART/ART.CAR")) {
        plat_error("Return Fire", "Return Fire game data was not found (the CD, extracted or as a disc image).");
        return false;
    }
    /* The original game program (RFIRE.BIN, CD root) supplies the models, shapes and descriptor tables. */
    if (!exe_load()) {
        fprintf(stderr, "%s\n", exe_error());
        plat_error("Return Fire", exe_error());
        return false;
    }
    if (!plat_init("Return Fire", 640, 480)) return false;
    if (sfx_init()) {
        A.sfx_on = true;
        sfx_set_lock_fns(audio_lock, audio_unlock);
        audio_set_sfx(sfx_render);
    }
    A.st = has_flag(argc, argv, "--skip-intro") ? A_START : A_INTRO;
    return true;
}

bool app_frame(void)
{
    /* Steps that change state present nothing; bound them so a broken state can't spin forever. */
    for (int i = 0; i < 256 && A.st != A_QUIT; i++) {
        if (!plat_poll()) { A.st = A_QUIT; break; }
        if (app_step() == STEP_FRAME) return A.st != A_QUIT;
    }
    return A.st != A_QUIT;
}

void app_exit(void)
{
    movie_close(A.mv);
    A.mv = NULL;
    if (A.st == A_VIEWER) viewer_end();
    play_end(A.play);
    A.play = NULL;
    if (A.have_sb) sprites_free(&A.sb);
    A.have_sb = false;
    audio_set_sfx(NULL);
    audio_music_stop();
    audio_pcm_close();
    if (A.sfx_on) sfx_shutdown();
    A.sfx_on = false;
    plat_shutdown();
}
