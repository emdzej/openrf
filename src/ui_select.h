/* Bunker vehicle-select overlay (ViewModeBunkerSelect 0x404570 / FUN_00404800 / FUN_00403fa0). */
#pragma once
#include "platform.h"
#include <stdint.h>

struct BunkerView;
/* Draw into the view rectangle (0,0,vw,vh) in logical 320x240 units; tick = DAT_0045f390. */
void ui_select_draw(Framebuffer *fb, int scale, int vw, int vh, const struct BunkerView *bv, uint32_t tick);
/* Same, into the view rectangle (vx, vy, vw, vh) (2 players: the view bitmap's clip origin). */
void ui_select_draw_at(Framebuffer *fb, int scale, int vx, int vy, int vw, int vh, const struct BunkerView *bv, uint32_t tick);

/* Draw-side state of FUN_00404fb0 (view +0xd4 zoom, +0xd0 spin, +0xcc icon fade), reset by
   FUN_00404ee0 when the fly-back starts. */
typedef struct { int32_t scale, angle, icons; } UiSkull;
static inline void ui_skull_reset(UiSkull *a) { a->scale = 0x28f; a->angle = 0; a->icons = 0; }
/* Fly-back (0x404f70 / 0x4052d0: over the world view; 0x405360 / 0x405440: over the fade cel): the other
   side's laughing skull and the lost vehicle type's stock. dt = frame ticks. */
void ui_flyback_draw(Framebuffer *fb, int scale, int vx, int vy, int vw, int vh, const struct BunkerView *bv,
                     UiSkull *a, int32_t dt);
/* 2 players, BV_SPECTATE (FUN_004054f0 / FUN_004055f0): skull and all four vehicle types crossed out. */
void ui_spectate_draw(Framebuffer *fb, int scale, int vx, int vy, int vw, int vh, const struct BunkerView *bv,
                      UiSkull *a, int32_t dt);
