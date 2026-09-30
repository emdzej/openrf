# AGENTS.md

Guidance for coding agents working in this repository. Humans: see
[README.md](README.md) and [openrf.emdzej.pl](https://openrf.emdzej.pl).

## What this is

OpenRF is a faithful, from-scratch reimplementation of **Return Fire** (Silent Software,
Windows 95 edition, 1996) in C11 + SDL3. It is not an emulator or a wrapper: every subsystem is
ported function by function from the original executable (`RFIRE.BIN` on the CD — the real
game; `RFIRE.EXE` only picks a language DLL), in the same 16.16 fixed-point maths.

The Windows game is itself a port of the 3DO original: the renderer is a software emulation of
the 3DO cel engine drawing into an 8-bit DirectDraw surface. OpenRF keeps that structure.

**Fidelity is the product.** Reproduce the original's behaviour, including its quirks and bugs
(e.g. the lobbed tank shell that never lands, per-event sound frequencies that are never used,
the drone's pitch limit that never applies). If you find something that looks wrong, check the
disassembly; if the original does it, keep it and add a comment. Only fix deviations *of the
port* from the original.

## Hard rules

1. **Never commit original data or anything derived from it.** `cd/` (the extracted CD),
   `rfire_game.exe`, `re/` (decompilation dumps) and `out/` (decoded assets) are git-ignored —
   keep it that way. No `.bin/.iso/.cue`, no extracted sprites, sounds or movies.
2. **No data copied from the executable in the source tree.** Tables the game needs from
   `RFIRE.BIN` (3D models, vehicle descriptors, BNO table, explosion scripts, sound events, HUD
   and select-screen tables, strings) are **loaded at runtime** by `src/exe.c`, which checks the
   file (431,616 bytes, CRC-32 `64c49a1b`). Generators in `tools/gen_*.py` emit only addresses,
   counts, types and loader code. Small algorithmic constants and instruction operands are fine;
   tables, geometry and strings are not.
3. **Cite the original.** Every ported function names its source: `/* Camera_Update 0x403a40 */`.
   Keep names consistent with the Ghidra project (rename there as you learn).
4. **Don't paste decompiler output** into source or docs. Write the port and the docs in your own
   words; describe layouts, formulas and addresses.

## Layout

| Path | What |
|---|---|
| `src/` | Platform layer (`platform.c`, SDL3), asset loaders, `exe.c` (runtime PE tables), movies (`movie.c`, own Cinepak decoder `cinepak.c`), music director, sound mixer (`sfx.c`), HUD, bunker select UI, front end (`main.c`, `play.c`, `play_rules.c`, `endgame.c`), high scores |
| `src/render/` | Cel rasteriser (`cel.c`, `ccb.c`), camera + world renderer (`view.c`), models (`models_data.c`, generated loader) |
| `src/game/` | Simulation: objects, collision, shapes, vehicles, weapons, effects, AI (turret/drone/sub), rules (man/gate/flag/mine), fixed-point maths |
| `tests/` | Headless tests that run the real engine code against the original data |
| `tools/` | Python: format decoders (`car.py`, `rfm.py`, `stm.py`, `models.py`), reference renderer (`view.py`), `render_cmp.py`, table-descriptor generators, `rfexe.py` (PE reader), `screenshots.sh`, `ghidra/` scripts |
| `docs/` | VitePress site → openrf.emdzej.pl. User guide, how-tos, and the reverse-engineering notes (`architecture.md`, `render.md`, `game.md`, `car.md`, `rfm.md`, `stm.md`). `CONTEXT.md` is an agent briefing, excluded from the site |
| `.github/workflows/` | `ci.yml` (build app + tests), `release.yml` (universal .app on tag), `pages.yml` (docs) |

## Game data

Everything at runtime comes from the user's CD. For development, extract it to `./cd`
(see `docs/howto/extract-cd.md`) — it must contain `ART/`, `SOUND/`, `TITLE/`, `WORLDS/` and
`RFIRE.BIN`. If `./cd` exists at configure time, the build symlinks it into the bundle as
`Contents/Resources/data`. Asset paths go through `assets_path()`, which matches
case-insensitively (the disc is upper case, the game's code mixed case).

## Build and test

```sh
brew install cmake sdl3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

for t in sim_test combat_test sfx_test ai_test rules_test twoplayer_test; do ./build/$t > /dev/null || echo "$t FAILED"; done
python3 tools/render_cmp.py        # C renderer vs tools/view.py: must be 0 mismatches in all scenes
```

Universal release build (what CI does): add `-DOPENRF_BUNDLED_SDL=ON
-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`; SDL3 is fetched and linked statically.

**Before claiming a change works:**
- All test binaries exit 0 and `render_cmp.py` reports 0 mismatches.
- For refactors, capture test output (and `out/sfx/test.wav`) before and after: it must be
  byte-identical. Behaviour changes need a reason tied to the original.
- For anything visible, run the app and look at a frame (below).

## Running the app unattended

```sh
OPENRF_MUTE=1 OPENRF_FIXED_STEP=1 OPENRF_DEMO=fire OPENRF_SHOT=/tmp/x.bmp OPENRF_SHOT_MS=11000 \
  "build/Return Fire.app/Contents/MacOS/Return Fire" cd --skip-intro --play
sips -s format png /tmp/x.bmp --out /tmp/x.png
```

| Variable | Effect |
|---|---|
| `OPENRF_MUTE=1` | **Always set for unattended runs** — silences output, mixing unchanged |
| `OPENRF_SHOT=<bmp>`, `OPENRF_SHOT_MS=<ms>` | Save one frame and quit. **Never use macOS `screencapture`** (it captures the user's whole desktop) |
| `OPENRF_FIXED_STEP=1` | Virtual 16 ms/frame clock, for reproducible frames (the normal clock is wall time) |
| `OPENRF_DEMO=<mode>` | Scripted input: `1`, `fire`, `jeep`, `msv`, `heli`, `turret`, `drone`, `sub`, `rules`, `win`; with `--play2`: `2p`, `2pheli`, `2pwin`, `2pspectate` |
| `OPENRF_SFX_LOG=1`, `OPENRF_CAM_H`, `OPENRF_HS`, `OPENRF_P1/P2` | Sound log, camera height, high-score file, 2P names |

Options: `--skip-intro`, `--play`, `--play2`, `--level <n|path>`, `--viewer`.
`tools/screenshots.sh` regenerates the docs screenshots (muted).

## Reverse engineering

- Ghidra project `returnfire`, program `rfire_game.exe` (a copy of `cd/RFIRE.BIN`), via
  [ghidra-cli](https://github.com/akiselev/ghidra-cli): `ghidra decompile 0xADDR --project
  returnfire --program rfire_game.exe`, plus `x-ref`, `disasm`, `memory`, `rename`.
- Grep the full dump: `ghidra script run tools/ghidra/DumpAllNamed.java --project returnfire
  --program rfire_game.exe -- "$PWD/re/all_named.c"`.
- `FixMul` (0x438300) is `__fastcall` — Ghidra hides its arguments unless the signature is set.
- Record findings in the matching `docs/*.md` with exact layouts and addresses.

## Traps (already solved: don't reintroduce)

- **Logical resolution is 320×240.** Hi-res (640×480) is the same cels rasterised at 2×
  (corners `>>15` instead of `>>16`, clip doubled). Don't lay anything out in 640×480 coordinates.
- **Palette +10.** Sprite pixels index the DirectDraw palette = the RFA/BMP palette shifted up by
  10 (slots 0–9 and 246–255 are Windows system colours). RFA status bars need `+10` when blitted.
- **Rendering mutates game state.** Turrets activate from the draw pass (`Model_TowerHook`),
  men are removed after 120 undrawn ticks, wreck timers count from the last drawn tick, and the
  idle-drone gate reads "enemy turrets in view last frame". Headless tests must render the view.
- **Input magnitudes.** The input word needs the magnitude bytes (`0xff` for left/right,
  `0xff00` for up/down) or vehicles move at 1/256 speed.
- **Variable timestep.** Frames advance by whole 16 ms ticks (0–12). Anything future-networked
  must stay deterministic: integer maths, the MSVC `rand()` port, no wall-clock reads in the sim.
- **Paths with parentheses.** The dev checkout lives under `Return Fire (Europe) (…)`; CMake
  custom commands need `VERBATIM`, shell snippets need quoting.
- **Fat binaries.** `otool -L` prints per-architecture header lines; filter indented lines only.
- **Homebrew SDL3** is built for the host macOS version, so local builds warn about the 11.0
  deployment target. Harmless; release builds use the bundled static SDL3.

## Docs site

VitePress in `docs/` (pnpm, `cd docs && pnpm build`). Camouflage colour theme in
`docs/.vitepress/theme/camo.css` (sampled from the game's status bar). **No emojis**, no version
menu (the GitHub link is enough). Describe OpenRF as cross-platform: macOS today, Windows and
Linux planned. Screenshots come from `tools/screenshots.sh`.

## Releases

Plain semver tags, **no `v` prefix**: bump `project(OpenRF VERSION …)` in `CMakeLists.txt`,
then `git tag 0.2.0 && git push origin 0.2.0`. The workflow builds the universal `.app`, checks
it links only system libraries, signs it ad hoc and attaches the zip + SHA-256.

## Roadmap context

- Windows and Linux builds: SDL3 covers the platform layer; `main.c`'s data-root lookup uses
  macOS `_NSGetExecutablePath`.
- Online two-player: deterministic lockstep (exchange input words, periodic state hashes) over
  the [swsrs](https://github.com/emdzej/swsrs) relay. Keep the simulation deterministic.

## Commits

Imperative subject, body explaining why when non-obvious. Don't commit `cd/`, `re/`, `out/`,
`build/` or `__pycache__/`.
