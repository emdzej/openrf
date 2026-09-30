/* App glue of the soldiers / gates / flag / end-of-game port (src/game/rules.h) for the play loop. */
#pragma once
#include "render/render.h"
#include "game/game.h"

/* Per level: EndGame hook (records the game clock), flag camera trackers on the players' views
   (v1 = NULL in the 1-player game). */
void play_rules_start(View *v0, View *v1);
/* collect(): the draw fields of classes 12 Flag, 13 Gate, 14 MAN. */
void play_rules_collect(Obj *o, RenderObj *r);
/* After game_frame: the Flag Discovery / Flag Pickup part of Mus_Director 0x41d730 (2 players also: both
   in the bunker -> 14 Bunker at 0x68, DAT_00480e9c). */
void play_rules_frame(void);
/* After the loop: EndGame fade, win sequence, high score. Returns false if the app should quit. */
bool play_rules_finish(const char *rfm_rel, bool quit);

/* Result of the last play_run (for the title-screen level progression). */
typedef struct { int winner; uint32_t time_ms; int level; int nplayers; } PlayOutcome;
extern PlayOutcome play_outcome;
