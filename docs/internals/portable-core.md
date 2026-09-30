# Portable core

OpenRF is split into a **portable core** (everything in `src/`, `src/render/` and `src/game/` except the
backends) and **platform backends**. The core includes no platform headers: CMake builds it as the
`openrf_core` object library without SDL on the include path, so a platform dependency cannot creep in.
The SDL3 backend is the only one today; the same core is meant to run as a
[gasm](https://github.com/emdzej/gasm) guest (a wasm module driven by a host at a fixed frame rate,
with no filesystem).

| File | Role |
|---|---|
| `src/platform.h` | The contract a backend implements (video, input, time, audio lock, storage) |
| `src/app.h`, `src/app.c` | The front end as a frame-driven state machine |
| `src/vfs.h`, `src/vfs.c` | File layer and the ISO 9660 disc-image reader |
| `src/audio.h`, `src/audio.c` | One 44100 Hz stereo mixer: music, movie PCM, sound effects |
| `src/input.c`, `src/keys.h` | Keyboard and pad mapping onto the game's input word |
| `src/platform_sdl.c` | SDL3 backend and `main()` |
| `src/vfs_host.c`, `src/storage_file.c` | POSIX file backends (SDL build and tests) |

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
number and order of presents and clock reads are unchanged: with `OPENRF_FIXED_STEP=1` the frames are
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

Reads must be safe from the audio thread (music streams from `SOUND/SCORE.WAV` while the main thread loads
levels), so sources read positionally (`pread`) and the ISO reader keeps no shared cursor.

## Audio

`audio_render(float *out, int frames)` produces 44100 Hz interleaved stereo in [-1, 1] by summing:

- **music**: a byte range of `SCORE.WAV` / `DRUMS.WAV` streamed through `vfs_read_at` with the range,
  loop, seamless-continue and volume semantics the music director (`music.c`) needs;
- **movie PCM**: the `.STM` soundtrack (22050 Hz), pushed whole when the movie opens and linearly
  resampled to 44100 Hz; the movie's video is slaved to the frames rendered so far;
- **sound effects**: `sfx_render`, the port of the original software mixer (44100 Hz S16 stereo, the
  DirectSound primary format), unchanged.

The SDL backend pulls it from one audio stream callback, with the stream lock as `plat_audio_lock`. A
fixed-rate backend calls it once per frame for `audio_frames_for_frame(num, den)` frames (the fraction is
carried, e.g. 735 per frame at 60 Hz, 705 or 706 at 62.5 Hz). Before, SDL mixed separate streams and
resampled the movie audio itself, so movie sound differs slightly in resampling detail; music and SFX
samples are the same.

## Storage and input

High scores (`highscore.c`, the original's `RFire_HS` format) go through `plat_storage_*` with the key
`RFire_HS`; the SDL build keeps the file in `~/Library/Application Support/Return Fire/`. Pads are
mapped in `input.c` ([Controls](/guide/controls)): pad 1 is player 1, pad 2 player 2; the SDL backend
feeds real gamepads through the same path.
