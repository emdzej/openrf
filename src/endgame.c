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
#include <sys/stat.h>

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

/* SetFadeTarget(target, ms) on what is in the framebuffer: the palette is scaled (UpdatePaletteFade 0x436970). */
static bool fade(const RGB *pal, int32_t from, int32_t to, uint32_t ms)
{
    Framebuffer *fb = plat_fb();
    uint64_t t0 = plat_ticks_ms();
    for (;;) {
        if (!plat_poll()) return false;
        uint64_t t = plat_ticks_ms() - t0;
        int32_t f = t >= ms ? to : from + (int32_t)((int64_t)(to - from) * (int64_t)t / (int64_t)(ms ? ms : 1));
        for (int i = 0; i < 256; i++)
            fb->palette[i] = (RGB){ (uint8_t)(pal[i].r * f >> 16), (uint8_t)(pal[i].g * f >> 16), (uint8_t)(pal[i].b * f >> 16) };
        music_service();
        plat_present();
        if (t >= ms) return true;
    }
}

bool endgame_fade_out(uint32_t ms)
{
    RGB pal[256];
    memcpy(pal, plat_fb()->palette, sizeof pal);
    return fade(pal, 0x10000, 0, ms);
}

bool endgame_sequence(const GameResult *r)
{
    Framebuffer *fb = plat_fb();
    bool ok = true;
    if (r->winner == 0 || r->winner == 1) {
        /* case 0: Mus_StartWinMusic (all SFX stopped, priority reset, 13 "Win" at 0xff), SetWinnerAndWinVideo */
        sfx_cmd(SFX_STOP_ALL, 1, NULL, true);
        music_set_enabled(true);
        music_init();
        music_request(MUS_WIN, 0xff, 0);
        music_service();
        int lv = r->level < 0 ? 0 : r->level > 9 ? 9 : r->level;
        int movie = win_movie_by_level[lv] > 3 ? 3 : win_movie_by_level[lv];
        int hires = fb->w >= 640;
        Image8 ban;
        bool have = image_load_bmp8(banners[r->winner + (hires ? 2 : 0)], &ban);
        /* banner position DAT_00455a18[movie]: x < 0 centred, else doubled in 640 modes */
        int bx = 0, by = 0;
        if (have) {
            bx = win_banner_pos[movie][0] < 0 ? (fb->w - ban.w) >> 1 : win_banner_pos[movie][0] * (hires ? 2 : 1);
            by = win_banner_pos[movie][1] < 0 ? (fb->h - ban.h) >> 1 : win_banner_pos[movie][1] * (hires ? 2 : 1);
            if (bx + ban.w > fb->w) bx = fb->w - ban.w;
            if (by + ban.h > fb->h) by = fb->h - ban.h;
        }
        /* case 1: ClearAndPresent + ShowWinBannerWithPalette; case 2: fade in 500 ms; case 3: until the music ends */
        memset(fb->pixels, 0, (size_t)fb->w * (size_t)fb->h);
        RGB pal[256] = { 0 };
        if (have) {
            memcpy(pal, ban.palette, sizeof pal);
            for (int j = 0; j < ban.h; j++)                          /* colour-keyed blit (index 0) */
                for (int i = 0; i < ban.w; i++) {
                    uint8_t c = ban.pixels[j * ban.w + i];
                    int x = bx + i, y = by + j;
                    if (c && x >= 0 && y >= 0 && x < fb->w && y < fb->h) fb->pixels[y * fb->w + x] = c;
                }
        }
        ok = fade(pal, 0, 0x10000, 500);
        while (ok && music_is_playing()) {
            if (!plat_poll()) { ok = false; break; }
            music_service();
            plat_present();
        }
        music_set_enabled(false);                                  /* Mus_Disable */
        /* cases 4/5: StartWinScript -> Script_WinBannerAndVideo: the movie with the banner drawn over it */
        if (ok) {
            MovieOverlay ov = { have ? ban.pixels : NULL, have ? ban.w : 0, have ? ban.h : 0, bx, by, pal,
                                fb->w, fb->h, 500 };
            ok = have ? movie_play_overlay(win_movies[movie], &ov) : movie_play(win_movies[movie]);
        }
        if (have) image_free(&ban);
        memset(fb->pixels, 0, (size_t)fb->w * (size_t)fb->h);
    } else {
        music_set_enabled(false);                                  /* no winner: Mus_Disable, straight to case 6 */
        music_service();
        ok = endgame_fade_out(1000);
    }
    music_set_enabled(true);                                       /* State_EnterBackScreen: Mus_Enable */
    return ok;
}

