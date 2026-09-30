/* End of a game: EndGame 0x40f380 (clock stop, 1 s fade out), State_EndOfGameSequence 0x40f050 (win music,
   banner TITLE/Ban{B,G}{L,H}.bmp, WIN*.STM by level with the banner over it), RecordHighScore 0x42d8b0
   (RFire_HS, XOR obfuscated) as done by State_BackScreen. */
#pragma once
#include "platform.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int winner;             /* DAT_00457100: 0/1 = side, other = no winner (lost / quit) */
    int level;              /* DAT_00443868: 0-based level (win movie table 0x4559e0) */
    int nplayers;
    uint32_t time_ms;       /* DAT_0044c4e0: game clock at EndGame */
    const char *map_rel;    /* the map file (the high-score record keeps its base name) */
} GameResult;

/* SetFadeTarget(0, ms) on the current frame (palette fade of the 8-bit framebuffer). */
bool endgame_fade_out(uint32_t ms);
/* State_EndOfGameSequence; false if the user closed the window. */
bool endgame_sequence(const GameResult *r);
/* RecordHighScore: 1-player wins keep the best time per (level, map); 2-player games count wins / draws per
   pair of names. Returns true if the table changed. */
bool highscore_record(const GameResult *r);
/* ~/Library/Application Support/Return Fire/RFire_HS (OPENRF_HS overrides; tests). */
const char *highscore_path(void);
/* Decoded 1-player records (for the log / tests): returns the count, fills up to max. */
typedef struct { int level; char map[33], player[33]; uint32_t time_ms; } HighScore1P;
int highscore_list(HighScore1P *out, int max);
/* 2-player records (names sorted; OPENRF_P1 / OPENRF_P2 name the players, default $USER / "Player 2"). */
typedef struct { char name[2][33]; uint32_t wins[2], draws; } HighScore2P;
int highscore_list2(HighScore2P *out, int max);
