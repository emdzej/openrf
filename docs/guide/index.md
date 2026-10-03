# Introduction

**OpenRF** is a reimplementation of **Return Fire**, the 1995 capture-the-flag
action game by Silent Software (3DO), in its 1996 Windows 95 edition.

Instead of emulating Windows, OpenRF re-implements the game engine: every subsystem —
rendering, vehicle physics, AI, weapons, sound, music and the front end — was reverse
engineered from the original executable and rewritten in portable C. It reads all of its
content (graphics, maps, movies, sounds, music) directly from the original CD, so it looks
and plays like the original, but runs on modern computers and in the browser.

<img src="/screenshots/drive.png" alt="Driving a tank" style="image-rendering: pixelated; border-radius: 6px">

## What you need

- A Mac (Apple Silicon or Intel), a Linux (x86_64 or arm64) or Windows 10/11 (x86_64) PC for the
  [gasm bundles](./install), or a web browser ([Play](/play/){target="_self"}).
- Your own copy of **Return Fire for Windows 95** (the CD, or a BIN/CUE or ISO image of it).
  OpenRF does not include any of the original game's files.

## Status

| Area | State |
|---|---|
| Intro and victory movies, title screen | Done |
| Music director (the orchestral score) and sound effects | Done |
| Perspective renderer with 3D buildings and vehicles | Done |
| All four vehicles, bunker lift, fuel and ammo | Done |
| Weapons, explosions, damage, debris | Done |
| Turrets, drones, submarine, soldiers, gates, flag, mines | Done |
| Winning, losing, level progression, high scores | Done |
| Two-player split screen | Done |
| Network play | Planned |

## Platforms

OpenRF is written in portable C11; all platform-specific code lives in one small layer, implemented for
[gasm](https://gasm.emdzej.pl): the game is one WebAssembly module, `openrf.wasm`, which runs on the gasm
runner on every desktop system and in the browser.

| Platform | State |
|---|---|
| macOS (Apple Silicon and Intel) | Available: `Return Fire (gasm).app` |
| Linux x86_64 and arm64 | Available as a gasm bundle (`openrf.sh`) |
| Windows 10/11 x86_64 | Available as a gasm bundle (`OpenRF.cmd`) |
| Web browser (Chrome, Edge, Firefox, Safari) | Available: [Play](/play/){target="_self"} |
| Any gasm runner (`openrf.wasm`, `gasm-run` 0.5+) | Available, see [Running on gasm](./gasm) |

The gasm bundles are the same `openrf.wasm` with the released `gasm-run` and a launcher; see
[Installing](./install) for the downloads.

Next: [install OpenRF](./install).
