#pragma once
#include <stdbool.h>
#include "app.h"
#include "platform.h"

/* An 8-bit image blitted over every frame (colour key: index 0), in output pixels: with an overlay the
   320x240 movie is presented pixel-doubled to out_w x out_h. The pointed-to pixels and palette must stay
   valid while the movie plays. */
typedef struct {
    const uint8_t *pixels;
    int w, h, x, y;
    const RGB *palette;
    int out_w, out_h;
    int fade_out_ms;       /* after the last frame: fade the picture out (win script entry +0x10) */
} MovieOverlay;

/* .STM playback (a movie in TITLE/), stepped once per frame. Any key skips, like the original.
   movie_open returns NULL if the movie can't be read (the caller just goes on). ov may be NULL; with an
   overlay: Script_WinBannerAndVideo 0x4375d0 style playback, the movie with the banner drawn on top. */
typedef struct Movie Movie;
Movie *movie_open(const char *rel, const MovieOverlay *ov);
Step movie_step(Movie *m);
void movie_close(Movie *m);
