#pragma once
#include "sprites.h"
/* Play a level (1 player). Returns false if the app should quit. */
bool play_run(const SpriteBank *sb, const char *rfm_rel);
