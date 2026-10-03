/* Platform contract: everything the portable game core needs from the host. The core (every file in
   src/ except the backends) includes no platform headers; a backend implements the functions below and
   drives the app (app.h). Backend: platform_gasm.c (gasm). The tests supply
   their own pieces (vfs_host.c, storage_file.c).

   A backend's job, in order:
   1. Mount the game data (vfs.h): a directory, a disc image, or a VfsSource over host-provided bytes.
   2. app_init(argc, argv) once (it calls plat_init). false: exit with an error (already reported
      through plat_error).
   3. Call app_frame() repeatedly until it returns false. Each call ends with exactly one present
      (plat_present or plat_present_rgb) unless the app is quitting; the backend paces the calls (a
      fixed-rate host such as gasm: its frame timer; a windowed one: vsync).
   4. app_exit() (it calls plat_shutdown).

   Threading: the core runs on one thread. Audio may be pulled from another thread (audio_render,
   audio.h) as long as it is called with plat_audio_lock held; the core takes the same lock around state
   the mixer reads. A single-threaded backend calls audio_render from its frame loop and makes the lock a
   no-op. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "keys.h"

typedef struct { uint8_t r, g, b; } RGB;

typedef struct {
    int w, h;              /* logical resolution (original: 320x240 or 640x480) */
    uint8_t *pixels;       /* w*h palette indices */
    RGB palette[256];
} Framebuffer;

/* ---- lifecycle ---- */
/* Open the output: an 8-bit framebuffer of w x h and 44100 Hz stereo audio (start pulling
   audio_render). */
bool plat_init(const char *title, int w, int h);
void plat_shutdown(void);
/* Report a fatal error to the user (message box, log line). May be called before plat_init. */
void plat_error(const char *title, const char *msg);

/* ---- video ---- */
Framebuffer *plat_fb(void);
/* Show the framebuffer through its palette (scaled/letterboxed by the backend to 4:3). */
void plat_present(void);
/* Show a true-colour image instead (0x00RRGGBB per pixel, e.g. a decoded movie frame), scaled to the
   same output rectangle. */
void plat_present_rgb(const uint32_t *xrgb, int w, int h);

/* ---- input ---- */
/* Gather input events. false when the user asked to quit (window closed). The core may call it more
   than once per presented frame (one call per step of the app state machine): state queried after it
   must be the current state, and edges are relative to the previous call. */
bool plat_poll(void);
/* Keyboard state, KEY_* codes (keys.h, = USB HID usages). Backends without a keyboard return false. */
bool plat_key_down(int key);
/* Edge since the previous plat_poll: any key, mouse button or pad button went down (skips stills and
   movies). */
bool plat_any_key_pressed(void);
/* Buttons held on virtual pad 0..3: PAD_* bits, same layout as the gasm ABI (SNES positions: A east,
   B south, X north, Y west). input.c maps them onto the game (docs/guide/controls.md). */
enum {
    PAD_A = 1 << 0, PAD_B = 1 << 1, PAD_X = 1 << 2, PAD_Y = 1 << 3, PAD_L = 1 << 4, PAD_R = 1 << 5,
    PAD_SELECT = 1 << 6, PAD_START = 1 << 7, PAD_UP = 1 << 8, PAD_DOWN = 1 << 9, PAD_LEFT = 1 << 10,
    PAD_RIGHT = 1 << 11,
};
uint32_t plat_pad(int player);

/* ---- time ---- */
/* Milliseconds since start. The game advances in whole 16 ms ticks of this clock (clock.h), so a
   reproducible backend returns a virtual clock stepped at each present (gasm: 16 ms per present). */
uint64_t plat_ticks_ms(void);

/* ---- audio ---- */
/* Excludes audio_render (audio.h) while the core changes mixer state. Recursive. */
void plat_audio_lock(void);
void plat_audio_unlock(void);

/* ---- storage (high scores) ---- */
/* Persistent key/value store (keys: [A-Za-z0-9._-], 1-128 bytes). get returns the value's length or -1
   if missing; copies it only if it fits in cap (cap 0 = query the length). */
int plat_storage_get(const char *key, void *dst, int cap);
bool plat_storage_set(const char *key, const void *data, int len);
/* Where a key lives, for log messages (a file path, "gasm:storage/<key>", ...). */
const char *plat_storage_location(const char *key);
