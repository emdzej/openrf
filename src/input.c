#include "input.h"
#include "platform.h"
#include <SDL3/SDL.h>

typedef struct { int sc; uint32_t bit; } Bind;

/* DefaultKeyBindings 0x402440: P1 WASD + H/J/K + Q/E, P2 numpad. */
static const Bind p1[] = {
    { SDL_SCANCODE_W, IN_UP }, { SDL_SCANCODE_S, IN_DOWN }, { SDL_SCANCODE_A, IN_LEFT }, { SDL_SCANCODE_D, IN_RIGHT },
    { SDL_SCANCODE_H, IN_BTN1 }, { SDL_SCANCODE_J, IN_BTN2 }, { SDL_SCANCODE_K, IN_BTN3 },
    { SDL_SCANCODE_Q, IN_BTN4 }, { SDL_SCANCODE_E, IN_BTN5 },
    /* Convenience for a single player on a Mac keyboard (not in the original). */
    { SDL_SCANCODE_UP, IN_UP }, { SDL_SCANCODE_DOWN, IN_DOWN }, { SDL_SCANCODE_LEFT, IN_LEFT }, { SDL_SCANCODE_RIGHT, IN_RIGHT },
};
static const Bind p2[] = {
    { SDL_SCANCODE_KP_8, IN_UP }, { SDL_SCANCODE_KP_5, IN_DOWN }, { SDL_SCANCODE_KP_4, IN_LEFT }, { SDL_SCANCODE_KP_6, IN_RIGHT },
    { SDL_SCANCODE_KP_MINUS, IN_BTN1 }, { SDL_SCANCODE_KP_PLUS, IN_BTN2 }, { SDL_SCANCODE_KP_ENTER, IN_BTN3 },
    { SDL_SCANCODE_KP_7, IN_BTN4 }, { SDL_SCANCODE_KP_9, IN_BTN5 },
};

static uint32_t read_binds(const Bind *b, int n)
{
    uint32_t w = 0;
    for (int i = 0; i < n; i++) if (plat_key_down(b[i].sc)) w |= b[i].bit;
    /* Digital full deflection magnitudes: low byte for left/right, byte1 for up/down. */
    if (w & (IN_LEFT | IN_RIGHT)) w |= 0xff;
    if (w & (IN_UP | IN_DOWN)) w |= 0xff00;
    return w;
}

void input_poll(Input *in, int nplayers)
{
    for (int p = 0; p < 2; p++) in->prev[p] = in->cur[p];
    in->cur[0] = read_binds(p1, (int)(sizeof p1 / sizeof *p1));
    if (plat_key_down(SDL_SCANCODE_LSHIFT) && plat_key_down(SDL_SCANCODE_LCTRL)) in->cur[0] |= 0x0e000000u;
    in->cur[1] = nplayers > 1 ? read_binds(p2, (int)(sizeof p2 / sizeof *p2)) : 0;
    if (nplayers > 1 && plat_key_down(SDL_SCANCODE_INSERT) && plat_key_down(SDL_SCANCODE_DELETE)) in->cur[1] |= 0x0e000000u;
}
