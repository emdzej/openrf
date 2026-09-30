# Internals

Return Fire on Windows is a port of the 3DO original: the renderer is a software emulation of
the 3DO's cel engine, drawing into an 8-bit DirectDraw surface, and the game logic runs in
16.16 fixed point on a variable timestep of whole 16 ms ticks. OpenRF keeps that structure.

<img src="/screenshots/turret.png" alt="" style="image-rendering: pixelated; border-radius: 6px">

| Document | Contents |
|---|---|
| [Portable core](/internals/portable-core) | App state machine, platform contract, file layer and disc images, audio mixer |
| [Engine architecture](/architecture) | Program flow, timing, input, sound and music, renderer overview, function index |
| [Renderer](/render) | Projection, floor walk, 3D model format, depth sort, draw functions |
| [Game simulation](/game) | Object system, collision, vehicles, weapons, AI, rules |
| [ART.CAR](/car) | Sprite bank (3DO Cel Control Blocks), palettes, TRANS.TBL |
| [RFM maps](/rfm) | Level map format, tile and object tables |
| [STM movies](/stm) | Pre-interleaved Cinepak + PCM movie container |

## Source layout

| Path | Contents |
|---|---|
| `src/` | Portable core: app state machine, file layer (`vfs.c`), audio mixer, asset loaders, movies, music, sound, HUD, front end; the SDL3 backend `platform_sdl.c` |
| `src/render/` | Cel rasteriser, camera, world renderer, 3D models |
| `src/game/` | Simulation: objects, collision, vehicles, weapons, effects, AI, rules |
| `tools/` | Format decoders, table generators, reference renderer |
| `tests/` | Headless tests against the original data |
