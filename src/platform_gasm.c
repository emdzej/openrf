/* gasm platform backend (platform.h) for the wasm32 guest openrf.wasm (gasm ABI v0, spec/ABI.md in the
   gasm repository). The only file that includes gasm.h. The runner calls the exports:
     gasm_init   mount the data, build the option list from launch params, app_init
     gasm_frame  one app_frame (= one presented frame at 62.5 Hz, one original 16 ms tick), then this
                 frame's share of the mixer output
     gasm_exit   app_exit (high scores are written through gasm:storage as they happen)

   Everything is deterministic: the clock is 16 ms per presented frame (like the SDL build's
   OPENRF_FIXED_STEP=1), audio is rendered on the frame (never on another thread), input is the runner's
   per-frame pad state. Same module + assets + params + pad input = same video and audio on every runner.

   Game data (vfs.h), first match wins:
   1. asset "cd" (or "rom", gasm-run --rom): a disc image, raw MODE1/2352 .bin or .iso, mounted with
      vfs_mount_image over asset_read_at (streamed, nothing is preloaded). A .cue sheet can't be followed
      (assets are a flat name map): pass the .bin it names instead.
   2. the disc's files as separate assets named by their disc paths (gasm-run --asset-dir <cd folder>, or
      --asset ART/ART.CAR=... per file). Names are tried as the game spells them and upper-cased (the
      disc's case); runners that fold case themselves match either way.

   Launch params (gasm-run --param k=v, URL query in the browser) replace the SDL build's arguments and
   environment: level, play, play2, skip_intro, viewer -> --level/--play/--play2/--skip-intro/--viewer;
   demo -> OPENRF_DEMO, p1/p2 -> the player names (USER/OPENRF_P1, OPENRF_P2), cam_h -> OPENRF_CAM_H,
   sfx_log -> OPENRF_SFX_LOG. They are put into wasi-libc's environment, where the core reads them. */
#include "platform.h"
#include "app.h"
#include "audio.h"
#include "vfs.h"
#include <gasm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wasi/api.h>

_Static_assert(PAD_A == GASM_BTN_A && PAD_B == GASM_BTN_B && PAD_X == GASM_BTN_X && PAD_Y == GASM_BTN_Y &&
               PAD_L == GASM_BTN_L && PAD_R == GASM_BTN_R && PAD_SELECT == GASM_BTN_SELECT &&
               PAD_START == GASM_BTN_START && PAD_UP == GASM_BTN_UP && PAD_DOWN == GASM_BTN_DOWN &&
               PAD_LEFT == GASM_BTN_LEFT && PAD_RIGHT == GASM_BTN_RIGHT, "PAD_* must be the gasm button bits");

/* 62.5 Hz: one original 16 ms tick per frame. */
enum { RATE_NUM = 125, RATE_DEN = 2, MAX_AUDIO_FRAMES = 1024 };

static Framebuffer fb;
static uint32_t *rgba, *rgba_movie;
static int movie_w, movie_h;
static uint64_t virt_ms;                 /* 16 ms per present */
static uint32_t pads[4], pads_prev;      /* pads_prev: all pads OR-ed at the previous plat_poll */
static bool any_edge;

static void log_str(const char *s) { gasm_log(s, (uint32_t)strlen(s)); }

/* ------------------------------------------------------------------ lifecycle */

bool plat_init(const char *title, int w, int h)
{
    (void)title;
    fb.w = w;
    fb.h = h;
    fb.pixels = calloc((size_t)w * h, 1);
    rgba = calloc((size_t)w * h, 4);
    if (!fb.pixels || !rgba) { plat_error("Return Fire", "out of memory"); return false; }
    gasm_audio_config(AUDIO_RATE, AUDIO_CHANNELS);
    return true;
}

void plat_shutdown(void)
{
    free(fb.pixels); free(rgba); free(rgba_movie);
    fb.pixels = NULL; rgba = rgba_movie = NULL;
    movie_w = movie_h = 0;
}

void plat_error(const char *title, const char *msg)
{
    char line[1024];
    snprintf(line, sizeof line, "%s: %s", title, msg);
    log_str(line);
}

/* ------------------------------------------------------------------ video */

Framebuffer *plat_fb(void) { return &fb; }

void plat_present(void)
{
    uint32_t lut[256];                   /* RGBA bytes = little-endian 0xAABBGGRR */
    for (int i = 0; i < 256; i++)
        lut[i] = 0xff000000u | (uint32_t)fb.palette[i].b << 16 | (uint32_t)fb.palette[i].g << 8 | fb.palette[i].r;
    size_t n = (size_t)fb.w * fb.h;
    for (size_t i = 0; i < n; i++) rgba[i] = lut[fb.pixels[i]];
    gasm_video_present(rgba, (uint32_t)fb.w, (uint32_t)fb.h, (uint32_t)fb.w * 4);
    virt_ms += 16;
}

/* Movie frames (0x00RRGGBB) at their own size (320x240); the runner scales them into the same 4:3 output. */
void plat_present_rgb(const uint32_t *px, int w, int h)
{
    if (w != movie_w || h != movie_h) {
        free(rgba_movie);
        rgba_movie = malloc((size_t)w * h * 4);
        movie_w = rgba_movie ? w : 0;
        movie_h = rgba_movie ? h : 0;
    }
    if (rgba_movie) {
        size_t n = (size_t)w * h;
        for (size_t i = 0; i < n; i++) {
            uint32_t c = px[i];
            rgba_movie[i] = 0xff000000u | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
        }
        gasm_video_present(rgba_movie, (uint32_t)w, (uint32_t)h, (uint32_t)w * 4);
    }
    virt_ms += 16;
}

/* ------------------------------------------------------------------ input and time */

/* Pads are stable within a gasm_frame; the core may poll several times per frame, so the "any button went
   down" edge fires at the first poll that sees the new state. The runner handles quitting (gasm_exit). */
bool plat_poll(void)
{
    uint32_t all = 0;
    for (uint32_t p = 0; p < 4; p++) all |= pads[p] = gasm_input_pad(p);
    any_edge = (all & ~pads_prev) != 0;
    pads_prev = all;
    return true;
}

bool plat_key_down(int key) { (void)key; return false; }
bool plat_any_key_pressed(void) { return any_edge; }
uint32_t plat_pad(int player) { return player >= 0 && player < 4 ? pads[player] : 0; }
uint64_t plat_ticks_ms(void) { return virt_ms; }

/* Single-threaded: the mixer runs inside gasm_frame. */
void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}

/* ------------------------------------------------------------------ storage */

int plat_storage_get(const char *key, void *dst, int cap)
{
    return gasm_storage_get(key, (uint32_t)strlen(key), dst, dst ? (uint32_t)(cap > 0 ? cap : 0) : 0);
}

bool plat_storage_set(const char *key, const void *data, int len)
{
    return gasm_storage_set(key, (uint32_t)strlen(key), data, (uint32_t)len) == 0;
}

const char *plat_storage_location(const char *key)
{
    static char loc[160];
    snprintf(loc, sizeof loc, "gasm:storage/%s", key);
    return loc;
}

/* ------------------------------------------------------------------ data: a disc image asset */

typedef struct { char name[16]; } ImageAsset;

static int64_t image_read_at(void *ctx, uint64_t off, void *dst, size_t len)
{
    const ImageAsset *a = ctx;
    size_t done = 0;
    while (done < len) {                                   /* the ABI allows short reads */
        uint64_t o = off + done;
        if (o > UINT32_MAX) break;                         /* asset offsets are 32-bit */
        int32_t k = gasm_asset_read_at(a->name, (uint32_t)strlen(a->name), (uint32_t)o, (uint8_t *)dst + done,
                                       (uint32_t)(len - done));
        if (k < 0) return done ? (int64_t)done : -1;
        if (k == 0) break;
        done += (size_t)k;
    }
    return (int64_t)done;
}

static void image_close(void *ctx) { free(ctx); }

static bool looks_like_cue(const char *name)
{
    char head[256] = { 0 };
    int32_t n = gasm_asset_read_at(name, (uint32_t)strlen(name), 0, head, sizeof head - 1);
    if (n <= 0) return false;
    for (int i = 0; i < n; i++) if ((unsigned char)head[i] < 9) return false;   /* binary */
    return strstr(head, "FILE") || strstr(head, "TRACK");
}

static bool mount_image_asset(const char *name)
{
    int32_t size = gasm_asset_size(name, (uint32_t)strlen(name));
    if (size < 0) return false;
    char msg[256];
    if (looks_like_cue(name)) {
        snprintf(msg, sizeof msg, "asset \"%s\" is a .cue sheet; pass the .bin it names instead (--asset %s=<image>.bin)",
                 name, name);
        log_str(msg);
        return false;
    }
    ImageAsset *a = calloc(1, sizeof *a);
    if (!a) return false;
    snprintf(a->name, sizeof a->name, "%s", name);
    VfsSource src = { a, (uint64_t)(uint32_t)size, image_read_at, image_close };
    char desc[64];
    snprintf(desc, sizeof desc, "asset \"%s\"", name);
    if (vfs_mount_image(&src, 0, desc)) return true;
    snprintf(msg, sizeof msg, "asset \"%s\" is not an ISO 9660 disc image (.iso or raw MODE1/2352 .bin)", name);
    log_str(msg);
    return false;
}

/* ------------------------------------------------------------------ data: the disc's files as assets */

typedef struct { int32_t size; char name[]; } FileAsset;

/* Disc path -> asset name: '\' -> '/', no leading "./" or "/". */
static void norm_path(const char *rel, char *out, size_t cap, bool upper)
{
    while (*rel == '/' || *rel == '\\' || (rel[0] == '.' && (rel[1] == '/' || rel[1] == '\\'))) rel += *rel == '.' ? 2 : 1;
    size_t i = 0;
    for (; rel[i] && i + 1 < cap; i++) {
        char c = rel[i] == '\\' ? '/' : rel[i];
        out[i] = upper && c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
    }
    out[i] = 0;
}

static void *files_open(void *ctx, const char *rel, uint64_t *size)
{
    (void)ctx;
    char name[512];
    for (int pass = 0; pass < 2; pass++) {                 /* as spelled, then the disc's upper case */
        norm_path(rel, name, sizeof name, pass == 1);
        int32_t n = gasm_asset_size(name, (uint32_t)strlen(name));
        if (n < 0) continue;
        size_t l = strlen(name);
        FileAsset *f = malloc(sizeof *f + l + 1);
        if (!f) return NULL;
        f->size = n;
        memcpy(f->name, name, l + 1);
        *size = (uint64_t)n;
        return f;
    }
    return NULL;
}

static int64_t files_read_at(void *ctx, void *file, uint64_t off, void *dst, size_t len)
{
    (void)ctx;
    const FileAsset *f = file;
    size_t done = 0;
    while (done < len && off + done <= UINT32_MAX) {
        int32_t k = gasm_asset_read_at(f->name, (uint32_t)strlen(f->name), (uint32_t)(off + done),
                                       (uint8_t *)dst + done, (uint32_t)(len - done));
        if (k < 0) return done ? (int64_t)done : -1;
        if (k == 0) break;
        done += (size_t)k;
    }
    return (int64_t)done;
}

static void files_close(void *ctx, void *file) { (void)ctx; free(file); }

/* The ABI can't enumerate assets, and the only directory the game lists is a map folder (the viewer:
   WORLDS/<n>PLAYER/LEVELk), so listing probes the map names the disc uses there (RFMAP001-204.RFM). */
static bool files_list(void *ctx, const char *dir, VfsListFn fn, void *user)
{
    (void)ctx;
    char d[256], p[300];
    norm_path(dir, d, sizeof d, true);
    if (strncmp(d, "WORLDS/", 7)) return false;
    bool any = false;
    for (int n = 1; n <= 204; n++) {
        char base[16];
        snprintf(base, sizeof base, "RFMAP%03d.RFM", n);
        snprintf(p, sizeof p, "%s/%s", dir, base);
        uint64_t sz;
        void *f = files_open(NULL, p, &sz);
        if (!f) continue;
        files_close(NULL, f);
        fn(base, false, user);
        any = true;
    }
    return any;
}

static bool mount_file_assets(void)
{
    VfsBackend b = { NULL, files_open, files_read_at, files_close, files_list, NULL };
    vfs_mount(&b, "gasm assets (disc files)");
    if (vfs_exists("ART/ART.CAR")) return true;
    vfs_unmount();
    return false;
}

/* ------------------------------------------------------------------ params */

static bool param(const char *name, char *dst, uint32_t cap) { return gasm_param_str(name, dst, cap); }

static bool param_on(const char *name)
{
    char v[16];
    return param(name, v, sizeof v) && strcmp(v, "0") && strcmp(v, "false") && strcmp(v, "no") && strcmp(v, "off");
}

static void param_env(const char *name, const char *env)
{
    char v[256];
    if (param(name, v, sizeof v) && *v) setenv(env, v, 1);
}

/* ------------------------------------------------------------------ exports */

static char *args[16];
static char level_arg[256];
static float audio_buf[MAX_AUDIO_FRAMES * AUDIO_CHANNELS];
static bool running;

GASM_EXPORT("gasm_abi_version") int32_t openrf_gasm_abi_version(void) { return GASM_ABI_VERSION; }

GASM_EXPORT("gasm_init") int32_t openrf_gasm_init(void)
{
    gasm_set_frame_rate((double)RATE_NUM / RATE_DEN);
    gasm_audio_config(AUDIO_RATE, AUDIO_CHANNELS);
    if (!mount_image_asset("cd") && !mount_image_asset("rom") && !mount_file_assets()) {
        plat_error("Return Fire", "game data not found: pass your Return Fire disc image as asset \"cd\" "
                   "(gasm-run openrf.wasm --asset cd=<image>.bin), or the CD's files (--asset-dir <folder>)");
        return 1;
    }
    char msg[600];
    snprintf(msg, sizeof msg, "data: %s", vfs_describe());
    log_str(msg);

    int argc = 0;
    args[argc++] = "openrf";
    if (param_on("skip_intro")) args[argc++] = "--skip-intro";
    if (param_on("play")) args[argc++] = "--play";
    if (param_on("play2")) args[argc++] = "--play2";
    if (param_on("viewer")) args[argc++] = "--viewer";
    if (param("level", level_arg, sizeof level_arg) && *level_arg) { args[argc++] = "--level"; args[argc++] = level_arg; }
    args[argc] = NULL;

    param_env("demo", "OPENRF_DEMO");
    param_env("cam_h", "OPENRF_CAM_H");
    param_env("sfx_log", "OPENRF_SFX_LOG");
    param_env("p2", "OPENRF_P2");
    char p1[64];
    if (!param("p1", p1, sizeof p1) || !*p1) strcpy(p1, "Player 1");   /* no login name on gasm */
    setenv("USER", p1, 1);
    setenv("OPENRF_P1", p1, 1);

    if (!app_init(argc, args)) return 1;
    running = true;
    return 0;
}

GASM_EXPORT("gasm_frame") void openrf_gasm_frame(void)
{
    if (!running) return;
    if (!app_frame()) {
        running = false;
        app_exit();
        __wasi_proc_exit(0);
    }
    int n = audio_frames_for_frame(RATE_NUM, RATE_DEN);
    if (n > MAX_AUDIO_FRAMES) n = MAX_AUDIO_FRAMES;
    audio_render(audio_buf, n);
    gasm_audio_push(audio_buf, (uint32_t)n);
}

GASM_EXPORT("gasm_exit") void openrf_gasm_exit(void)
{
    if (!running) return;
    running = false;
    app_exit();
}
