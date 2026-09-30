/* The application as a frame-driven state machine (the original's nested blocking loops -- intro stills
   and movies, title screen, game loop, end-of-game sequence, map viewer -- become states with per-frame
   step functions). The platform backend calls app_init once, app_frame until it returns false, then
   app_exit (platform.h describes the whole contract). */
#pragma once
#include <stdbool.h>

/* Command-line style options: --skip-intro, --play, --play2, --level <n|path>, --viewer (argv[0] and
   non-option arguments, e.g. the data path the backend already mounted, are ignored). The game data
   must be mounted (vfs.h). false: fatal error, already reported with plat_error. */
bool app_init(int argc, char **argv);
/* One presented frame: poll input, advance, render, present. false = quit. */
bool app_frame(void);
void app_exit(void);

/* Result of a state's step function. Each step starts after a plat_poll (the app polls before every
   step, like each iteration of the original loops did).
   STEP_FRAME: the step presented a frame; call it again next frame.
   STEP_AGAIN: nothing presented (the state changed); poll and step again within the same frame.
   STEP_DONE:  finished without presenting; the owner moves on (then returns STEP_AGAIN itself).
   A frame is therefore poll, step, [poll, step ...] until one step presents. */
typedef enum { STEP_FRAME, STEP_AGAIN, STEP_DONE } Step;
