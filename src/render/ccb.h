/* Patched CCB copies drawn with 3DO placement semantics: the UI / HUD path of the original
   (Cel_AddToList 0x401220 copies an ART.CAR CCB, the caller patches X/Y/HDX/VDY/mode, and
   Cel_DrawList 0x4262f0 computes the corners with Cel_ComputeCorners 0x4260a0 /
   Cel_ComputeCorners2x 0x426180 or takes explicit ones (flag 0x1000)). */
#pragma once
#include "render.h"

#define CCB_F_OPAQUE   0x20u          /* BGND: colour 0 drawn */
#define CCB_F_EXPLICIT 0x1000u        /* +0x10..+0x2c are 4 corner points */
#define CCB_F_SKIP     0x80000000u

typedef struct {
    int idx;                          /* source ART.CAR CCB (keyed-blend / PLUT lookups), -1 = none */
    uint32_t flags;                   /* +0x00 */
    const uint8_t *src;               /* +0x08 pixel pointer (may be offset for sub-rects) */
    const uint8_t *plut;              /* +0x0c mode 0x11 remap / mode 10 RGB555 table (NULL = sprite's) */
    int32_t v[8];                     /* +0x10..+0x2c: X, Y, HDX, HDY, VDX, VDY, HDDX, HDDY or 4 corners */
    int mode;                         /* +0x34 */
    int32_t p1;                       /* +0x38 */
    int w, h;                         /* +0x3c/+0x40 (w is also the source stride) */
} Ccb;
enum { CCB_X, CCB_Y, CCB_HDX, CCB_HDY, CCB_VDX, CCB_VDY, CCB_HDDX, CCB_HDDY };

void ccb_init(const SpriteBank *b);
const SpriteBank *ccb_bank(void);
/* Cel_AddToList: copy of CCB idx as stored in ART.CAR (X/Y 0, HDX 1.0 (12.20), VDY 1.0). */
Ccb ccb_get(int idx);
/* Cel_SetScale 0x4336c0 with the CCB's shift-table entry: on-screen size w x h (16.16). */
void ccb_set_scale(Ccb *c, int32_t w, int32_t h);
/* FUN_00433570: blit only the sub-rectangle (x, y, w, h) of the source (mode 6). */
void ccb_subrect(Ccb *c, int x, int y, int w, int h);
/* Cel_DrawList for one CCB. clip is in framebuffer pixels (inclusive, already doubled in hi-res). */
void ccb_draw(Framebuffer *fb, const ClipRect *clip, int scale, const Ccb *c);
/* GetNearestPaletteIndex(game palette, RGB555) + 10 (mode 10 fill colour). */
uint8_t ccb_rgb555_index(uint16_t c);
