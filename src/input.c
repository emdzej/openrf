#include "input.h"
#include "platform.h"

typedef struct { int key; uint32_t bit; } Bind;

/* DefaultKeyBindings 0x402440: P1 WASD + H/J/K + Q/E, P2 numpad. */
static const Bind p1[] = {
    { KEY_W, IN_UP }, { KEY_S, IN_DOWN }, { KEY_A, IN_LEFT }, { KEY_D, IN_RIGHT },
    { KEY_H, IN_BTN1 }, { KEY_J, IN_BTN2 }, { KEY_K, IN_BTN3 },
    { KEY_Q, IN_BTN4 }, { KEY_E, IN_BTN5 },
    /* Convenience for a single player on a Mac keyboard (not in the original). */
    { KEY_UP, IN_UP }, { KEY_DOWN, IN_DOWN }, { KEY_LEFT, IN_LEFT }, { KEY_RIGHT, IN_RIGHT },
};
static const Bind p2[] = {
    { KEY_KP_8, IN_UP }, { KEY_KP_5, IN_DOWN }, { KEY_KP_4, IN_LEFT }, { KEY_KP_6, IN_RIGHT },
    { KEY_KP_MINUS, IN_BTN1 }, { KEY_KP_PLUS, IN_BTN2 }, { KEY_KP_ENTER, IN_BTN3 },
    { KEY_KP_7, IN_BTN4 }, { KEY_KP_9, IN_BTN5 },
};

/* Pad buttons -> input bits (not in the original, which read the keyboard and DirectInput joysticks
   through its key-binding table): d-pad = directions, A/B/X = buttons 1-3 (H/J/K), L/R = buttons 4/5
   (Q/E). START also launches while the player's view is in the bunker select (input_set_menu). */
static const struct { uint32_t pad, bit; } pad_binds[] = {
    { PAD_UP, IN_UP }, { PAD_DOWN, IN_DOWN }, { PAD_LEFT, IN_LEFT }, { PAD_RIGHT, IN_RIGHT },
    { PAD_A, IN_BTN1 }, { PAD_B, IN_BTN2 }, { PAD_X, IN_BTN3 }, { PAD_L, IN_BTN4 }, { PAD_R, IN_BTN5 },
};
static bool in_menu[2], start_armed[2];   /* START launches only once released in the menu (not the title's START) */

void input_set_menu(int p, bool menu)
{
    if (p < 0 || p > 1) return;
    in_menu[p] = menu;
    if (!menu) start_armed[p] = false;
}

static uint32_t read_binds(const Bind *b, int n, int pad)
{
    uint32_t w = 0;
    for (int i = 0; i < n; i++) if (plat_key_down(b[i].key)) w |= b[i].bit;
    uint32_t pb = plat_pad(pad);
    for (int i = 0; i < (int)(sizeof pad_binds / sizeof *pad_binds); i++) if (pb & pad_binds[i].pad) w |= pad_binds[i].bit;
    if (!(pb & PAD_START)) start_armed[pad] = true;
    else if (in_menu[pad] && start_armed[pad]) w |= IN_BTN1;
    /* Digital full deflection magnitudes: low byte for left/right, byte1 for up/down. */
    if (w & (IN_LEFT | IN_RIGHT)) w |= 0xff;
    if (w & (IN_UP | IN_DOWN)) w |= 0xff00;
    return w;
}

void input_poll(Input *in, int nplayers)
{
    for (int p = 0; p < 2; p++) in->prev[p] = in->cur[p];
    in->cur[0] = read_binds(p1, (int)(sizeof p1 / sizeof *p1), 0);
    if (plat_key_down(KEY_LSHIFT) && plat_key_down(KEY_LCTRL)) in->cur[0] |= 0x0e000000u;
    in->cur[1] = nplayers > 1 ? read_binds(p2, (int)(sizeof p2 / sizeof *p2), 1) : 0;
    if (nplayers > 1 && plat_key_down(KEY_INSERT) && plat_key_down(KEY_DELETE)) in->cur[1] |= 0x0e000000u;
}

static uint32_t any_pad(void) { return plat_pad(0) | plat_pad(1) | plat_pad(2) | plat_pad(3); }

bool input_ui(int action)
{
    uint32_t p0 = plat_pad(0), p1b = plat_pad(1), all = any_pad();
    bool back = false;
    for (int p = 0; p < 4; p++) back |= (plat_pad(p) & (PAD_START | PAD_SELECT)) == (PAD_START | PAD_SELECT);
    switch (action) {
    case UI_PLAY1: return plat_key_down(KEY_F2) || ((p0 & PAD_START) && !(p0 & PAD_SELECT));
    case UI_PLAY2: return plat_key_down(KEY_F3) || ((p1b & PAD_START) && !(p1b & PAD_SELECT)) ||
                          ((p0 & PAD_SELECT) && !(p0 & PAD_START));
    case UI_BACK:  return plat_key_down(KEY_ESCAPE) || back;
    case UI_SWAP:  return ((plat_key_down(KEY_LALT) || plat_key_down(KEY_RALT)) && plat_key_down(KEY_3)) ||
                          ((all & PAD_SELECT) && !back);
    }
    return false;
}
