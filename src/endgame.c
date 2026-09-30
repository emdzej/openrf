/* End-of-game sequence and high scores. EndGame 0x40f380, State_EndOfGameSequence 0x40f050,
   SetWinnerAndWinVideo 0x4370e0, ShowWinBannerWithPalette 0x437330 / DrawWinBanner 0x437170,
   Script_WinBannerAndVideo 0x4375d0 (script entry 0x455b00: fade-in 0, fade-out 500 ms),
   Mus_StartWinMusic 0x430b30, RecordHighScore 0x42d8b0. */
#include "endgame.h"
#include "exe.h"
#include "assets.h"
#include "movie.h"
#include "music.h"
#include "sfx.h"
#include "game/rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* File names from RFIRE.BIN: win movies 0x4559d0 (by win_movie_by_level), banners 0x455a08 (blue/green, lo/hi-res). */
static const char *win_movies[4], *banners[4];
static void endgame_tables_load(void)
{
    for (int i = 0; i < 4; i++) {
        win_movies[i] = exe_str(exe_u32(0x4559d0 + 4 * (uint32_t)i));
        banners[i] = exe_str(exe_u32(0x455a08 + 4 * (uint32_t)i));
    }
}
EXE_LOADER(endgame_tables_load)

void fade_begin(Fade *f, const RGB *pal, int32_t from, int32_t to, uint32_t ms)
{
    memcpy(f->pal, pal, sizeof f->pal);
    f->from = from;
    f->to = to;
    f->ms = ms;
    f->t0 = plat_ticks_ms();
    f->done = false;
}

/* SetFadeTarget(target, ms) on what is in the framebuffer: the palette is scaled (UpdatePaletteFade 0x436970). */
Step fade_step(Fade *fd)
{
    if (fd->done) return STEP_DONE;
    Framebuffer *fb = plat_fb();
    uint64_t t = plat_ticks_ms() - fd->t0;
    int32_t from = fd->from, to = fd->to;
    uint32_t ms = fd->ms;
    int32_t f = t >= ms ? to : from + (int32_t)((int64_t)(to - from) * (int64_t)t / (int64_t)(ms ? ms : 1));
    for (int i = 0; i < 256; i++)
        fb->palette[i] = (RGB){ (uint8_t)(fd->pal[i].r * f >> 16), (uint8_t)(fd->pal[i].g * f >> 16), (uint8_t)(fd->pal[i].b * f >> 16) };
    music_service();
    plat_present();
    if (t >= ms) fd->done = true;
    return STEP_FRAME;
}

/* The sequence, one state per blocking loop of the original. */
enum { E_IDLE, E_FADE_OUT, E_FADE_IN, E_MUSIC, E_MOVIE, E_LOSE_FADE };
static struct {
    int st;
    GameResult r;
    Fade fade;
    Image8 ban;
    bool have;
    int bx, by, movie;
    RGB pal[256];
    Movie *mv;
} E;

void endgame_begin(const GameResult *r)
{
    E.r = *r;
    E.st = E_FADE_OUT;                                             /* EndGame: SetFadeTarget(0, 1000) */
    fade_begin(&E.fade, plat_fb()->palette, 0x10000, 0, 1000);
}

static void start_sequence(void)
{
    Framebuffer *fb = plat_fb();
    const GameResult *r = &E.r;
    if (r->winner == 0 || r->winner == 1) {
        /* case 0: Mus_StartWinMusic (all SFX stopped, priority reset, 13 "Win" at 0xff), SetWinnerAndWinVideo */
        sfx_cmd(SFX_STOP_ALL, 1, NULL, true);
        music_set_enabled(true);
        music_init();
        music_request(MUS_WIN, 0xff, 0);
        music_service();
        int lv = r->level < 0 ? 0 : r->level > 9 ? 9 : r->level;
        E.movie = win_movie_by_level[lv] > 3 ? 3 : win_movie_by_level[lv];
        int hires = fb->w >= 640;
        E.have = image_load_bmp8(banners[r->winner + (hires ? 2 : 0)], &E.ban);
        /* banner position DAT_00455a18[movie]: x < 0 centred, else doubled in 640 modes */
        E.bx = E.by = 0;
        if (E.have) {
            E.bx = win_banner_pos[E.movie][0] < 0 ? (fb->w - E.ban.w) >> 1 : win_banner_pos[E.movie][0] * (hires ? 2 : 1);
            E.by = win_banner_pos[E.movie][1] < 0 ? (fb->h - E.ban.h) >> 1 : win_banner_pos[E.movie][1] * (hires ? 2 : 1);
            if (E.bx + E.ban.w > fb->w) E.bx = fb->w - E.ban.w;
            if (E.by + E.ban.h > fb->h) E.by = fb->h - E.ban.h;
        }
        /* case 1: ClearAndPresent + ShowWinBannerWithPalette; case 2: fade in 500 ms; case 3: until the music ends */
        memset(fb->pixels, 0, (size_t)fb->w * (size_t)fb->h);
        memset(E.pal, 0, sizeof E.pal);
        if (E.have) {
            memcpy(E.pal, E.ban.palette, sizeof E.pal);
            for (int j = 0; j < E.ban.h; j++)                        /* colour-keyed blit (index 0) */
                for (int i = 0; i < E.ban.w; i++) {
                    uint8_t c = E.ban.pixels[j * E.ban.w + i];
                    int x = E.bx + i, y = E.by + j;
                    if (c && x >= 0 && y >= 0 && x < fb->w && y < fb->h) fb->pixels[y * fb->w + x] = c;
                }
        }
        fade_begin(&E.fade, E.pal, 0, 0x10000, 500);
        E.st = E_FADE_IN;
    } else {
        music_set_enabled(false);                                  /* no winner: Mus_Disable, straight to case 6 */
        music_service();
        fade_begin(&E.fade, fb->palette, 0x10000, 0, 1000);
        E.st = E_LOSE_FADE;
    }
}

static void free_banner(void)
{
    if (E.have) image_free(&E.ban);
    E.have = false;
}

static Step finish(void)
{
    E.st = E_IDLE;
    music_set_enabled(true);                                       /* State_EnterBackScreen: Mus_Enable */
    return STEP_DONE;
}

Step endgame_step(void)
{
    Framebuffer *fb = plat_fb();
    Step s;
    switch (E.st) {
    case E_FADE_OUT:
        if ((s = fade_step(&E.fade)) != STEP_DONE) return s;
        start_sequence();
        return STEP_AGAIN;
    case E_FADE_IN:
        if ((s = fade_step(&E.fade)) != STEP_DONE) return s;
        E.st = E_MUSIC;
        return STEP_AGAIN;
    case E_MUSIC:
        if (music_is_playing()) {
            music_service();
            plat_present();
            return STEP_FRAME;
        }
        music_set_enabled(false);                                  /* Mus_Disable */
        /* cases 4/5: StartWinScript -> Script_WinBannerAndVideo: the movie with the banner drawn over it */
        if (E.have) {
            MovieOverlay ov = { E.ban.pixels, E.ban.w, E.ban.h, E.bx, E.by, E.pal, fb->w, fb->h, 500 };
            E.mv = movie_open(win_movies[E.movie], &ov);
        } else E.mv = movie_open(win_movies[E.movie], NULL);
        E.st = E_MOVIE;
        return STEP_AGAIN;
    case E_MOVIE:
        if (E.mv && (s = movie_step(E.mv)) != STEP_DONE) return s;
        movie_close(E.mv);
        E.mv = NULL;
        free_banner();
        memset(fb->pixels, 0, (size_t)fb->w * (size_t)fb->h);
        return finish();
    case E_LOSE_FADE:
        if ((s = fade_step(&E.fade)) != STEP_DONE) return s;
        return finish();
    }
    return STEP_DONE;
}

void endgame_cancel(void)
{
    movie_close(E.mv);
    E.mv = NULL;
    free_banner();
    E.st = E_IDLE;
}
