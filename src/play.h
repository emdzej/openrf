/* A level: State_Game1P 0x41a660 / State_Game2P 0x40ec50 (1 or 2 players by the map header), then the
   end-of-game sequence. */
#pragma once
#include "app.h"
#include "sprites.h"

typedef struct Play Play;
/* Loads the level; NULL if it can't be loaded (the caller goes back to the title screen). */
Play *play_begin(const SpriteBank *sb, const char *rfm_rel);
/* One game frame; STEP_DONE when the level and its end sequence are over (result: play_outcome,
   play_rules.h). rfm_rel must stay valid until then. */
Step play_step(Play *p);
/* Frees the level (also mid-level or mid-sequence, when the app quits). */
void play_end(Play *p);
