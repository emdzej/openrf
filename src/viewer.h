#pragma once
#include "app.h"
#include "sprites.h"
/* Map viewer (--viewer): false if there are no maps. Step until STEP_DONE (Esc), then viewer_end. */
bool viewer_begin(const SpriteBank *sb);
Step viewer_step(void);
void viewer_end(void);
