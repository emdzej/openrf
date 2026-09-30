/* Player input word, as built by ReadPlayerInput 0x401340 (docs/architecture.md §5.5), from the keyboard
   and the virtual pads (plat_pad: pad 0 -> player 1, pad 1 -> player 2; docs/guide/controls.md). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum {
    IN_UP    = 0x40000000u, IN_DOWN  = 0x80000000u,
    IN_LEFT  = 0x10000000u, IN_RIGHT = 0x20000000u,
    IN_BTN1  = 0x08000000u, IN_BTN2  = 0x04000000u, IN_BTN3 = 0x02000000u,
    IN_BTN4  = 0x00200000u, IN_BTN5  = 0x00400000u,
    IN_ACTIONS = 0xfc000000u,
};

typedef struct { uint32_t cur[2], prev[2]; } Input;

/* PollAllPlayerInputs 0x42fd30: prev = cur; cur = read. */
void input_poll(Input *in, int nplayers);
static inline int input_pressed(const Input *in, int p, uint32_t bit) { return (in->cur[p] & bit) && !(in->prev[p] & bit); }
/* Player p's view is in the bunker select: the pad's START also means button 1 (launch). */
void input_set_menu(int p, bool menu);

/* Front-end actions, keyboard or pad (held state):
   UI_PLAY1 F2 / pad 1 START          start the current 1-player map (title screen)
   UI_PLAY2 F3 / pad 2 START / pad 1 SELECT   start the current 2-player map (title screen)
   UI_BACK  Esc / START + SELECT      leave the level (or the map viewer)
   UI_SWAP  Alt + 3 / SELECT          swap sides (2 players) */
enum { UI_PLAY1, UI_PLAY2, UI_BACK, UI_SWAP };
bool input_ui(int action);
