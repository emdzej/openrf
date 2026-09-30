# Introduction

**OpenRF** is a native reimplementation of **Return Fire**, the 1995 capture-the-flag
action game by Silent Software (3DO), in its 1996 Windows 95 edition.

Instead of emulating Windows, OpenRF re-implements the game engine: every subsystem —
rendering, vehicle physics, AI, weapons, sound, music and the front end — was reverse
engineered from the original executable and rewritten in portable C. It reads all of its
content (graphics, maps, movies, sounds, music) directly from the original CD, so it looks
and plays like the original, but runs natively on modern computers.

<img src="/screenshots/drive.png" alt="Driving a tank" style="image-rendering: pixelated; border-radius: 6px">

## What you need

- A Mac running macOS 11 or newer (Apple Silicon or Intel), any system with the
  [gasm](./gasm) runtime (macOS, Linux, Windows), or a web browser ([Play](/play/){target="_self"}). Native Windows and Linux builds are
  [planned](#platforms).
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

OpenRF is written in portable C11; all platform-specific code lives in one small layer, with two
implementations: SDL3 (the native app) and gasm (a WebAssembly module, which also runs in the browser).

| Platform | State |
|---|---|
| macOS 11+ (universal: Apple Silicon and Intel) | Available |
| gasm runtime (`openrf.wasm`: macOS, Linux, Windows via `gasm-run` 0.3+) | Available, see [Running on gasm](./gasm) |
| Web browser (`openrf.wasm` on gasm's browser host: Chrome, Edge, Firefox, Safari) | Available: [Play](/play/){target="_self"} |
| Windows | Planned |
| Linux | Planned |

Next: [install OpenRF](./install).
