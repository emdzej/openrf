# Portable core

OpenRF is split into a **portable core** (everything in `src/`, `src/render/` and `src/game/` except the
backends) and **platform backends**. The core includes no platform headers: the native CMake build
compiles it as the `openrf_core` object library with no platform library at all, so a platform
dependency cannot creep in. The game's backend is [gasm](https://github.com/emdzej/gasm) (`openrf.wasm`,
a wasm module driven by a host at a fixed frame rate, with no filesystem; see
[Running on gasm](/guide/gasm)); the native build has only the file backends the headless tests use.

| File | Role |
|---|---|
| `src/platform.h` | The contract a backend implements (video, input, time, audio lock, storage) |
| `src/app.h`, `src/app.c` | The front end as a frame-driven state machine |
| `src/vfs.h`, `src/vfs.c` | File layer and the ISO 9660 disc-image reader |
| `src/audio.h`, `src/audio.c` | One 44100 Hz stereo mixer: music, movie PCM, sound effects |
| `src/input.c`, `src/keys.h` | Keyboard and pad mapping onto the game's input word |
| `src/vfs_host.c`, `src/storage_file.c` | POSIX file backends (the native tests) |
| `src/platform_gasm.c` | gasm backend and the module's exports (`gasm_init`, `gasm_frame`, `gasm_exit`) |

## Frame-driven app loop

The original game, and the port until now, ran nested blocking loops: the intro stills and movies, the
title screen, the game loop, the end-of-game sequence (fades, waiting for the win music, the win movie),
the map viewer. Each called poll and present itself. A host that calls the game once per frame cannot
run that, so each loop is now a state with a step function, and one step is exactly one iteration of the
old loop.

```c
bool app_init(int argc, char **argv);   /* after the backend has mounted the data */
bool app_frame(void);                    /* false = quit */
void app_exit(void);
```

A step returns `STEP_FRAME` (it presented a frame), `STEP_AGAIN` (it changed state without presenting)
or `STEP_DONE` (finished; the owner moves on). `app_frame` polls, steps, and repeats until a step
presents. Because every step starts after a poll, just like every iteration of the old loops did, the
number and order of presents and clock reads are unchanged: on a fixed 16 ms clock the frames are
byte-identical to the blocking version. The only difference is an extra poll at some state changes
(for example between a still and the next movie), which consumes edge-triggered input the same way the
next loop's first poll did.

States: intro (items of the table at 0x455A40), start (sprites, options), viewer, menu, title, play.
A level (`play.c`) runs the game frame, then the end sequence (`play_rules.c` → `endgame.c`: EndGame
fade, win music, banner fade-in, win movie with the banner, high score). Movies (`movie.c`) and palette
fades (`fade_begin` / `fade_step`) are reusable stepped objects.

## Platform contract

`src/platform.h` documents every function. In short, a backend:

1. mounts the game data (`vfs.h`),
2. calls `app_init` once, `app_frame` until it returns false, then `app_exit`,
3. implements video (`plat_fb`, `plat_present`, `plat_present_rgb` for movie frames), input
   (`plat_poll`, `plat_key_down` with `keys.h` codes = USB HID usages, `plat_any_key_pressed`, `plat_pad`
   with the gasm button layout), time (`plat_ticks_ms`), the audio lock, storage
   (`plat_storage_get` / `plat_storage_set` / `plat_storage_location`) and `plat_error`.

`plat_poll` may be called several times per presented frame; edges are relative to the previous call.

## File layer

All data access goes through `vfs.h`: `vfs_open` / `vfs_read_at` (streaming, positional),
`vfs_read_all`, `vfs_size`, `vfs_exists`, `vfs_list` (directory listing for the map viewer). Names are
disc-relative and case-insensitive. One source is mounted at a time:

- a **directory** (`vfs_mount_path`, POSIX, `vfs_host.c`): the extracted CD, looked up component by
  component case-insensitively;
- a **disc image** (`vfs_mount_image`) read through a `VfsSource`, a "read bytes at offset" callback. The
  ISO 9660 tree is read once at mount time. Plain images (2048-byte sectors) and raw images (2352-byte
  sectors, MODE1 with a 16-byte header or MODE2 form 1 with 24) are detected from the sync pattern and
  the primary volume descriptor. `vfs_mount_path` backs it with a file (`.iso`, `.bin`, or a `.cue` whose
  `FILE` line names the image); a gasm backend backs it with `asset_read_at`.

Sources read positionally (`pread`, `asset_read_at`) and the ISO reader keeps no shared cursor, so a
backend may pull audio (music streams from `SOUND/SCORE.WAV`) on another thread than the one loading levels.

## Audio

`audio_render(float *out, int frames)` produces 44100 Hz interleaved stereo in [-1, 1] by summing:

- **music**: a byte range of `SCORE.WAV` / `DRUMS.WAV` streamed through `vfs_read_at` with the range,
  loop, seamless-continue and volume semantics the music director (`music.c`) needs;
- **movie PCM**: the `.STM` soundtrack (22050 Hz), pushed whole when the movie opens and linearly
  resampled to 44100 Hz; the movie's video is slaved to the frames rendered so far;
- **sound effects**: `sfx_render`, the port of the original software mixer (44100 Hz S16 stereo, the
  DirectSound primary format), unchanged.

A fixed-rate backend such as gasm calls it once per frame for `audio_frames_for_frame(num, den)` frames
(the fraction is carried, e.g. 735 per frame at 60 Hz, 705 or 706 at 62.5 Hz); a backend that pulls it from
an audio thread takes `plat_audio_lock` around it.

## The gasm backend

`platform_gasm.c` implements the contract over the gasm imports (ABI v0):

- **Lifecycle**: `gasm_init` sets the frame rate to 62.5 Hz (one original 16 ms tick per frame) and the
  audio format to 44100 Hz stereo, mounts the data, turns launch params into the option list and the
  environment the core reads (`demo` -> `OPENRF_DEMO`, `p1` -> `USER`/`OPENRF_P1`, ...), then calls
  `app_init`; a failure is logged and `gasm_init` returns 1. `gasm_frame` is one `app_frame` followed by
  `audio_render` + `audio_push` of `audio_frames_for_frame(125, 2)` frames (705 or 706). When the app
  quits (`app_frame` false) it calls `app_exit` and WASI `proc_exit(0)`; `gasm_exit` (the player closed
  the runner) calls `app_exit`.
- **Video**: the 640x480 8-bit framebuffer goes through its palette into RGBA for `video_present`; movie
  frames are presented at their own 320x240 (the runner scales both to the same 4:3 output).
- **Title**: the custom section `gasm.title` names the window and tab "Return Fire" (gasm 0.6.0; older
  runners ignore it).
- **Time**: `plat_ticks_ms` is 16 ms per presented frame, so frame N is always game time 16·(N−1) ms.
  Nothing reads the runner's clock, and the mixer runs on the frame, so movies (slaved to the audio
  played) are reproducible too.
- **Input**: the raw keyboard (gasm 0.5.0: `input_mode(KEYS_RAW)`, `key_state` mapped onto `keys.h`,
  `key_events` for the "any key" edge), so `input.c`'s original bindings apply; gamepads as
  `input_pad(0..3)`. Both are read at the first `plat_poll` of a frame and stable within it.
- **Data**: the asset `cd` (or `rom`) as a disc image through `vfs_mount_image` over `asset_read_at`,
  else the disc's files as assets named by their paths (`--asset-dir`): names are tried as spelled and
  upper-cased. `vfs_list` in that mode probes the map names the viewer looks for.
- **Storage**: `gasm:storage` get/set, key `RFire_HS`.

## Storage and input

High scores (`highscore.c`, the original's `RFire_HS` format) go through `plat_storage_*` with the key
`RFire_HS`: the gasm build keeps it in the runner's storage, the native tests in a file under
`~/Library/Application Support/Return Fire/` (`OPENRF_HS` overrides). Pads are mapped in `input.c`
([Controls](/guide/controls)): pad 1 is player 1, pad 2 player 2.
