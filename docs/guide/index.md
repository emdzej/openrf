# Introduction

**OpenRF** is a native macOS reimplementation of **Return Fire**, the 1995 capture-the-flag
action game by Silent Software (3DO), in its 1996 Windows 95 edition.

Instead of emulating Windows, OpenRF re-implements the game engine: every subsystem —
rendering, vehicle physics, AI, weapons, sound, music and the front end — was reverse
engineered from the original executable and rewritten in portable C. It reads all of its
content (graphics, maps, movies, sounds, music) directly from the original CD, so it looks
and plays like the original, but runs natively on modern Macs.

<img src="/screenshots/drive.png" alt="Driving a tank" style="image-rendering: pixelated; border-radius: 6px">

## What you need

- A Mac running macOS 11 or newer (Apple Silicon or Intel).
- Your own copy of **Return Fire for Windows 95** (the CD, or a BIN/CUE or ISO image of it).
  OpenRF does not include any of the original game's files.

## Status

| Area | State |
|---|---|
| Intro and victory movies, title screen | ✅ |
| Music director (the orchestral score) and sound effects | ✅ |
| Perspective renderer with 3D buildings and vehicles | ✅ |
| All four vehicles, bunker lift, fuel and ammo | ✅ |
| Weapons, explosions, damage, debris | ✅ |
| Turrets, drones, submarine, soldiers, gates, flag, mines | ✅ |
| Winning, losing, level progression, high scores | ✅ |
| Two-player split screen | ✅ |
| Network play | planned |

Next: [install OpenRF](./install).
