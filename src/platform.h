/* Platform layer: window, 8-bit indexed framebuffer, audio, input. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint8_t r, g, b; } RGB;

typedef struct {
    int w, h;              /* logical resolution (original: 320x240 or 640x480) */
    uint8_t *pixels;       /* w*h palette indices */
    RGB palette[256];
} Framebuffer;

bool plat_init(const char *title, int w, int h);
void plat_shutdown(void);
Framebuffer *plat_fb(void);
void plat_present(void);
/* Present a true-colour image (e.g. decoded movie frame) scaled to the window. */
void plat_present_rgb(const uint32_t *xrgb, int w, int h);
/* Returns false when the user asked to quit. */
bool plat_poll(void);
bool plat_key_down(int sdl_scancode);
bool plat_any_key_pressed(void);   /* edge-triggered since last poll */
uint64_t plat_ticks_ms(void);
void plat_sleep_ms(uint32_t ms);

/* Audio: fire-and-forget effects plus one streamed music channel. */
int  plat_sound_load(const char *path);          /* returns id or -1 */
void plat_sound_play(int id);
/* Byte offsets are relative to the WAV data chunk; end 0 = end of data. */
bool plat_music_play(const char *path, uint32_t start, uint32_t end, uint32_t loop_start, bool loop);
/* Change range without restarting; pos UINT32_MAX keeps the current position. */
void plat_music_set_range(uint32_t pos, uint32_t end, uint32_t loop_start, bool loop);
bool plat_music_ended(void);
void plat_music_set_volume(float v);   /* 0..1 */
void plat_music_stop(void);
/* Sound-effect mixer output: a 44100 Hz S16 stereo stream (the DirectSound primary format) whose
   callback renders `frames` interleaved stereo frames; mixed by SDL alongside music/movie audio.
   lock/unlock exclude the callback (use around changes to state the callback reads). */
typedef void (*PlatMixFn)(int16_t *out, int frames);
bool plat_sfx_open(PlatMixFn fn);
void plat_sfx_lock(void);
void plat_sfx_unlock(void);
void plat_sfx_close(void);
/* Raw PCM stream (movies). Returns seconds of audio played so far. */
bool plat_pcm_open(int freq, int channels, int bits);
void plat_pcm_push(const void *data, int bytes);
double plat_pcm_played_seconds(void);
bool plat_pcm_drained(void);
void plat_pcm_close(void);
