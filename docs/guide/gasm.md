# Running on gasm

Besides the macOS app, OpenRF builds as **`openrf.wasm`**, a game module for
[gasm](https://gasm.emdzej.pl), a portable game runtime on WebAssembly. The same file runs in
gasm's native runner (`gasm-run`, macOS, Linux, Windows) and in its browser runner. It is the same
engine as the app, with a different platform layer: the frames, sounds and gameplay are identical
(the test runs below compare them pixel for pixel).

On gasm the game runs at a fixed 62.5 frames per second, one tick of the original's 16 ms game
clock per frame, and everything is deterministic: the same inputs give the same game on every
runner, which is what future online two-player play builds on.

## Get the files

- `openrf-<version>.wasm` from the [releases](https://github.com/emdzej/openrf/releases) (with its
  SHA-256), or build it (below).
- `gasm-run` from the [gasm releases](https://github.com/emdzej/gasm/releases), or
  `cargo install gasm-host`.
- Your Return Fire CD, as a disc image or as a folder (see [Game data](./game-data)).

## Run

With a disc image: pass the image as the asset named **`cd`**. A raw `.bin` (MODE1/2352) or an
`.iso` works; for a `.bin`/`.cue` pair, pass the **`.bin`** (the cue sheet only names it).

```sh
gasm-run openrf.wasm --asset "cd=Return Fire (Europe) (En,Fr,De,Es,It).bin"
```

With the CD's files: a mounted CD, a mounted image, or an extracted folder (the one holding
`RFIRE.BIN`, `ART`, `SOUND`, `TITLE` and `WORLDS`):

```sh
gasm-run openrf.wasm --asset-dir /Volumes/RFIRE
gasm-run openrf.wasm --asset-dir ~/ReturnFire/cd
```

The data is read on demand in both cases (the 220 MB music file is streamed), so the game starts at
once. Esc closes the runner. `--mute` silences it.

## Launch parameters

Parameters replace the app's command-line options and environment variables. Pass them as
`--param name=value` (in the browser runner: URL query parameters).

| Parameter | Effect | App equivalent |
|---|---|---|
| `skip_intro=1` | Skip the intro stills and movies | `--skip-intro` |
| `play=1` | Go straight into the `level` map (level 1 by default) | `--play` |
| `play2=1` | Go straight into a two-player game ("Driving School", or the `level` 2-player map) | `--play2` |
| `level=<n or path>` | Map: 1-100 one-player, 101-204 two-player, or a path such as `WORLDS/2PLAYER/LEVEL3/RFMAP115.RFM` | `--level` |
| `viewer=1` | The map viewer | `--viewer` |
| `p1=<name>`, `p2=<name>` | Player names for the high scores (default "Player 1" / "Player 2") | `$USER`, `OPENRF_P1/P2` |
| `demo=<mode>` | Scripted input for tests: `1`, `fire`, `jeep`, `msv`, `heli`, `turret`, `drone`, `sub`, `rules`, `win`; with `play2=1`: `2p`, `2pheli`, `2pwin`, `2pspectate` | `OPENRF_DEMO` |
| `cam_h=<n>` | Driving camera height (debug) | `OPENRF_CAM_H` |
| `sfx_log=1` | Sound-effect log (debug) | `OPENRF_SFX_LOG` |

```sh
gasm-run openrf.wasm --asset cd=rf.bin --param skip_intro=1 --param level=12 --param play=1
```

The title screen's number keys (1-9, Shift+1-9) are not available on gasm, which exposes pads only:
use `level=` instead.

## Controls

gasm gives the game virtual gamepads; the game maps them as described in
[Controls](./controls#gamepads). Connected gamepads take pads 1 and 2 in connection order; without
them, `gasm-run` maps the keyboard:

| Pad | In the game | `gasm-run` keys, pad 1 | `gasm-run` keys, pad 2 |
|---|---|---|---|
| D-pad | Forward / back, turn | Arrows | I / J / K / L |
| A | Button 1 (fire, launch, dock) | X | . |
| B | Button 2 | Z | , |
| X | Button 3 | S | M |
| L / R | Buttons 4 / 5 (turret, strafe) | Q / W | U / O |
| START | Title: one-player game; bunker: launch | Enter | Right Ctrl, keypad Enter |
| SELECT | Title: two-player game; in play: swap sides | Right Shift | Backspace |
| START + SELECT | Leave the level | Enter + Right Shift | |

The second keyboard layout is used while fewer than two gamepads are connected, so two players can
share one keyboard.

## High scores

High scores are kept in gasm's per-game storage under the key `RFire_HS`, in the same format as the
app's file. The namespace is the module's file name, so for `openrf.wasm`:

- `gasm-run`: `~/Library/Application Support/gasm/openrf/RFire_HS` (macOS),
  `~/.local/share/gasm/openrf/RFire_HS` (Linux), `%APPDATA%\gasm\openrf\RFire_HS` (Windows);
  `--storage-dir <dir>` puts them elsewhere. A renamed module (`openrf-0.2.0.wasm`) gets its own
  namespace; `--storage-id openrf` keeps the old one.
- Browser runner: the site's IndexedDB (database `gasm`).
- Headless runs start with empty storage (unless `--storage-dir` is given).

To carry over the app's scores, copy `~/Library/Application Support/Return Fire/RFire_HS` into the
gasm directory.

## Build

Needs CMake, [wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases) and gasm's C SDK
(`gasm-c-sdk-<version>.zip` from the gasm releases, or the `sdk/c` folder of a gasm checkout).
`tools/fetch-gasm-sdk.sh` downloads both into `.deps/`:

```sh
tools/fetch-gasm-sdk.sh
cmake -S . -B build-gasm -DOPENRF_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm -j        # -> build-gasm/openrf.wasm (about 300 KB)
```

`OPENRF_GASM_OPT` picks the optimisation level (default `-O2`; `-Oz` is about 50 KB smaller and 20% slower). To skip the
start-up compilation (about 45 ms), compile it ahead of time: `gasm-run openrf.wasm --compile openrf.cwasm`, then run the
`.cwasm` with the same options.

## Headless runs and checks

`gasm-run --headless N` runs N frames without a window or sound and prints hashes of everything
the game showed and played; with `--screenshot out.png` it saves the last frame. Frame N shows the
game at 16 x (N - 1) ms, the frame the app saves with `OPENRF_FIXED_STEP=1 OPENRF_SHOT_MS=16 x (N - 1)`.

```sh
gasm-run openrf.wasm --asset cd=rf.bin --headless 689 --screenshot fire.png \
  --param skip_intro=1 --param play=1 --param demo=fire
# frames=689 presented=689 size=640x480
# video_fnv32=d014dcfb audio_fnv32=f1520dc1 audio_frames=486158
```

`--input "FROM-TO:BUTTON+BUTTON,..."` scripts pad 1 by frame number, for example start a game,
launch the tank, drive and fire:

```sh
gasm-run openrf.wasm --asset cd=rf.bin --headless 780 --screenshot drive.png --param skip_intro=1 \
  --input "100-104:START,250-254:A,450-760:UP,600-640:LEFT,740-744:A,770-774:A"
```

gasm's headless Node runner (`node runners/web/headless.mjs`, same options) prints the same hashes.

Speed (Apple M-series, `gasm-run --headless --no-hash`, two-player split screen): about 1750 frames
per second, 28 times real time; hashing every frame's pixels brings it to about 450.
