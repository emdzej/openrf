/* Game timing, after GetTicks16ms 0x4279f0 / FrameTiming 0x433220:
   variable timestep in whole 16 ms ticks, capped at 12 per frame (-f12). */
#pragma once
#include "platform.h"

typedef struct { uint64_t last; int dt; uint64_t game_ticks; } Clock;

static inline uint64_t clock_now16(void) { return plat_ticks_ms() >> 4; }
static inline void clock_start(Clock *c) { c->last = clock_now16(); c->dt = 0; c->game_ticks = 0; }
static inline int clock_frame(Clock *c)
{
    uint64_t now = clock_now16();
    uint64_t d = now - c->last;
    c->last = now;
    c->dt = d > 12 ? 12 : (int)d;
    c->game_ticks += (uint64_t)c->dt;
    return c->dt;
}
