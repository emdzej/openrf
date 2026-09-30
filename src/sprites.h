/* ART/ART.CAR sprite bank (3DO CCB image, see docs/car.md). */
#pragma once
#include "platform.h"
#include <stddef.h>

typedef struct {
    uint32_t flags, mode, param;
    int w, h;
    const uint8_t *pixels;
    uint32_t src, plut;        /* CCB +0x08 SourcePtr / +0x0C PLUTPtr (file offsets) */
} Sprite;

typedef struct {
    uint8_t *data;
    size_t size;               /* bytes in data */
    int count;
    Sprite *spr;
    RGB palette[256];          /* DirectDraw palette: sprite pixel values index this */
    /* TRANS.TBL (Pal_InitTransTables 0x4090c0): average-blend, 32 darken, 32 brighten tables. */
    uint8_t blend[256][256];
    uint8_t shade[32][256];
    uint8_t bright[32][256];
} SpriteBank;

bool sprites_load(SpriteBank *b, const char *rel);   /* also loads or builds ART/TRANS.TBL */
void sprites_free(SpriteBank *b);
/* Draw sprite with its top-left at (x, y), clipped to the framebuffer. */
void sprite_draw(const SpriteBank *b, int idx, Framebuffer *fb, int x, int y);
