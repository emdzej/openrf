# Running on gasm

<a class="gasm-badge" href="https://gasm.emdzej.pl"><img class="gasm-badge-light" src="https://gasm.emdzej.pl/badge/built-for-gasm-light.svg" alt="Built for gasm" width="120" height="44"><img class="gasm-badge-dark" src="https://gasm.emdzej.pl/badge/built-for-gasm-dark.svg" alt="Built for gasm" width="120" height="44"></a>

Besides the macOS app, OpenRF builds as **`openrf.wasm`**, a game module for
[gasm](https://gasm.emdzej.pl), a portable game runtime on WebAssembly. The same file runs in
gasm's native runner (`gasm-run`, macOS, Linux, Windows) and in its browser runner. It is the same
engine as the app, with a different platform layer: the frames, sounds and gameplay are identical
(the test runs below compare them pixel for pixel).

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
`--param level=12 --param play=1` or `--keymap FILE`. `OPENRF_CD=<CD>` uses a CD for one run without
saving it. Each bundle has a `README.txt` with the same details, and the licences (OpenRF GPL-3.0,
gasm-run MIT).

## Get the files

To use your own `gasm-run` instead of a bundle:

- `openrf-<version>.wasm` from the [releases](https://github.com/emdzej/openrf/releases) (with its
  SHA-256), or build it (below).
- `gasm-run` **0.3.0 or newer** from the [gasm releases](https://github.com/emdzej/gasm/releases), or
  `cargo install gasm-host`. Folders (`--asset-dir`) and keyboard layouts (`--keymap`) need 0.3.0; with
  0.2.0 only a disc image (`--asset cd=`) works.
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
resident memory with `--asset-dir`, 52 MB with `--asset cd=<.bin>`. Esc closes the runner. `--mute`
silences it.

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
gasm-run openrf.wasm --asset-dir cd --param skip_intro=1 --param level=12 --param play=1
```

The title screen's number keys (1-9, Shift+1-9) are not available on gasm, which exposes pads only:
use `level=` instead.

## Controls

gasm gives the game virtual gamepads; the game maps them as described in
[Controls](./controls#gamepads). Connected gamepads take pads 1 and 2 in connection order; without
them, gasm maps the keyboard with its default two-player layout (the same in `gasm-run` and in the
browser):

| Pad | In the game | Keys, pad 1 | Keys, pad 2 |
|---|---|---|---|
| D-pad | Forward / back, turn | Arrows | I / J / K / L |
| A | Button 1 (fire, launch, dock) | X | . (period) |
| B | Button 2 | Z | , (comma) |
| X | Button 3 | S | M |
| L / R | Buttons 4 / 5 (turret, strafe) | Q / W | U / O |
| START | Title: one-player game; bunker: launch | Enter | Right Ctrl, keypad Enter |
| SELECT | Title: two-player game; in play: swap sides | Right Shift | Backspace |
| START + SELECT | Leave the level | Enter + Right Shift | Right Ctrl + Backspace |

Pad 2's keys work while fewer than two gamepads are connected, so **two players can share one
keyboard**: start a two-player game with pad 1's SELECT (Right Shift) or pad 2's START (Right Ctrl) on
the title screen. With one gamepad connected, it is player 1 and pad 2 stays on the keyboard.

To change the keys, write a layout file: one binding per line, `<pad 1-4> <button> <key code>...`
(buttons `a b x y l r select start up down left right`, key codes are the W3C `KeyboardEvent.code`
names such as `KeyW`, `ArrowUp`, `Numpad8`). `gasm-run --print-keymap` prints the default as a
starting point; pass yours with `--keymap FILE`, or save it as `keymap.txt` in gasm's data directory
(`~/Library/Application Support/gasm/` on macOS). For example, the original game's keys (W A S D and
H J K for player 1, the keypad for player 2):

```text
1 up KeyW
1 down KeyS
1 left KeyA
1 right KeyD
1 a KeyH
1 b KeyJ
1 x KeyK
1 l KeyQ
1 r KeyE
1 start Enter
1 select ShiftRight
2 up Numpad8
2 down Numpad5
2 left Numpad4
2 right Numpad6
2 a NumpadSubtract
2 b NumpadAdd
2 x NumpadEnter
2 l Numpad7
2 r Numpad9
2 start Numpad0
2 select NumpadDecimal
```

The browser player has the same format in its **Keyboard layout** dialog.

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
- High scores are kept in the site's IndexedDB. **Remove imported data** deletes the CD copy (not the
  scores).
- The browser may clear site data when the disk is nearly full; **Keep it** asks it not to
  (`navigator.storage.persist()`).
- Test parameters (URL query): the launch parameters below, and `hashframes=N` to run N frames without
  input and print the `video_fnv32=...` line of `gasm-run --headless`.

## High scores

High scores are kept in gasm's per-game storage under the key `RFire_HS`, in the same format as the
app's file. The namespace is the module's file name, so for `openrf.wasm`:

- `gasm-run`: `~/Library/Application Support/gasm/openrf/RFire_HS` (macOS),
  `~/.local/share/gasm/openrf/RFire_HS` (Linux), `%APPDATA%\gasm\openrf\RFire_HS` (Windows);
  `--storage-dir <dir>` puts them elsewhere. A renamed module (`openrf-0.2.0.wasm`) gets its own
  namespace; `--storage-id openrf` keeps the old one.
- Browser: the site's IndexedDB (database `gasm`, namespace `openrf`); on this site's
  [play page](/play/){target="_self"} that is `openrf.emdzej.pl`'s storage.
- Headless runs start with empty storage (unless `--storage-dir` is given).

To carry over the app's scores, copy `~/Library/Application Support/Return Fire/RFire_HS` into the
gasm directory.

## Build

Needs CMake, [wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases) and gasm's C SDK
(`gasm-c-sdk-<version>.zip` from the gasm releases, or the `sdk/c` folder of a gasm checkout). The C SDK
(`gasm.h`, the toolchain file) is the same in gasm 0.2.0 and 0.3.0, so either builds the module; 0.3.0 is
only needed to run it from a folder.
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

gasm's headless Node runner (`node runners/web/headless.mjs`, same options) prints the same hashes, and so
does the browser player (`/play/?hashframes=689&skip_intro=1&play=1&demo=fire`, after importing the CD):
the same module, CD and parameters give the same frames and sound on every runner, whether the data comes
from an image, a folder, OPFS or picked files. Two players without a keyboard: `--param play2=1 --param
demo=2p` (or `2pheli`, `2pwin`, `2pspectate`) scripts both pads (900 frames:
`video_fnv32=660f4ab4 audio_fnv32=31f91835`).

Speed (Apple M-series, `gasm-run --headless --no-hash`, two-player split screen): about 1750 frames
per second, 28 times real time; hashing every frame's pixels brings it to about 450.
