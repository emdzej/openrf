/* Portable audio mixer: one 44100 Hz stereo output made of
   - the streamed music channel (byte ranges of SCORE.WAV / DRUMS.WAV read through the file layer, with
     the range / loop / seamless-continue / volume semantics the music director needs, music.c),
   - the movie PCM channel (.STM soundtracks, 22050 Hz, resampled),
   - the sound-effect mixer (sfx_render, already 44100 Hz S16 stereo like the original's DirectSound
     primary buffer).
   The backend pulls it: from an audio thread under plat_audio_lock, or once per frame for
   audio_frames_for_frame() frames (fixed-rate hosts such as gasm). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { AUDIO_RATE = 44100, AUDIO_CHANNELS = 2 };

/* Render frames interleaved stereo frames, samples in [-1, 1]. Call with plat_audio_lock held. */
void audio_render(float *out, int frames);
/* Frames to render for one app frame at rate_num / rate_den Hz (e.g. 60/1, 125/2 for 62.5 Hz): the
   fraction is carried over, so the long-run total is exact. */
int audio_frames_for_frame(uint32_t rate_num, uint32_t rate_den);

/* The sound-effect source (sfx_render); NULL = none. */
void audio_set_sfx(void (*mix)(int16_t *out, int frames));
void audio_lock(void);
void audio_unlock(void);

/* Music: a PCM WAV streamed from the data. Byte offsets are relative to the WAV data chunk; end 0 = end
   of data. */
bool audio_music_play(const char *rel, uint32_t start, uint32_t end, uint32_t loop_start, bool loop);
/* Change range without restarting; pos UINT32_MAX keeps the current position (seamless). */
void audio_music_set_range(uint32_t pos, uint32_t end, uint32_t loop_start, bool loop);
bool audio_music_ended(void);          /* also true when nothing is playing */
void audio_music_set_volume(float v);  /* 0..1 */
void audio_music_stop(void);

/* Movie PCM: the whole soundtrack is pushed up front, video is slaved to how much has been played. */
bool audio_pcm_open(int freq, int channels, int bits);
void audio_pcm_push(const void *data, int bytes);
double audio_pcm_played_seconds(void);
bool audio_pcm_drained(void);
void audio_pcm_close(void);
