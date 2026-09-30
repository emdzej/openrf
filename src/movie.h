#pragma once
#include <stdbool.h>
#include "platform.h"
/* Play a .STM movie in TITLE/. Returns false if the user quit the app.
   Any key skips, like the original. */
bool movie_play(const char *rel);

/* An 8-bit image blitted over every frame (colour key: index 0), in output pixels: with an overlay the
   320x240 movie is presented pixel-doubled to out_w x out_h. */
typedef struct {
    const uint8_t *pixels;
    int w, h, x, y;
    const RGB *palette;
    int out_w, out_h;
    int fade_out_ms;       /* after the last frame: fade the picture out (win script entry +0x10) */
} MovieOverlay;
/* Script_WinBannerAndVideo 0x4375d0 style playback: the movie with the banner drawn on top. */
bool movie_play_overlay(const char *rel, const MovieOverlay *ov);
