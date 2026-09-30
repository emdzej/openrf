/* Player input word, as built by ReadPlayerInput 0x401340 (docs/architecture.md §5.5). */
#pragma once
#include <stdint.h>

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
