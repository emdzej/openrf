# Running on gasm

<a class="gasm-badge" href="https://gasm.emdzej.pl"><img class="gasm-badge-light" src="https://gasm.emdzej.pl/badge/built-for-gasm-light.svg" alt="Built for gasm" width="120" height="44"><img class="gasm-badge-dark" src="https://gasm.emdzej.pl/badge/built-for-gasm-dark.svg" alt="Built for gasm" width="120" height="44"></a>

OpenRF is **`openrf.wasm`**, a game module for [gasm](https://gasm.emdzej.pl), a portable game runtime
on WebAssembly. The same file runs in gasm's native runner (`gasm-run`, macOS, Linux, Windows) and in its
browser runner, with identical frames, sounds and gameplay (the test runs below compare them hash for
hash).

On gasm the game runs at a fixed 62.5 frames per second, one tick of the original's 16 ms game
clock per frame, and everything is deterministic: the same inputs give the same game on every
runner, which is what future online two-player play builds on.

## Ready-made bundles

The easiest way to run the gasm build is a bundle from the
[releases](https://github.com/emdzej/openrf/releases): `openrf.wasm`, the released `gasm-run` it was
tested with, and a launcher that asks for your CD once and remembers it. See
[Installing](./install) for the first start on each system.

| Bundle | Start with | Saved CD |
|---|---|---|
| `openrf-gasm-<version>-macos-universal.zip` | `Return Fire (gasm).app` (hold Option to change the CD) | `~/Library/Application Support/OpenRF/cd-location` |
| `openrf-gasm-<version>-linux-x86_64.tar.gz`, `-linux-arm64.tar.gz` | `./openrf.sh [CD]` (`--install-desktop` for a menu entry) | `~/.config/openrf/cd-location` |
| `openrf-gasm-<version>-windows-x86_64.zip` | `OpenRF.cmd [CD]` | `%APPDATA%\OpenRF\cd-location` |

The CD can be a folder (the disc, a mounted image, a copy; passed as `--asset-dir`) or a raw `.bin` or
`.iso` file (`--asset cd=`); a `.cue` is refused, choose the `.bin` next to it. All three launchers take
the same options: `--change-cd`, `--forget-cd`, `--help`, `--dry-run` (print the `gasm-run` command
instead of running it), and pass anything after the CD on to `gasm-run`, for example
`--param level=12 --param play=1`, `--mute`, or `--filter xbr` (gasm-run 0.6.0: how the picture is
scaled up, `sharp` by default, also `nearest`, `xbr`, `fsr`, `crt`; `--integer-scale` for whole
multiples only). `OPENRF_CD=<CD>` uses a CD for one run without
saving it. Each bundle has a `README.txt` with the same details, and the licences (OpenRF GPL-3.0,
gasm-run MIT).

## Get the files

To use your own `gasm-run` instead of a bundle:

- `openrf-<version>.wasm` from the [releases](https://github.com/emdzej/openrf/releases) (with its
  SHA-256), or build it (below).
- `gasm-run` **0.5.0 or newer** from the [gasm releases](https://github.com/emdzej/gasm/releases), or
  `cargo install gasm-host`. The module reads the raw keyboard, which gasm added in 0.5.0; older runners
  stop it with an error on the first frame.
- Your Return Fire CD: the disc in a drive, a mounted disc image, or a folder you copied it to (see
  [Game data](./game-data)). A raw `.bin` or an `.iso` also works without mounting.

Or skip the download: [play in the browser](/play/){target="_self"}, which runs the same module.

## Run

**Recommended: the CD's files.** Point `--asset-dir` at the folder that holds `RFIRE.BIN`, `ART`,
`SOUND`, `TITLE` and `WORLDS`: the CD itself, a mounted image ([mount it](#mount-your-cd)) or an
extracted copy. Names are matched case-insensitively, so any copy works.

```sh
gasm-run openrf.wasm --asset-dir /Volumes/RFIRE        # macOS: the CD or a mounted .iso
gasm-run openrf.wasm --asset-dir D:\                   # Windows: the CD drive
gasm-run openrf.wasm --asset-dir /media/$USER/RFIRE    # Linux
gasm-run openrf.wasm --asset-dir ~/ReturnFire/cd       # an extracted folder
```

**A disc image file:** pass it as the asset named **`cd`**. A raw `.bin` (MODE1/2352) or an `.iso` works;
for a `.bin`/`.cue` pair, pass the **`.bin`** (the cue sheet only names it).

```sh
gasm-run openrf.wasm --asset "cd=Return Fire (Europe) (En,Fr,De,Es,It).bin"
```

Either way the data is read on demand, not loaded: the 220 MB music file is streamed and the game
starts at once. Measured with `gasm-run --headless` (the `fire` demo below, 689 frames): 53 MB maximum
resident memory with `--asset-dir`, 52 MB with `--asset cd=<.bin>`. Holding Esc closes the runner.
`--mute` silences it; `--filter <sharp|nearest|xbr|fsr|crt>` and `--integer-scale` choose how the
320x240 / 640x480 picture is scaled to the window (gasm-run 0.6.0+). The window is titled
"Return Fire".

### Mount your CD

A physical CD mounts by itself. A disc image has to be mounted to use it as a folder (or pass it
with `--asset cd=` instead):

| System | `.iso` | Where it appears |
|---|---|---|
| macOS | Double-click it (DiskImageMounter) | `/Volumes/RFIRE` |
| Windows 10/11 | Right-click, **Mount** (or double-click) | a new drive letter, e.g. `E:\` |
| Linux | `sudo mount -o loop,ro rf.iso /mnt/rfire`, or GNOME Disks: **Attach Disk Image** | `/mnt/rfire`, or `/media/$USER/RFIRE` |

A raw `.bin`/`.cue` pair (MODE1/2352) can't be mounted by these tools: convert it to an `.iso` first
([Extract the CD image](/howto/extract-cd#from-a-bin-cue-pair)), or give the `.bin` to OpenRF directly.
More in [Mount it](/howto/extract-cd#mount-it).

## Launch parameters

Pass options to the game as `--param name=value` (in the browser runner: URL query parameters).

| Parameter | Effect |
|---|---|
| `skip_intro=1` | Skip the intro stills and movies |
| `play=1` | Go straight into the `level` map (level 1 by default) |
| `play2=1` | Go straight into a two-player game ("Driving School", or the `level` 2-player map) |
| `level=<n or path>` | Map: 1-100 one-player, 101-204 two-player, or a path such as `WORLDS/2PLAYER/LEVEL3/RFMAP115.RFM` |
| `viewer=1` | The map viewer |
| `p1=<name>`, `p2=<name>` | Player names for the high scores (default "Player 1" / "Player 2") |
| `demo=<mode>` | Scripted input for tests: `1`, `fire`, `jeep`, `msv`, `heli`, `turret`, `drone`, `sub`, `rules`, `win`; with `play2=1`: `2p`, `2pheli`, `2pwin`, `2pspectate` |
| `cam_h=<n>` | Driving camera height (debug) |
| `sfx_log=1` | Sound-effect log (debug) |

```sh
gasm-run openrf.wasm --asset-dir cd --param skip_intro=1 --param level=12 --param play=1
```

The title screen's number keys (1-9, Shift+1-9) start the first map of a difficulty level; `level=` picks any map.

## Controls

The game reads the keyboard itself, with the original key bindings: W A S D
and H J K Q E for player 1, the keypad for player 2, F2 / F3, 1-9, Alt + 3, Esc (see
[Controls](./controls)). gasm's keyboard layout (`--keymap`, `keymap.txt`)
doesn't apply to OpenRF: the module switches it off (`input_mode` `KEYS_RAW`), so no key reaches
the game twice.

Gamepads still arrive as gasm's virtual pads, the first connected for player 1 and the second for player
2, mapped as in [Controls](./controls#gamepads).

Esc: a tap leaves the level (as in the original); holding it for a second quits `gasm-run` (in the
browser: stops the game). Fullscreen and muting are the runner's business (`--mute`; in the browser,
the buttons under the picture).

## In the browser

[Play](/play/){target="_self"} runs `openrf.wasm` in gasm's browser host, in a Web Worker. You choose
your CD folder once (Chrome and Edge: a folder picker; Firefox and Safari: a folder upload dialog); the
page checks it (`RFIRE.BIN`, `ART/ART.CAR`, `SOUND/SCORE.WAV`) and copies the parts the game uses
(about 275 MB: `RFIRE.BIN`, `ART`, `SOUND`, `TITLE`, `WORLDS`) into the site's private browser storage
(the origin private file system, OPFS). The game then reads it on demand, and later visits start at
once. **Play without importing** reads the picked files in place instead; a disc image (`.iso` or raw
`.bin`) works as well. Nothing is uploaded: the files stay on your computer.

- Sound starts with the first click (browsers require it). Fullscreen: the button, or double-click the
  picture.
- Scaling: the menu under the picture picks gasm's upscaling filter (`sharp` by default, `nearest`,
  `xbr`, `fsr`, `crt`) and **integer** limits it to whole multiples; also as `?filter=xbr` and `?integer`
  in the URL. It needs WebGL 2; without it the picture is scaled by the browser.
- High scores are kept in the site's IndexedDB. **Remove imported data** deletes the CD copy (not the
  scores).
- The browser may clear site data when the disk is nearly full; **Keep it** asks it not to
  (`navigator.storage.persist()`).
- Test parameters (URL query): the launch parameters below, and `hashframes=N` to run N frames without
  input and print the `video_fnv32=...` line of `gasm-run --headless`.

## High scores

High scores are kept in gasm's per-game storage under the key `RFire_HS`, in the original's format.
The namespace is the module's file name, so for `openrf.wasm`:

- `gasm-run`: `~/Library/Application Support/gasm/openrf/RFire_HS` (macOS),
  `~/.local/share/gasm/openrf/RFire_HS` (Linux), `%APPDATA%\gasm\openrf\RFire_HS` (Windows);
  `--storage-dir <dir>` puts them elsewhere. A renamed module (`openrf-0.2.0.wasm`) gets its own
  namespace; `--storage-id openrf` keeps the old one.
- Browser: the site's IndexedDB (database `gasm`, namespace `openrf`); on this site's
  [play page](/play/){target="_self"} that is `openrf.emdzej.pl`'s storage.
- Headless runs start with empty storage (unless `--storage-dir` is given).

Scores from the old native macOS app (up to 0.3.0) carry over: copy
`~/Library/Application Support/Return Fire/RFire_HS` into the gasm directory.

## Build

Needs CMake, [wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases) and gasm's C SDK
(`gasm-c-sdk-<version>.zip` from the gasm releases, or the `sdk/c` folder of a gasm checkout). The C SDK
(`gasm.h`, the toolchain file) must be 0.5.0 or newer (the raw keyboard imports), and so must the runner;
the pinned version is 0.6.0. `tools/fetch-gasm-sdk.sh` downloads both into `.deps/`:

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
game at 16 x (N - 1) ms, so a moment at T ms is frame ceil(T / 16) + 1 (11000 ms: frame 689).
`tools/screenshots.sh` makes the documentation screenshots this way.

```sh
gasm-run openrf.wasm --asset-dir cd --headless 689 --screenshot fire.png \
  --param skip_intro=1 --param play=1 --param demo=fire
# frames=689 presented=689 size=640x480
# video_fnv32=d014dcfb audio_fnv32=f1520dc1 audio_frames=486158
```

`--input "FROM-TO:BUTTON+BUTTON,..."` scripts pad 1 by frame number, for example start a game,
launch the tank, drive and fire:

```sh
gasm-run openrf.wasm --asset-dir cd --headless 780 --screenshot drive.png --param skip_intro=1 \
  --input "100-104:START,250-254:A,450-760:UP,600-640:LEFT,740-744:A,770-774:A"
```

Keys work the same way with `KEY(...)` (W3C key names); this run gives the same hashes as the pad
script with START, UP + A and LEFT:

```sh
gasm-run openrf.wasm --asset-dir cd --headless 900 --param skip_intro=1 \
  --input "30-35:KEY(F2),300-700:KEY(KeyW+KeyH),720-760:KEY(KeyA)"
```

gasm's headless Node runner (`node runners/web/headless.mjs`, same options) prints the same hashes, and so
does the browser player (`/play/?hashframes=689&skip_intro=1&play=1&demo=fire`, after importing the CD):
the same module, CD and parameters give the same frames and sound on every runner, whether the data comes
from an image, a folder, OPFS or picked files. Two players without input: `--param play2=1 --param
demo=2p` (or `2pheli`, `2pwin`, `2pspectate`) scripts both pads (900 frames:
`video_fnv32=660f4ab4 audio_fnv32=31f91835`).

Speed (Apple M-series, `gasm-run --headless --no-hash`, two-player split screen): about 1750 frames
per second, 28 times real time; hashing every frame's pixels brings it to about 450.
