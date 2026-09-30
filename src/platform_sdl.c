#include "platform.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static Framebuffer fb;
static uint32_t *rgba;
static bool key_pressed_edge;

static SDL_AudioDeviceID audio_dev;
#define MAX_SOUNDS 128
#define MAX_VOICES 16
typedef struct { SDL_AudioSpec spec; uint8_t *buf; uint32_t len; } Sound;
static Sound sounds[MAX_SOUNDS];
static int nsounds;
static SDL_AudioStream *voices[MAX_VOICES];
bool plat_init(const char *title, int w, int h)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    int scale = (w <= 320) ? 3 : 2;
    if (!SDL_CreateWindowAndRenderer(title, w * scale, h * scale, SDL_WINDOW_RESIZABLE, &win, &ren)) {
        fprintf(stderr, "window: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(ren, 1);
    /* Keep the original 4:3 pixel grid with integer scaling where possible. */
    SDL_SetRenderLogicalPresentation(ren, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    fb.w = w; fb.h = h;
    fb.pixels = calloc((size_t)w * h, 1);
    rgba = calloc((size_t)w * h, 4);

    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, 44100 };
    audio_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!audio_dev) fprintf(stderr, "audio: %s\n", SDL_GetError());
    /* OPENRF_MUTE=1: mix as usual but output silence (unattended test runs). */
    const char *mute = SDL_getenv("OPENRF_MUTE");
    if (audio_dev && mute && *mute && *mute != '0') SDL_SetAudioDeviceGain(audio_dev, 0.0f);
    return true;
}

void plat_shutdown(void)
{
    plat_music_stop();
    plat_sfx_close();
    for (int i = 0; i < MAX_VOICES; i++) if (voices[i]) SDL_DestroyAudioStream(voices[i]);
    for (int i = 0; i < nsounds; i++) SDL_free(sounds[i].buf);
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    free(fb.pixels); free(rgba);
    SDL_Quit();
}

Framebuffer *plat_fb(void) { return &fb; }

/* Debug: OPENRF_FIXED_STEP=1 replaces the wall clock with a virtual one that advances 16 ms per presented
   frame, so runs (and OPENRF_SHOT captures) are reproducible. */
static int fixed_step = -1;
static uint64_t virt_ms;
static bool fixed_step_on(void)
{
    if (fixed_step < 0) { const char *e = SDL_getenv("OPENRF_FIXED_STEP"); fixed_step = e && *e && *e != '0'; }
    return fixed_step;
}
static void advance_virtual_clock(void) { if (fixed_step_on()) virt_ms += 16; }

/* Debug: OPENRF_SHOT=<file.bmp> writes the frame shown after OPENRF_SHOT_MS ms, then quits. */
static void maybe_screenshot_px(const uint32_t *px, int w, int h)
{
    static int done;
    const char *path = SDL_getenv("OPENRF_SHOT");
    if (!path || done) return;
    const char *ms = SDL_getenv("OPENRF_SHOT_MS");
    if (plat_ticks_ms() < (uint64_t)(ms ? SDL_atoi(ms) : 1500)) return;
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888, (void *)px, w * 4);
    SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    done = 1;
    SDL_Event q = { .type = SDL_EVENT_QUIT };
    SDL_PushEvent(&q);
}

void plat_present(void)
{
    uint32_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = 0xFF000000u | (fb.palette[i].r << 16) | (fb.palette[i].g << 8) | fb.palette[i].b;
    size_t n = (size_t)fb.w * fb.h;
    for (size_t i = 0; i < n; i++) rgba[i] = lut[fb.pixels[i]];
    maybe_screenshot_px(rgba, fb.w, fb.h);
    advance_virtual_clock();
    SDL_UpdateTexture(tex, NULL, rgba, fb.w * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

void plat_present_rgb(const uint32_t *px, int w, int h)
{
    static SDL_Texture *mt;
    static int mw, mh;
    if (!mt || mw != w || mh != h) {
        if (mt) SDL_DestroyTexture(mt);
        mt = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureScaleMode(mt, SDL_SCALEMODE_NEAREST);
        mw = w; mh = h;
    }
    maybe_screenshot_px(px, w, h);
    advance_virtual_clock();
    SDL_UpdateTexture(mt, NULL, px, w * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, mt, NULL, NULL);   /* 320x240 -> logical 640x480, pixel-doubled */
    SDL_RenderPresent(ren);
}

static SDL_AudioStream *pcm;
static uint64_t pcm_pushed;
static int pcm_bytes_per_sec;

bool plat_pcm_open(int freq, int channels, int bits)
{
    plat_pcm_close();
    SDL_AudioSpec spec = { bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE, channels, freq };
    pcm = SDL_CreateAudioStream(&spec, NULL);
    if (!pcm) return false;
    pcm_pushed = 0;
    pcm_bytes_per_sec = freq * channels * (bits / 8);
    SDL_BindAudioStream(audio_dev, pcm);
    return true;
}

void plat_pcm_push(const void *data, int bytes)
{
    if (!pcm) return;
    SDL_PutAudioStreamData(pcm, data, bytes);
    pcm_pushed += (uint64_t)bytes;
}

double plat_pcm_played_seconds(void)
{
    if (!pcm) return 0;
    int queued = SDL_GetAudioStreamQueued(pcm);
    return (double)(pcm_pushed - (uint64_t)(queued > 0 ? queued : 0)) / pcm_bytes_per_sec;
}

bool plat_pcm_drained(void) { return !pcm || SDL_GetAudioStreamQueued(pcm) == 0; }

void plat_pcm_close(void)
{
    if (pcm) { SDL_DestroyAudioStream(pcm); pcm = NULL; }
}

bool plat_poll(void)
{
    SDL_Event e;
    key_pressed_edge = false;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) return false;
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            if (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT))
                SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
            else if (e.key.scancode == SDL_SCANCODE_M && audio_dev)   /* M: mute/unmute the game */
                SDL_SetAudioDeviceGain(audio_dev, SDL_GetAudioDeviceGain(audio_dev) > 0.0f ? 0.0f : 1.0f);
            else
                key_pressed_edge = true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) key_pressed_edge = true;
    }
    return true;
}

bool plat_key_down(int sc)
{
    const bool *ks = SDL_GetKeyboardState(NULL);
    return ks[sc];
}

bool plat_any_key_pressed(void) { return key_pressed_edge; }
uint64_t plat_ticks_ms(void) { return fixed_step_on() ? virt_ms : SDL_GetTicks(); }
void plat_sleep_ms(uint32_t ms) { SDL_Delay(ms); }

int plat_sound_load(const char *path)
{
    if (nsounds >= MAX_SOUNDS) return -1;
    Sound *s = &sounds[nsounds];
    if (!SDL_LoadWAV(path, &s->spec, &s->buf, &s->len)) {
        fprintf(stderr, "wav %s: %s\n", path, SDL_GetError());
        return -1;
    }
    return nsounds++;
}

void plat_sound_play(int id)
{
    if (!audio_dev || id < 0 || id >= nsounds) return;
    Sound *s = &sounds[id];
    /* Reuse an idle voice with a matching source format, else replace one. */
    int slot = -1;
    for (int i = 0; i < MAX_VOICES && slot < 0; i++)
        if (!voices[i] || SDL_GetAudioStreamQueued(voices[i]) == 0) slot = i;
    if (slot < 0) slot = 0;
    if (voices[slot]) SDL_DestroyAudioStream(voices[slot]);
    voices[slot] = SDL_CreateAudioStream(&s->spec, NULL);
    SDL_BindAudioStream(audio_dev, voices[slot]);
    SDL_PutAudioStreamData(voices[slot], s->buf, (int)s->len);
    SDL_FlushAudioStream(voices[slot]);
}

/* Music: stream a byte range of a WAV's data chunk on demand (SCORE.WAV is 223 MB).
   Ranges can be changed while playing to continue seamlessly (MusTrans1_ContinueSeamless). */
typedef struct {
    FILE *f;
    uint32_t data_off, data_len;
    uint32_t pos, end, loop_start;
    bool loop, ended;
} MusicSrc;
static MusicSrc msrc;
static SDL_AudioStream *music;

static void SDLCALL music_cb(void *ud, SDL_AudioStream *st, int additional, int total)
{
    uint8_t buf[16384];
    while (additional > 0 && msrc.f && !msrc.ended) {
        if (msrc.pos >= msrc.end) {
            if (!msrc.loop) { msrc.ended = true; return; }
            msrc.pos = msrc.loop_start;
        }
        uint32_t n = msrc.end - msrc.pos;
        if (n > sizeof buf) n = sizeof buf;
        if ((int)n > additional) n = (uint32_t)((additional + 3) & ~3);
        if (n > msrc.end - msrc.pos) n = msrc.end - msrc.pos;
        fseek(msrc.f, (long)(msrc.data_off + msrc.pos), SEEK_SET);
        size_t got = fread(buf, 1, n, msrc.f);
        if (got == 0) { msrc.ended = true; return; }
        SDL_PutAudioStreamData(st, buf, (int)got);
        msrc.pos += (uint32_t)got;
        additional -= (int)got;
    }
}

bool plat_music_play(const char *path, uint32_t start, uint32_t end, uint32_t loop_start, bool loop)
{
    plat_music_stop();
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) { fclose(f); return false; }
    SDL_AudioSpec spec = {0};
    uint32_t data_off = 0, data_len = 0;
    for (;;) {
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8) break;
        uint32_t sz = ch[4] | ch[5] << 8 | ch[6] << 16 | (uint32_t)ch[7] << 24;
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fmt[16];
            if (fread(fmt, 1, 16, f) != 16) break;
            spec.channels = fmt[2] | fmt[3] << 8;
            spec.freq = fmt[4] | fmt[5] << 8 | fmt[6] << 16 | fmt[7] << 24;
            spec.format = (fmt[14] | fmt[15] << 8) == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
            fseek(f, (long)(sz - 16 + (sz & 1)), SEEK_CUR);
        } else if (!memcmp(ch, "data", 4)) {
            data_off = (uint32_t)ftell(f);
            data_len = sz;
            break;
        } else {
            fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
        }
    }
    if (!data_off || !spec.freq) { fclose(f); return false; }
    msrc = (MusicSrc){ f, data_off, data_len, 0, 0, 0, false, false };
    music = SDL_CreateAudioStream(&spec, NULL);
    SDL_SetAudioStreamGetCallback(music, music_cb, NULL);
    plat_music_set_range(start, end, loop_start, loop);
    SDL_BindAudioStream(audio_dev, music);
    return true;
}

void plat_music_set_range(uint32_t pos, uint32_t end, uint32_t loop_start, bool loop)
{
    if (!music) return;
    SDL_LockAudioStream(music);
    if (end == 0 || end > msrc.data_len) end = msrc.data_len;
    if (pos != UINT32_MAX) msrc.pos = pos > end ? end : pos & ~3u;
    msrc.end = end;
    msrc.loop_start = loop_start & ~3u;
    msrc.loop = loop;
    msrc.ended = false;
    SDL_UnlockAudioStream(music);
}

bool plat_music_ended(void)
{
    if (!music) return true;
    return msrc.ended && SDL_GetAudioStreamQueued(music) == 0;
}

void plat_music_set_volume(float v) { if (music) SDL_SetAudioStreamGain(music, v); }

void plat_music_stop(void)
{
    if (music) { SDL_DestroyAudioStream(music); music = NULL; }
    if (msrc.f) { fclose(msrc.f); msrc.f = NULL; }
}


/* Sound-effect mixer stream (sfx.c renders into it from the audio thread). */
static SDL_AudioStream *sfx_stream;
static PlatMixFn sfx_fn;

static void SDLCALL sfx_cb(void *ud, SDL_AudioStream *st, int additional, int total)
{
    int16_t buf[2048 * 2];
    (void)ud; (void)total;
    while (additional > 0) {
        int frames = additional / 4;
        if (frames <= 0) frames = 1;
        if (frames > 2048) frames = 2048;
        sfx_fn(buf, frames);
        SDL_PutAudioStreamData(st, buf, frames * 4);
        additional -= frames * 4;
    }
}

bool plat_sfx_open(PlatMixFn fn)
{
    if (!audio_dev || sfx_stream) return sfx_stream != NULL;
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, 44100 };
    sfx_stream = SDL_CreateAudioStream(&spec, NULL);
    if (!sfx_stream) return false;
    sfx_fn = fn;
    SDL_SetAudioStreamGetCallback(sfx_stream, sfx_cb, NULL);
    SDL_BindAudioStream(audio_dev, sfx_stream);
    return true;
}

void plat_sfx_lock(void) { if (sfx_stream) SDL_LockAudioStream(sfx_stream); }
void plat_sfx_unlock(void) { if (sfx_stream) SDL_UnlockAudioStream(sfx_stream); }
void plat_sfx_close(void)
{
    if (sfx_stream) { SDL_DestroyAudioStream(sfx_stream); sfx_stream = NULL; }
}
