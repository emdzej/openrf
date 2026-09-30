# OpenRF — native macOS Return Fire

**Docs, user guide and screenshots: [openrf.emdzej.pl](https://openrf.emdzej.pl)** ·
[Download](https://github.com/emdzej/openrf/releases)

![OpenRF](docs/public/screenshots/tank-fire.png)

A from-scratch reimplementation of **Return Fire** (Silent Software, Windows 95 edition, 1996)
for Apple Silicon Macs. It contains no original code, assets or data: it reads the data files, and
the tables of the original game program (`RFIRE.BIN`: 3D models, object and sound tables), directly
from your own copy of the game CD.

## Build

Requirements: Xcode command line tools, CMake, SDL3 (`brew install cmake sdl3`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This produces `build/Return Fire.app`.

## Game data

Extract the CD image so the directory contains `ART/`, `SOUND/`, `TITLE/`, `WORLDS/` and `RFIRE.BIN`
(the original game program, on the root of the CD; OpenRF checks it is the supported build):

```sh
# from the .bin/.cue (MODE1/2352): strip the 16-byte sector headers, then extract the ISO
python3 -c "import sys;f=open(sys.argv[1],'rb');o=open('rf.iso','wb')
while (s:=f.read(2352)): o.write(s[16:2064])" "Return Fire (Europe) (En,Fr,De,Es,It).bin"
7z x rf.iso -ocd
```

If `./cd` exists at build time, the build links it into the bundle as
`Return Fire.app/Contents/Resources/data`, so the app can be opened from Finder. Otherwise the game
looks for `./cd` next to the bundle, or takes a path as the first argument:

```sh
"build/Return Fire.app/Contents/MacOS/Return Fire" cd
```

Options: `--skip-intro`, `--play` (straight into the `--level` map, level 1 by default), `--play2` (straight into
a 2-player game: "Driving School" or the `--level` 2-player map), `--level <n | path>` (n = 1..100 one-player
maps, 101..204 two-player maps, or a path such as `WORLDS/2PLAYER/LEVEL3/RFMAP115.RFM`; the player count comes
from the map), `--viewer` (map viewer).
Title screen: `1`–`9` start the first map of a difficulty level, `Shift`+`1`–`9` the same for two players,
`F2` the current map, `F3` the current two-player map. After a 2-player game the scores (wins per side, draws)
are kept per pair of names: `OPENRF_P1` / `OPENRF_P2` (default `$USER` / "Player 2").
Debug: `OPENRF_DEMO=1` scripts input (launch a tank and drive); `OPENRF_CAM_H=<n>` sets the driving
camera height (0 = 1.0x zoom; the value recovered from the binary is -170 = 2.3x, unverified).
Debug: `OPENRF_SHOT=out.bmp OPENRF_SHOT_MS=2000` saves one frame and exits. `OPENRF_FIXED_STEP=1` runs on a virtual 16 ms/frame clock
(reproducible frames, for before/after `cmp`).
Debug (2-player maps): `OPENRF_DEMO=2p` (player 1 launches a tank, player 2 a jeep, both drive), `2pheli`
(player 2 takes the heli), `2pwin` (player 2 wins: green banner), `2pspectate` (player 1 loses everything:
fly-back skull, then the spectate screen).

## Controls (original defaults)

| | Player 1 | Player 2 |
|---|---|---|
| Move | W A S D (or arrows) | Num 8 4 5 6 |
| Buttons 1–3 | H J K | Num − + Enter |
| Buttons 4–5 | Q E | Num 7 9 |

In the bunker: W/S/A/D choose a vehicle, H launches (player 2: Num 8/5/4/6, Num −). Driving back onto the pad
and pressing H docks. In a 2-player game the screen is split (player 1 left, blue; player 2 right, green) and
Alt+3 swaps the two keyboard layouts between the players (Swap Sides). A player left with no vehicles while the
other still has jeeps watches the skull screen until the game ends; with no jeeps on either side it is a draw.
Esc leaves the level. Alt+Enter toggles fullscreen, M mutes the game. Map viewer: arrows scroll, `[` `]` change map, Esc returns.

## Status

| Area | State |
|---|---|
| Intro stills + STM movies (own Cinepak decoder) | done |
| Title screen, music director (SCORE.WAV track table) | done |
| ART.CAR sprites, TRANS.TBL, RFM maps | done |
| Perspective world renderer, 3D models (pixel-identical to tools/view.py) | done |
| Object system, bunker/lift, vehicle movement, collision, water | done (1P and 2P) |
| Vehicle models (turret, tracks, rotor, shadows, wrecks, lift) | done |
| Bunker vehicle-select screen, dashboard HUD, radar, fuel/ammo gauges | done |
| Weapons, explosions, damage, sound effects | done |
| Turrets, drone, submarine, soldiers, gates, flag, mines, win/loss, high scores | done |
| 2-player split screen (views, status bar, HUDs, two listeners, spectate, draw, 2P scores) | done |

## Layout

- `src/` — engine (C11 + SDL3). Original function addresses are cited in comments.
- `tools/` — Python decoders/reference renderers for each format.
- `docs/` — reverse-engineering notes: `car.md`, `rfm.md`, `stm.md`, `architecture.md`, …
- `re/` — Ghidra scripts and decompiler dumps (local reference only).

## License

OpenRF is free software under the [GNU GPL v3](LICENSE). Return Fire is © 1995–1996
Silent Software / Prolific Publishing; OpenRF contains none of its code or assets —
you need your own copy of the game.
