/* SDL3 platform backend (platform.h): window, 8-bit framebuffer presentation, keyboard and gamepads,
   one audio stream pulling the portable mixer (audio.c), the data-root search and main(). The only file
   that includes SDL. Storage: storage_file.c; data files: vfs_host.c. */
#include "platform.h"
#include "app.h"
#include "audio.h"
#include "vfs_host.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <libgen.h>
#include <mach-o/dyld.h>
#endif

/* keys.h values are SDL scancodes. */
_Static_assert(KEY_A == SDL_SCANCODE_A && KEY_1 == SDL_SCANCODE_1 && KEY_ESCAPE == SDL_SCANCODE_ESCAPE &&
               KEY_F2 == SDL_SCANCODE_F2 && KEY_RIGHTBRACKET == SDL_SCANCODE_RIGHTBRACKET &&
               KEY_INSERT == SDL_SCANCODE_INSERT && KEY_DELETE == SDL_SCANCODE_DELETE && KEY_UP == SDL_SCANCODE_UP &&
               KEY_KP_MINUS == SDL_SCANCODE_KP_MINUS && KEY_KP_ENTER == SDL_SCANCODE_KP_ENTER &&
               KEY_KP_9 == SDL_SCANCODE_KP_9 && KEY_KP_0 == SDL_SCANCODE_KP_0 && KEY_LCTRL == SDL_SCANCODE_LCTRL &&
               KEY_RALT == SDL_SCANCODE_RALT && KEY_RGUI == SDL_SCANCODE_RGUI && KEY_COUNT == SDL_SCANCODE_COUNT,
               "keys.h must match SDL scancodes");

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static Framebuffer fb;
static uint32_t *rgba;
static bool key_pressed_edge;
static SDL_AudioDeviceID audio_dev;
static SDL_AudioStream *audio_stream;

/* ------------------------------------------------------------------ audio */

/* The mixer is pulled here, on SDL's audio thread, with the stream locked (= plat_audio_lock). */
static void SDLCALL audio_cb(void *ud, SDL_AudioStream *st, int additional, int total)
{
    float buf[1024 * 2];
    (void)ud; (void)total;
    while (additional > 0) {
        int frames = additional / (int)(sizeof(float) * 2);
        if (frames <= 0) frames = 1;
        if (frames > 1024) frames = 1024;
        audio_render(buf, frames);
        SDL_PutAudioStreamData(st, buf, frames * (int)sizeof(float) * 2);
        additional -= frames * (int)sizeof(float) * 2;
    }
}

void plat_audio_lock(void) { if (audio_stream) SDL_LockAudioStream(audio_stream); }
void plat_audio_unlock(void) { if (audio_stream) SDL_UnlockAudioStream(audio_stream); }

/* ------------------------------------------------------------------ lifecycle */

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

    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, AUDIO_RATE };
    audio_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!audio_dev) fprintf(stderr, "audio: %s\n", SDL_GetError());
    /* OPENRF_MUTE=1: mix as usual but output silence (unattended test runs). */
    const char *mute = SDL_getenv("OPENRF_MUTE");
    if (audio_dev && mute && *mute && *mute != '0') SDL_SetAudioDeviceGain(audio_dev, 0.0f);
    if (audio_dev) {
        SDL_AudioSpec mix = { SDL_AUDIO_F32, AUDIO_CHANNELS, AUDIO_RATE };
        audio_stream = SDL_CreateAudioStream(&mix, NULL);
        if (audio_stream) {
            SDL_SetAudioStreamGetCallback(audio_stream, audio_cb, NULL);
            SDL_BindAudioStream(audio_dev, audio_stream);
        }
    }
    return true;
}

void plat_shutdown(void)
{
    if (audio_stream) { SDL_DestroyAudioStream(audio_stream); audio_stream = NULL; }
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    free(fb.pixels); free(rgba);
    SDL_Quit();
}

void plat_error(const char *title, const char *msg)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, NULL);
}

/* ------------------------------------------------------------------ video */

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

/* ------------------------------------------------------------------ input */

/* Gamepads in connection order = virtual pads 0..3. */
#define MAX_PADS 4
static SDL_Gamepad *pads[MAX_PADS];

static void pad_added(SDL_JoystickID id)
{
    for (int i = 0; i < MAX_PADS; i++)
        if (!pads[i]) {
            pads[i] = SDL_OpenGamepad(id);
            if (pads[i]) fprintf(stderr, "gamepad %d: %s\n", i + 1, SDL_GetGamepadName(pads[i]));
            return;
        }
}

static void pad_removed(SDL_JoystickID id)
{
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i] && SDL_GetGamepadID(pads[i]) == id) { SDL_CloseGamepad(pads[i]); pads[i] = NULL; }
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
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) key_pressed_edge = true;
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) pad_added(e.gdevice.which);
        if (e.type == SDL_EVENT_GAMEPAD_REMOVED) pad_removed(e.gdevice.which);
    }
    return true;
}

bool plat_key_down(int sc)
{
    const bool *ks = SDL_GetKeyboardState(NULL);
    return sc > 0 && sc < SDL_SCANCODE_COUNT && ks[sc];
}

bool plat_any_key_pressed(void) { return key_pressed_edge; }

/* SDL's positional face buttons onto the gasm/SNES layout (A east, B south, X north, Y west); the left
   stick doubles the d-pad. */
uint32_t plat_pad(int player)
{
    if (player < 0 || player >= MAX_PADS || !pads[player]) return 0;
    SDL_Gamepad *g = pads[player];
    static const struct { SDL_GamepadButton b; uint32_t bit; } map[] = {
        { SDL_GAMEPAD_BUTTON_EAST, PAD_A }, { SDL_GAMEPAD_BUTTON_SOUTH, PAD_B }, { SDL_GAMEPAD_BUTTON_NORTH, PAD_X },
        { SDL_GAMEPAD_BUTTON_WEST, PAD_Y }, { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PAD_L },
        { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_R }, { SDL_GAMEPAD_BUTTON_BACK, PAD_SELECT },
        { SDL_GAMEPAD_BUTTON_START, PAD_START }, { SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_UP },
        { SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_DOWN }, { SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_LEFT },
        { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_RIGHT },
    };
    uint32_t bits = 0;
    for (size_t i = 0; i < sizeof map / sizeof *map; i++) if (SDL_GetGamepadButton(g, map[i].b)) bits |= map[i].bit;
    const int dead = 16000;
    int x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    if (x < -dead) bits |= PAD_LEFT;
    if (x > dead) bits |= PAD_RIGHT;
    if (y < -dead) bits |= PAD_UP;
    if (y > dead) bits |= PAD_DOWN;
    if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > dead) bits |= PAD_L;
    if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > dead) bits |= PAD_R;
    return bits;
}

uint64_t plat_ticks_ms(void) { return fixed_step_on() ? virt_ms : SDL_GetTicks(); }

/* ------------------------------------------------------------------ main */

/* Data root: a path given on the command line (directory or disc image), else next to the executable
   (inside the .app: Contents/Resources/data, or data.bin / data.cue / data.iso there), then ./cd. */
static bool try_root(const char *path)
{
    return vfs_mount_path(path) && vfs_exists("ART/ART.CAR");
}

static bool try_root_or_image(const char *base)
{
    static const char *const ext[] = { "", ".cue", ".bin", ".iso" };
    char p[PATH_MAX + 8];
    for (size_t i = 0; i < sizeof ext / sizeof *ext; i++) {
        snprintf(p, sizeof p, "%s%s", base, ext[i]);
        if (try_root(p)) return true;
    }
    return false;
}

static bool find_data_root(int argc, char **argv)
{
    if (argc > 1 && argv[1][0] != '-') return try_root(argv[1]);
    const char *tries[] = { "../Resources/data", "../../../cd", "../../../../cd", "cd", "../cd" };
#ifdef __APPLE__
    char exe[PATH_MAX], cand[PATH_MAX];
    uint32_t n = sizeof exe;
    if (_NSGetExecutablePath(exe, &n) == 0) {
        char *dir = dirname(exe);
        for (size_t i = 0; i < sizeof tries / sizeof *tries; i++) {
            snprintf(cand, sizeof cand, "%s/%s", dir, tries[i]);
            if (try_root_or_image(cand)) return true;
        }
    }
#else
    for (size_t i = 0; i < sizeof tries / sizeof *tries; i++) if (try_root_or_image(tries[i])) return true;
#endif
    return try_root_or_image("cd");
}

int main(int argc, char **argv)
{
    if (!find_data_root(argc, argv)) {
        const char *msg = "Return Fire game data was not found.\n\n"
                          "Put the extracted CD (the folder containing RFIRE.BIN, ART, SOUND, TITLE and WORLDS) at "
                          "Return Fire.app/Contents/Resources/data, or a disc image of it at "
                          "Contents/Resources/data.cue (with its .bin), data.bin or data.iso, or pass the folder or "
                          "image as an argument.";
        fprintf(stderr, "%s\n", msg);
        plat_error("Return Fire", msg);
        return 1;
    }
    fprintf(stderr, "data: %s\n", vfs_describe());
    if (!app_init(argc, argv)) return 1;
    while (app_frame()) {}
    app_exit();
    return 0;
}
