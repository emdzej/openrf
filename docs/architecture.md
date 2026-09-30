# Return Fire (Win95, 1996) — engine architecture map

Source: `cd/RFIRE.BIN` (PE32, image base 0x400000). Ghidra project `returnfire` / `rfire_game.exe`.
Fresh decompile with all current names: `re/all_named.c` (regenerate with `re/DumpAllNamed.java`);
`re/all.c` is the original un-named dump (825 functions).

> Note: during this pass 72 un-analysed code regions (main-loop state functions, object-class methods,
> span rasterisers, …) were turned into functions, so Ghidra now has **897 non-external functions**
> (825 in `re/all.c`). ~416 game/engine functions now carry meaningful names (table in §9).

## 1. Key facts (TL;DR)

| Topic | Finding |
|---|---|
| Origin | A **3DO port**: the renderer is a software emulation of 3DO cels (CCB lists, PLUTs, 3DO ScreenGroup double buffering) drawn into a locked 8-bit DirectDraw surface. |
| Display | 8-bit paletted. `DAT_0043fcf4` mode: 1/2/3 = fullscreen 640x480 / 320x240 / 320x200 (page Flip), 5/6/7 = windowed (system-memory surface + Blt). `DAT_0043fcf0` = hi-res flag. Game logic and layout are in **320x240 space**; hi-res = same art, rasteriser scales 2x. Only full-screen pictures have separate `*H`/`Hi*` versions. |
| Main loop | `WinMain` 0x40bc20: PeekMessage pump → call app-state function pointer `PTR_FUN_004410b8` → `PresentFrame(0)` 0x436ba0. Runs flat out; the only limiter is the vsync of `Flip` (fullscreen). `WaitMessage` only while paused/inactive. |
| Tick | `GetTicks16ms` 0x4279f0 = `timeGetTime()>>4` → **16 ms ticks (62.5 Hz)**. `FrameTimingUpdate` 0x433220 computes the per-frame delta `DAT_0045f3a8` = elapsed ticks (integer, via 16.16 table `DAT_00459720`), clamped to 12 ticks (`-f` option). **Variable timestep**, all motion multiplied by delta. |
| Units | 16.16 fixed point. Map 128x128 cells, 32 px per cell (`0x200000`); world 0..0x10000000. Heading full circle = `0x400000` (64 directions, matrix table 0x481780). |
| Entities | Pool of 512 objects (0x47261c), each with a class pointer (init/destroy/update/collide hooks). ~19 classes (names from class descriptors): Missle (shells/bullets/missiles), Vehicle, Turret Gun, FWall (debris), Shadow, Storage (bunker lift), Destroyed Vehicle, Drone, Mine, Expl, Flag, Gate, MAN, SUB, Death Missle, Stay (remains), Grenade, TRACER. Static map objects (91 "BNO_" types) live in the map cells. |
| Vehicles | Tank, Jeep, MSV (ASV), Heli — defs via 0x44afc8. Stock per team at 0x4438b8 (Tank 2, Jeep 3, MSV 2, Heli 1, 30 mines). Only the Jeep carries the flag. |
| Input | One 32-bit input word per player per frame (current + previous for edges). Defaults P1 WASD + H/J/K + Q/E, P2 numpad. Registry value "Keyboard and Joystick" (0x84 bytes). Deterministic record/replay (`-r`/`-w`). |
| 2 players | Side-by-side split: two 156x149 views at x=0 and x=164 (320 space), screen windows (8,12) / (-8,12), divider 2pMScr.rfa, status bar 2pBScr{L,H}.rfa with two HUD blocks. Stereo: left channel = P1 listener, right = P2. Ported: `play_run_2p` (src/play.c), `game_frame_2p`, docs/game.md §11. |
| Front-end | **Plain Win32**: menu bar + dialogs from the language DLL (`LANGS\RFIRExxx.DLL`, loaded as Lang.DLL): player names → level select (scans `Worlds\*\*.rfm`, per-user progress in registry). `CNFG*.BIN` are the external keyboard/joystick configurator **programs**, not data. |
| Sound | DirectSound 44.1 kHz 16-bit stereo; 37 SDT (= WAV) samples, 46 "sound events" with priority/decay; 20 voices / 15 logical sounds. One API: `Snd_QueueCommand(cmd,a,b,flush)` 0x42cb40, serviced each frame by `Snd_Service` 0x42cff0. |
| Music | 18-track table at 0x443aa0 = byte ranges of `SOUND\Score.WAV` (offsets relative to the `data` chunk), streamed on a thread via AVIStream → DirectSound. Chosen by a priority-based music director (0x41d730). Full table in §7. |
| Boilerplate share | Of the 825 original functions: **~14 % CRT + import thunks (118)**, **~19 % Win32/DirectX/VfW/registry/dialog code (155)**, **~67 % game-side (552)** — of which ~45 are the 3DO cel rasteriser/span routines and ~20 generic list/heap/timer helpers. So roughly **60 % real game logic+engine, 40 % OS/CRT glue**. (897-function count: 617 game-side / 162 platform / 118 CRT+thunks.) |

## 2. Module breakdown (address ranges are approximate; code is not strictly grouped)

| Module | Main addresses | Notes |
|---|---|---|
| Timers/callback lists | 0x401000–0x4011f0 | delta-sorted timer queue (`TimerQueueAdd/Advance`), per-frame callback list |
| Cel display list | 0x401220, 0x433620–0x4338c0, 0x4260a0–0x427700, 0x40fb30–0x411ba5 | 3DO CCB emulation: `Cel_AddToList`, `Cel_FlushList`, `Cel_DrawList` (0x13 draw modes → 8 span functions), quad/shadow rasterisers |
| Input | 0x401340–0x402b90, 0x42fc80–0x42fe40 | keyboard/joystick polling, bindings, registry config |
| Camera / view | 0x403350–0x404fb0, 0x419dd0 | `Camera_Update` 0x403a40, `View_ZoomInIntro` 0x404e80, `View_RenderWorld` 0x419dd0 |
| Object classes | 0x4061c0–0x4074xx (Man, Sub), 0x417f50 (Storage), 0x4204c0 (Expl), 0x4235a0 (Turret), 0x423cd0 (Gate), 0x424320 (Flag), 0x428210–0x42b3c0 (Vehicle + per-type physics), 0x4292d0 (Wreck), 0x430bb0 (Projectile), 0x4319c0 (Grenade), 0x434e40 (Mine), 0x435140 (Drone), 0x435da0 (Stay), 0x4344b0 (Debris) | each class = {init, destroy, update, …} table in .data |
| Object system / collision | 0x41d9xx–0x41ed80 | `ObjCreate` 0x41e240, `ObjUpdateAll` 0x41e970, cell linking, `ObjCheckCollision` 0x41dcf0 |
| 3D models | 0x4085a0–0x408d40 | `View_QueueModel`, `Model_*` — vehicles/buildings are textured-quad models, 64-step yaw/pitch matrices, depth-sorted |
| Art / palette | 0x408e80–0x4098b0, 0x435f20, 0x436890–0x436970 | `Car_Load` 0x4095f0 (ART.CAR), `Pal_InitTransTables` 0x4090c0 (TRANS.TBL), fades |
| App shell | 0x405740–0x406110, 0x40b710–0x40f3c0 | init, WndProc, menu commands, app-state functions, `StartNewGame` 0x40b710, `EndGame` 0x40f380 |
| DirectDraw | 0x407e60–0x408030, 0x40dd80, 0x433010, 0x436ba0 | init, mode set, lost-surface restore, `PresentFrame` |
| Sound FX | 0x411e20, 0x42b960–0x42d2e0 | DirectSound init, sample load, command queue/mixer |
| Music | 0x41d350–0x41d970, 0x42bc80–0x42c320, 0x4305d0–0x430a60 | director, request, AVIStream streaming, music thread |
| Front-end dialogs / registry / profiles | 0x412370–0x417390, 0x41abb0–0x41b750, 0x42e020–0x42ee80, 0x4328a0 | message boxes, New Game/level select/player names/high scores/about/sysinfo dialogs |
| Level load / game rules | 0x4320c0–0x4322f0, 0x41a330–0x41aa70, 0x42a480 | `LoadLevelMap`, `GameInit`, `GameSetup1P/2P`, frame functions, `JeepFlagCheck` |
| HUD / radar | 0x422640–0x422fb0, 0x437f90 | dirty-bit HUD gauges, 128x128 radar, status-bar background |
| Video / scripts | 0x424a10–0x425d20 (ICM/Cinepak), 0x436ee0–0x438100 | STM playback through VfW ICOpen/ICDecompress; intro/win "script" steps |
| Record/replay, high scores | 0x42d560–0x42d8b0, 0x42ea20 | input recording, game clock, `RecordHighScore` |
| Math | 0x438300–0x438580 | `FixMul`, atan/cos/sin wrappers |
| CRT | 0x438600–0x43ce70 | MSVC 4.x runtime (rand, atoi, heap, startup `entry` 0x438ab0, float formatting) |

## 3. Program flow

```
entry 0x438ab0 (CRT) → WinMain 0x40bc20
  LoadLanguageDll 0x42c7f0 (RFireXxx.DLL by locale) ; create music thread 0x430910 + semaphore
  single-instance check (FindWindow "ReturnFire")
  InitApplication 0x405740
     ParseCommandLine 0x405ac0  (-2 -a -c<dir> -f<maxdelta> -l<level> -m<map> -r/-w replay -z/-j)
     CreateMainWindow 0x405f30 ; HeapInit ; InitDirectDraw 0x407f30 ; sound init ; input init 0x42fc80
     GameInit 0x41a330 (palette, time-scale table, views, ART.CAR) ; intro script
  loop: PeekMessage/GetMessage/TranslateAccelerator/Dispatch
        (*PTR_FUN_004410b8)()   // app state
        PresentFrame(0) 0x436ba0 (+ UpdatePaletteFade 0x436970)
```

App states (`PTR_FUN_004410b8`), see §5 for details:

| Addr | Ghidra name | Role |
|---|---|---|
| 0x40eb70 | State_IntroPlaying | steps the intro script (emi.rfa → TWI.stm → Prolific.stm → Silent.rfa → RF.stm); a demo game runs behind it, then → Game1P/2P or queued state |
| 0x40ebd0 | State_Game1P | fade-in, status-bar redraw for 6 frames, then `GameFrame1P` 0x41a660 |
| 0x40ec50 | State_Game2P | same with `GameFrame2P` 0x41aa70 |
| 0x40ecf0 | State_StartGameFade | fade out 500 ms → `StartNewGame` 0x40b710 → Game1P/2P |
| 0x40ed60 | State_Idle | no-op (minimised) |
| 0x40ef00 | State_EnterBackScreen | draw `\Art\{Hi,Lo}Bck<Lang>.RFA` (or PS480/PS240), stop SFX, music track 17 (Drums), → BackScreen |
| 0x40ed70 | State_BackScreen | title/menu backdrop (WaitMessage); after a game: `RecordHighScore`, level-progress registry update |
| 0x40f050 | State_EndOfGameSequence | after `EndGame` 0x40f380: win music, banner `TITLE\Ban{B,G}{L,H}.bmp`, `TITLE\Win{1,2,3,}.stm` by level, → EnterBackScreen |

Note: the renderer section uses the alias `View_RenderWorld` for 0x419dd0; its Ghidra name is `RenderWorldView`.

### Per-frame game update (`GameFrame1P` 0x41a660)
(`GameFrame2P` 0x41aa70 polls the input after step 5 instead, runs both view modes in step 6 and marks both HUD
blocks; `State_Game2P` 0x40ec50 is `State_Game1P` with `GameFrame2P` and both HUDs.)
1. poll input (`PollAllPlayerInputs` 0x42fd30 via ptr 0x44f2e0)
2. music step 0x41d350; bunker-door flag reset 0x427fe0
3. `TimerQueueAdvance(delta)` 0x401090
4. `ObjUpdateAll` 0x41e970 (class->update for every active object; reap delete queue)
5. `RadarUpdateMovers` 0x422fb0; water/palette cycling 0x419b50
6. `view->mode(view)` at view+0xc0 (normally `RenderWorldView` 0x419dd0; 2P: both views)
7. `Cel_FlushList(0x48c260)` + callback list 0x4011a0; `HudUpdateAll(nPlayers)` 0x422750; `Cel_FlushList(0)`
8. `Mus_Director` 0x41d730
9. `Snd_QueueCommand(2,0,0,1)` + `Snd_Service` 0x42cff0
10. `FrameTimingUpdate` 0x433220 (toggle screen context 0x471580/0x4715a4, next delta)
Then WinMain calls `PresentFrame`.

## 4. Recommended reimplementation order

1. **Platform shell** (SDL2/Metal or plain Cocoa+CoreAudio): 320x240 8-bit framebuffer + palette → RGBA upload, integer scaling; 16 ms tick clock with the same delta rule (elapsed ticks, cap 12); input word per player with default bindings.
2. **Asset loaders**: RFA/BMP, ART.CAR (cels + PLUTs, from the ART.CAR agent), TRANS.TBL (or regenerate it from the ART.CAR palette as the game does), RFM → 128x128 cell grid via tile table 0x452858 and BNO table 0x451438.
3. **Cel rasteriser**: CCB semantics (0x44-byte cel, draw modes 0..0x12, shading/blend via TRANS tables). Test by drawing the static map top-down.
4. **World view**: perspective floor rows (1/z table, focal ~300), queued depth-sorted 3D models, shadows, camera follow, 1P/2P viewports. Use a static map as the visual test.
5. **Object system**: 512-slot pool, class tables, cell linking, collision (`ObjCheckCollision`), delta-list timers, `RandRange`.
6. **Vehicles**: Tank first (def table 0x44afc8, physics fn at def+0x14), then Jeep (flag carrying), Heli, MSV; bunker/lift select (Storage), fuel/ammo, drones for idle vehicles.
7. **Weapons + effects**: Projectile (shell/bullet/missile), Grenade, Mine, Expl, Debris, Wreck, Tracer.
8. **Enemies/world**: Turret Gun, MAN (soldiers), Gate, SUB, Flag + win rule (`JeepFlagCheck` 0x42a480 → `EndGame` 0x40f380).
9. **HUD + radar**, status bar backgrounds.
10. **Sound FX** (event table 0x43e978, priority mixer, two-listener stereo), then **music** (Score.WAV ranges + director).
11. **Front-end**: replace Win32 menus/dialogs with an in-app menu (player names, level select from `Worlds\*`, options, high scores `RFire_HS` format), intro/win STM videos (needs the STM/Cinepak decoder from the video agent).
12. 2-player split screen, record/replay (useful as a regression test harness: deterministic given input + deltas).


## 5. Platform, front-end and input


All addresses are VAs in `RFIRE.BIN` (Ghidra program `rfire_game.exe`). Every function in the table below has been renamed in Ghidra.
Resource IDs (strings, menus, dialogs, accelerators) are in the **language DLL**, not in RFIRE.BIN. The DLL is loaded as `Lang.DLL` from the cwd (`LoadLanguageDll` builds a `\Langs\RFire<Lng>.DLL` path from the thread locale but then ignores it). It also sets `DAT_00470f18` = language index 0 Eng, 1 Frn, 2 Ger, 3 Itl, 4 Spn. That index selects entries in the per-language filename tables (stride 0x20): `\Langs\CnfgXxx.Bin` @0x44c008, `\Art\HiBckXxx.RFA` @0x44c0a8, `\Art\LoBckXxx.RFA` @0x44c148 and `\Langs\RFireXxx.hlp` @0x44c1e8. English strings were dumped from `cd/LANGS/RFIREENG.DLL` into `/tmp/rfa/strings_eng.txt` with `/tmp/rfa/resstr.py`, which also dumps the `acc` and `menu` resources.

### 1. Startup / shutdown
`entry` (CRT) calls `WinMain(0x40bc20)`, which runs these steps:
1. `LoadLanguageDll`.
2. Create the music semaphore and the music thread `FUN_00430910` (sound fork), plus a critical section.
3. If `FindWindowA("ReturnFire")` finds an existing instance, focus it and exit (single instance).
4. `InitApplication(0x405740)`:
   - `ParseCommandLine`, `LoadAcceleratorsA(0x48b)`, `CreateMainWindow` and `HeapInit`.
   - Register the levels in the registry (`FUN_00412d00`).
   - `InitDirectDraw`.
   - Fail if a DirectDraw critical error flag is set (`FUN_004367f0`), otherwise `InitialFadeAndPresent`.
   - `LoadWinBanners` (preloads TITLE\Ban*.bmp) and `FUN_00418ae0` (object pools).
   - Check that SCORE.WAV exists (`FUN_00430610`), then `InitInputDevices(3)`.
   - `FUN_0041a330`: first game-world init (sound/entity forks).
   - Set up recording/replay, then seed the RNG (`FUN_00438600(GetTickCount)`).
   - Continue with `FUN_0041d1f0` and `FUN_00427a00`.
   - Unless in hidden-launch mode, `InitSoundAndGameWindows`: DirectSound init `FUN_00411e20`, SFX load `FUN_0042cbc0`, then 1P/2P window setup `FUN_0041a480` / `FUN_0041a750`.
   - `RegCreateDefaultConfig`, then `RegReadDisplayType` → `ApplyDisplayModeSetting`, then `RegReadCDMusic` → `SetMusicEnabled`.
5. If `DAT_0043fc88` is set (static 1, so the intro always plays): disable menus, set state=`State_IntroPlaying` and `StartIntroScript`.
6. **Main loop.** Drain the messages first (`PeekMessage`/`GetMessage`, `TranslateAccelerator`, `Dispatch`). Then call `(*PTR_FUN_004410b8)()` (the current app state), then `PresentFrame(0)`. If paused (`0x43fca0 & 2`), also call `WaitMessage()`.
   - On WM_QUIT it frees resources, `HeapShutdown`, and `FreeLibrary` of the language DLL.
   - WM_DESTROY (`MainWndProc` case 2): stop the music thread, free video/sound/art, `Sleep(250)`, `PostQuitMessage`.
   - WM_CLOSE: stop music, save the input recording if `-w`, then `DestroyWindow`.

#### Command line (`ParseCommandLine` 0x405ac0)
| opt | effect |
|---|---|
| `-2` | 2-player game (`DAT_00443864`=2; default 1) |
| `-a` | `DAT_0043f3a8`=1 (unknown debug flag) |
| `-c<dir>` | SetCurrentDirectory / data root (`DAT_0043fd10`) |
| `-f<n>` | max frame delta in 16 ms ticks (`DAT_004438f8`, default 12, clamped to 30) |
| `-jh`/`-jl`/`-jx`, `-z` | hidden launch (`0x43fca0 \|= 4`): window hidden, mode 7. The window waits for WM_COMMAND 0x1e61 (from the RFIRE.EXE launcher), which restores `DAT_0043fcf8` and does the sound and window init. |
| `-l<n>` | start level number (`DAT_00443868`, 0-based, clamped to 8) |
| `-m<file>` / bare arg | world (.rfm) file (`DAT_00443860`) |
| `-r<file>` | **replay** input recording (all `ReadPlayerInput`/joystick results come from the file) |
| `-w<file>` | **record** input (dword stream, saved on exit) |

The record/replay format is a dword count followed by a dword stream. It contains every input word and joystick-API result in call order, and also the RNG seed (`GetTickCount` goes through it). A recording is therefore a deterministic replay: this proves game logic is deterministic given the inputs, the seed and the frame deltas. Note that the frame delta comes from `timeGetTime` and is **not** recorded, so exact replay also depends on timing.

### 2. App state machine (`PTR_FUN_004410b8`, called once per loop iteration)
| addr | name | role |
|---|---|---|
| 0x40eb70 | State_IntroPlaying | Steps the intro script (`RunScriptStep`). When `DAT_00455990==0`, sets fade vars and goes to Game1P/Game2P (the demo game runs behind the back-screen), or to a queued state. |
| 0x40ebd0 | State_Game1P | Not paused: fade in to 0x10000 over 500 ms, redraw the status bar (`DrawStatusBarBackground`) for 6 frames, music ping, then the game tick `FUN_0041a660`. |
| 0x40ec50 | State_Game2P | Same, with the 2-player tick `FUN_0041aa70`. |
| 0x40ecf0 | State_StartGameFade | Fade out 500 ms. When black: `SetupGame`, `SetupViewports(1)`, fade in, go to Game1P/2P. |
| 0x40ed60 | State_Idle | Empty (used while minimized). |
| 0x40ef00 | State_EnterBackScreen | Draws the language back-screen `\Art\{Hi,Lo}BckXxx.RFA` (or `ART\PS480/PS240.RFA` in windowed pause), stops game sound, starts music track 0x11 (Drums, prio 0x32), then goes to `State_BackScreen`. |
| 0x40ed70 | State_BackScreen | Title/menu backdrop; waits with `WaitMessage`. After a finished game (`DAT_004410d0`): `RecordHighScore(winner)` and the level-progress registry update (`FUN_00416330`, `FUN_00415330`). May auto-post "New Game" (0x1e64). A left click in the bottom 1/16 of the client area also posts New Game. |
| 0x40f050 | State_EndOfGameSequence | Sub-state `DAT_004410c0`, see below. |

End of game: `BeginEndOfGame(winner)` 0x40f380 stops the game clock, sets winner `DAT_00457100` (0/1 = player/team, other = draw/none) and `DAT_004410d0`=1, fades out over 1000 ms, then enters `State_EndOfGameSequence`:
1. Case 0: if there is a winner, `FUN_00430b30` (win music) and `SetWinnerAndWinVideo(winner, level)`, which picks `TITLE\Win{1,2,3,}.stm` from table 0x4559e0 by level (0,1,1,1,1,2,2,2,3,3). With no winner, skip to case 6.
2. Case 1: show the winner banner `TITLE\Ban{B,G}{L,H}.bmp` (Blue/Green × Lo/Hi res).
3. Case 2: fade in.
4. Case 3: wait for music.
5. Case 4: `StartWinScript`.
6. Case 5: run the script.
7. Case 6: fade out.
8. Default: go to `State_EnterBackScreen` and enable the menus (`DAT_004410a8`=2).

`DAT_004410a8` app phase: 0 = init, 1 = intro / end sequence (menus greyed), 2 = ready.

#### Intro/win "scripts" (`RunScriptStep` 0x437d80)
Each entry is 0x14 bytes: `{fn(entry, frameIdx, timeGetTime) -> done?, arg (file), holdMs, fadeInMs, fadeOutMs}`. `PTR_PTR_00455b3c` points at the current entry and advances when fn returns nonzero.
- Intro @0x455a38: FadeOut → `TITLE\emi.rfa` (hold 1000) → `TITLE\TWI.stm` → `TITLE\Prolific.stm` → `TITLE\Silent.rfa` (hold 2500) → `TITLE\RF.stm` → End.
- Skip @0x455ad8: End. Any WM_KEYDOWN or mouse button calls `SkipIntroScript` 0x437e00.
- Win @0x455b00: `Script_WinBannerAndVideo` → End.

The step functions are `Script_FadeOut` 0x437860, `Script_ShowImage` 0x4378e0 (RFA via `ShowFullscreenImage`), `Script_PlayVideo` 0x437a70 (STM: `FUN_00424b70` open / `FUN_00425a40` start / `FUN_00425d20` step / `FUN_00424a10` close, Cinepak via ICOpen) and `Script_End` 0x437be0.

### 3. Display (DirectDraw)
`DAT_0043fcf4` = display mode:

| mode | meaning | size |
|---|---|---|
| 1 | fullscreen exclusive | 640x480x8 |
| 2 | fullscreen exclusive | 320x240x8 |
| 3 | fullscreen | 320x200 (viewport height 200; no menu item, hidden-launch/legacy) |
| 5/6/7 | windowed versions of 1/2/3 | - |
| 4 | invalid | - |

- `DAT_0043fcf0` = hi-res flag.
- `DAT_0043fce8`/`DAT_0043fcec` = logical width/height (0x140×0xf0 or 0x280×0x1e0).
- Default mode is 6 from the command-line init. The registry "Display Type" (1 = 640, 2 = 320) and "Full Screen" values then choose the mode via `ApplyDisplayModeSetting`.
- `InitDirectDraw` 0x407f30 calls `DirectDrawCreate` and GetDisplayMode. If the desktop is 8 bpp, `DAT_0043fcb4`=1 (windowed allowed). Otherwise it shows a message box and forces fullscreen (5→1, 6→2, 7→3).
- `SetDisplayMode` 0x408030:
  - FS: SetCooperativeLevel(0x51), SetDisplayMode(w,h,8), a primary surface with 1 back buffer (caps 0x218, flip chain), GetAttachedSurface → back.
  - Windowed: cooperative level NORMAL, a primary plus a **640x480 system-memory offscreen** "back" surface (caps 0x840) and a clipper.
  - Always calls `UpdatePaletteFade(1)`, then `FUN_0040e060` (redraw).
  - Globals: DD `DAT_0043fcbc`, primary `DAT_0043fcc0`, back `DAT_0043fcc4`, clipper `DAT_0043fccc`, palette `DAT_00459fd0`.
- `PresentFrame(force)` 0x436ba0 only presents when the dirty flag `DAT_004815e8` is set.
  - FS: `Flip(0, DDFLIP_WAIT)`, which is vsync-limited. If `DAT_004815ec` is set, it does a BltFast of the back surface to the primary instead.
  - Windowed: `Blt` of the back surface to the client rectangle (stretched).
  - Handles DDERR_SURFACELOST via `RestoreLostSurfaces`. Increments the frame counter `DAT_00481608`, then calls `UpdatePaletteFade`.
- **Palette fade:** `SetFadeTarget(level 0..0x10000, ms)` 0x436890. `UpdatePaletteFade` interpolates brightness `DAT_00455998` linearly in time. It applies the fade **by subtraction**: each RGB byte becomes `max(0, c - ((0x10000-b)>>8))`, not multiplied. The master palette is at `DAT_0045ab64` (256 × {r,g,b,flags}).
  - FS: sets all 256 entries; entries 0..10 are forced to black unless the intro is playing.
  - Windowed: sets only entries 10..245 to preserve the system colours.
  - `ShowFullscreenImage(rfa, force)` 0x436fb0 loads an RFA image, copies its palette into the pending palette `DAT_0045a644`, and blits or stretches it onto the back buffer.
- **Pause** (`TogglePause` 0x436030, F3 / menu 0x406):
  - Sets flag `0x43fca0 |= 2`, saves the player "inputs-active" values `DAT_0048be70` and `DAT_0048c0bc`, and pauses the game clock.
  - On resume it adds the paused duration to the frame clock (`DAT_0045f394`/`DAT_0045f3b0`) so the frame delta does not jump.
- Flags in `DAT_0043fca0`: 1 = menu loop active, 2 = paused, 4 = hidden launch, 8 = app inactive (WM_ACTIVATEAPP; this auto-pauses), 0x10 = user/minimize pause, 0x20 = modal dialog open.

### 4. Timing (confirmed)
- `GetTicks16ms` 0x4279f0 returns `timeGetTime()>>4`, i.e. a 16.0 ms tick (62.5 Hz).
- `FrameTiming` 0x433220 runs once per game frame, at the end of `FUN_0041a660`/`FUN_0041aa70`:
  - `dt = min(now - last, DAT_0045468c)`, where `DAT_0045468c` = 12 from the default `-f12`.
  - `delta = (acc + table[dt]) >> 16`, with `table[i] = i*60*65536/60 = i<<16` (`InitFrameTimingTable` 0x4332d0 builds the table; `DAT_00454688`=`DAT_00454690`=60). So `DAT_0045f3a8` = elapsed 16 ms ticks, integer, 0..12.
  - It also accumulates game time `DAT_0045f390` and flips the double-buffer target descriptor `DAT_0045f3ac` between 0x471580 and 0x4715a4.
- **Variable timestep, no fixed tick, no frame limiter** other than vsync on Flip. The game logic multiplies by `DAT_0045f3a8`, and a frame with dt=0 does nothing time-based.
- Timers:
  - `TimerAdd(delay, fn, a, b)` 0x401000 inserts into a delta-sorted list at `DAT_004568b0`.
  - `TimerTick(dt)` 0x401090 fires the expired timers. A callback's return value r means: r<0 → free, r>0 → new period, r==0 → reuse the old period.
  - `RunFrameTasks` 0x4011a0 runs a per-frame task list at `PTR_DAT_0043ee90`; a callback that returns nonzero is removed.
- The game clock for high scores is in ms (`GameClockStart/Stop/Pause/Resume` 0x42d840..0x42d890; result in `DAT_0044c4e0`).

### 5. Input
`PollAllPlayerInputs` 0x42fd30 is called at the start of every game frame through `PTR_thunk_FUN_0042fd30_0044f2e0`. For each player i: `prev[i]=cur[i]; cur[i]=ReadPlayerInput(...)`. Current inputs are at `DAT_0046f5dc`, previous at `DAT_0046f5d4` (dword per player), and the game detects edges from the two. `ReadPlayerInput` 0x401340 ORs keyboard (`GetKeyboardState` → `ReadKeyboardBindings`) and joystick (`joyGetPosEx` → `JoyInputActive(code)`) results. The physical device index is swapped when "Swap Sides" (`DAT_0043fc9c`, Alt+3) is set. Any action bit (0xfc000000) refreshes the idle timer `DAT_0043ee9c` (screensaver suppression).

Input word bits:

| bit | P1 default key | P2 default key | struct slot |
|---|---|---|---|
| 0x40000000 | W | Num8 | +0x48 (up) |
| 0x80000000 | S | Num5 | +0x4c (down) |
| 0x10000000 | A | Num4 | +0x50 (left) |
| 0x20000000 | D | Num6 | +0x54 (right) |
| 0x08000000 | H | Num- | +0x2c (button 1) |
| 0x04000000 | J | Num+ | +0x30 (button 2) |
| 0x02000000 | K | Enter | +0x34 (button 3) |
| 0x00200000 | Q | Num7 | +0x38 (button 4, e.g. rotate/strafe left) |
| 0x00400000 | E | Num9 | +0x3c (button 5) |
| 0x01000000 / 0x00800000 | unbound | unbound | +0x40 / +0x44 |

Details of the input word:
- Left/right sets low byte 0xff and up/down sets byte1 0xff: digital "full deflection" analog magnitudes.
- Shift+Ctrl (P1) or Insert+Delete (P2) forces 0x0e000000 (buttons 1+2+3 together; meaning is up to the game-logic fork).
- French (`DAT_00470f18`=1) uses AZERTY defaults (Z/Q/A in place of W/A/Q).
- Numpad VKs are remapped to navigation VKs when NumLock is off (`NumpadToNavKey`).
- Swap Sides (Alt+3, menu 0x415, `DAT_0043fc9c`, 2 players only): the two physical devices change players.
  Port: `src/input.c` reads P1 (WASD/HJK/QE, plus the arrows) and P2 (numpad) every frame; Alt+3 in the 2-player
  loop swaps the two words before `game_frame_2p`.

Binding layout:
- The bindings are one 0x84-byte REG_BINARY "Keyboard and Joystick" under `HKCU\Software\Silent Software, Inc.\Return Fire\1.0\Configuration`, loaded by `RegLoadInputConfig` 0x4022c0.
- The file holds 66 u16 values: 1P keyboard layout in words 0..10, 2P layout in 11..32, joystick codes after that.
- Defaults come from `DefaultKeyBindings` 0x402440 and `DefaultJoyBindings` 0x402530. `LoadKeyboardConfig` / `LoadJoystickConfig` unpack them into per-player structs at 0x48c7e0 + p*0x94.
- Key VKs sit at +0x2c..+0x54 and joystick codes at +0x58..+0x80.
- Joystick codes (`JoyInputActive`): 0..3 = J1 axes ±, 4..7 = J1 buttons 1-4, 8..11 = J1 POV down/left/right/up, 12..23 = the same for J2. The axis threshold is 1/4 of the range from each end.
- The bindings are edited by an **external configurator**: the CONFIG.EXE path from `HKLM\...\1.0\Patches`, falling back to `\Langs\CnfgXxx.Bin`. `CNFG*.BIN` are simply that configurator (a PE executable, launched with CreateProcess from the Config dialog button 0x3f2). Afterwards `ReinitInputDevices` is called.

### 6. Front-end = Win32 menus and dialogs (no in-game menu system)
Menu 101: see the table below. Accelerators 0x48b: F2 new game, F3 pause, F4 / Alt+Enter full screen, F6 restart, F7 high scores, Esc exit, Alt+1 640x480, Alt+2 320x240, Alt+3 swap sides, Ctrl+M music, Ctrl+C config, Ctrl+S 0x414 (unlabelled toggle), Alt+F1 help, Shift+F1 system info, Ctrl+F1 about, Alt+0 0x40b (re-layout viewports and redraw).

| cmd | action |
|---|---|
| 0x405, 0x1e64 | New game: `NewGameDialogAndStart` → `NewGameDialogs` |
| 0x407 | Restart (`State_StartGameFade`) |
| 0x406 | Pause |
| 0x403 | High Scores |
| 0x402 | Exit (confirm 0x4f6) |
| 0x40c/0x40d | Toggle fullscreen |
| 0x413 / 0x412 | 640x480 / 320x240 |
| 0x411 | Music toggle |
| 0x415 | Swap sides (swaps names `DAT_0048bb20` ↔ `DAT_0048bb49`) |
| 0x40e | Config dialog 0x47c (`ConfigDlgProc`: display type, full screen, CD music, launch configurator) |
| 0x409 | WinHelp |
| 0x40a | SysInfo dialog 0x67 |
| 0x408 | About 0x68 ("Ver: G507 1.0.6 Win95 x86") |

Modal dialogs from fullscreen temporarily switch to windowed (1→5, 2→6) and back.

`NewGameDialogs` 0x412c40:
- Dialog 0x3f5 `PlayerNamesDlgProc`, player profiles. Names default to `WNetGetUserA`; profiles are stored per user under `...\1.0\Levels\<user>` with "Active"/"Numb" values.
- Then dialog 0x3f4 `LevelSelectDlgProc` (ImageList map previews via `FUN_00414110`).
- The level list is built by scanning `<InstallPath>\Worlds\{1Player,2Player}\*\*.rfm` (`RegScanWorldDir*`, `FindWorldsInInstallPath`). Registry values are 0x20-byte records per map, protected by a CRC-16 (poly 0x8408 reflected, init 0xffff) over 0x1c bytes; this tracks level completion/unlock.
- On OK it copies the map path (`DAT_0048bb72` → `DAT_0048b1a0`), sets the level `DAT_00443868`, and switches to `State_StartGameFade`.

`SetupGame` 0x40b710:
- Frees all objects, then calls `LoadWorldFile(nplayers)` 0x4322f0: chunked RFM with 'WRL' magic and chunks LEVL/NAME/VHCL. The default maps are "Worlds\1Player\Level1\The Cakewalk.rfm" and "...2Player\Level1\Driving School.rfm".
- Then `InitInputDevices`, `LoadStatusBarArt` (`ART\{1p,2p}BScr{L,H}.rfa`, `ART\2pMScr.rfa`), world init `FUN_0041a330`, and 1P/2P window setup. Finally `GameClockStart`.

`SetupViewports` 0x40e470 fills the player view structs at 0x48bd90 / 0x48bfdc:
- 1P: 320×(240 or 200) with the camera centre at (160,120).
- 2P: **side-by-side vertical split**, P1 x=0..155 and P2 x=164..319 (0x9c wide, 8 px gap), each 240 tall.
- With param=1 (in-game with the status bar) it uses `FUN_0040e890` instead, with heights 0x98 (1P) or 0x95 (2P).

High scores:
- The file path comes from `HKLM\...\1.0\High Scores` (default `C:\WINDOWS\RFire_HS.XXX`).
- Obfuscation: each byte is stored as `(plain ^ 0x5a) + key[i&7]` with key "retufire"; decrypt with `c = (c - key[i&7]) ^ 0x5a`.
- Header (0x1c bytes): +0 u32 hdrsize=0x1c, +4 "rfhs\0", +0xc u32 n1P, +0x10 u32 rec1P=0x48, +0x14 u32 n2P, +0x18 u32 rec2P=0x50.
- 1P record: u16 level (1-based, 0x7f = custom map), char map[33], char player[33], u32 time_ms. It keeps the best (lowest) time per (level, map).
- 2P record: name1[33], name2[33] (sorted), u32 p1wins, p2wins, draws.
- Written by `RecordHighScore` 0x42d8b0, shown by `HighScoresDlgProc` 0x42ea20 (dialog 0x482).

Registry summary: under `HKCU\Software\Silent Software, Inc.\Return Fire\1.0\Configuration`: "Display Type" (1 = 640, 2 = 320), "Full Screen", "CD Music", "Keyboard and Joystick", plus "Levels" subkeys. `HKLM ...\1.0` holds "Installed Path", "High Scores" and "Patches\CONFIG.EXE".

### 7. Out of scope (noted)
The data tables with pointers to 0x408840 / 0x408a20 / 0x408aa0 / 0x408d40 are **not UI widgets**. They are 3D/part-render descriptors: 3x3 fixed-point transforms at 0x481780 (stride 9 dwords), a render callback, and a child pointer. They belong to the renderer and entity forks.

### Key globals
| addr | meaning |
|---|---|
| 0043fc6c / 0043fcd8 | main HWND |
| 00470f1c | language DLL HMODULE |
| 00470f18 | language index |
| 0043fcf4 | display mode (1..3 FS, 5..7 windowed) |
| 0043fcf8 | saved mode for hidden launch |
| 0043fcf0 | hi-res flag |
| 0043fce8 / 0043fcec | screen width/height |
| 0043fcb4 | windowed possible (desktop is 8 bpp) |
| 0043fca0 | pause/activity flags |
| 004410b8 | current app-state function |
| 004410c0 | end-sequence sub-state |
| 004410a8 | app phase |
| 004410d0 | game-finished pending flag |
| 00455990 | intro/script running |
| 00455b3c | script pointer |
| 00443864 | number of players |
| 00443868 | level index |
| 00443860 | world file path |
| 0048bb20 / 0048bb49 | player 1/2 names |
| 0048bd90 / 0048bfdc | player 1/2 view/player structs (0x24c each) |
| 0048be50 / 0048c09c | per-player update function pointer |
| 004815e8 | frame dirty (present needed) |
| 00481608 | presented frame counter |
| 0045f3a8 | frame delta (16 ms ticks) |
| 0045f390 | accumulated game ticks |
| 0046f5dc / 0046f5d4 | current/previous input words per player |
| 0043fc9c | swap sides |
| 0048ca80 / 0048ca84 | joystick 1/2 present |
| 0048ca88 | keyboard enabled |
| 0043fd08 / 0043fd0c | recording / replay active |
| 00455998 / 0045599c | current/target fade brightness (0..0x10000) |
| 0045ab64 | master palette |


## 6. Game world, entities and game logic


### Units and conventions
- **Fixed point:** positions, speeds and most tunables are 16.16. `FixMul` 0x438300 is `(a*b)>>16` using a 64-bit intermediate.
- **World size:** the map is 128x128 cells and each cell is 32 px. A cell is `0x200000` in 16.16. World coordinates run from 0 to `0x10000000` (4096 px). A cell centre is `cell*0x200000 + 0x100000`.
- **Heading units:** object headings (`obj+0x4c`) are masked with `0x3fffff`, so a full circle is `0x400000`. That is 64 integer "directions" in 16.16. The rotation-matrix table is at 0x481780: 64 entries of 3x3 ints (9 dwords), indexed by `heading>>16`.
  - `AngleBetweenPoints` 0x407c00 returns `(atan*256/2π)>>2 - 0x100000`, i.e. heading units with 0 pointing "up".
  - `CosFixed`/`SinFixed` (0x438360/0x438520) take a different unit: a full circle is `0x1000000`, i.e. 256 steps. The constant is `1/65536 * 2π/256`.
- **Time:**
  - The global tick is `DAT_0045f390`, advanced each frame in `FrameTimingUpdate` (0x433220).
  - The per-frame delta is `DAT_0045f3a8`, in ticks. One tick is `timeGetTime()>>4`, i.e. 16 ms (62.5 Hz). The delta is clamped to 12 ticks (`-f` option, default 12).
  - All motion scales by `DAT_0045f3a8`. The simulation is variable-timestep with integer ticks.
- **Random:** `RandRange(n)` 0x4335f0 is `((rand()&0x7fff)*2*n)>>16`, giving 0..n-1 using the MSVC `rand`.

### Per-frame game update (`GameFrame1P` 0x41a660; the 2P version 0x41aa70 is the same with two players)
1. Poll input: indirect call through `PTR_0044f2e0`, which resolves to 0x42fd30. It fills the current and previous input words (`DAT_0046f5dc`/`DAT_0046f5d4`) per player from 0x401340.
2. `FUN_0040aeb0` runs if the debug flag `DAT_0044387c` is set. Then the music director step `FUN_0041d350` runs, followed by `FUN_00427fe0`, which resets the bunker-door cell flags.
3. `TimerQueueAdvance(delta)` 0x401090 runs the delta-list of timed callbacks at `DAT_004568b0`. Entries are added by `TimerQueueAdd` 0x401000.
4. **`ObjUpdateAll` 0x41e970:** walks the active list `DAT_00443de0` and calls `class->update(class,obj)` (class+0x10). It then reaps the delete queue `DAT_00480e34` (`class->destroy`, unlink cell and children, return to the free list 0x443dd0).
5. `RadarUpdateMovers` 0x422fb0 draws moving objects into the 128x128 radar bitmap `DAT_00440c64`.
6. `FUN_00419b50` animates water and palette cycling. It uses the remap table `DAT_00481620` (entries 0xc3..0xc6).
7. View render through a function pointer: `view->mode(view)` at `0x48be50` (`0x48bd90+0xc0`). The 2P frame also calls `0x48c09c` for the second view. The normal mode is `RenderWorldView` 0x419dd0 (renderer fork).
8. `RunCallbackList` 0x4011a0 (list `PTR_0043ee90`). Then `HudUpdateAll(nPlayers)` 0x422750, which runs dirty-bit HUD gauges from a 0x108-byte HUD block per player at 0x472060 (+0x108 for P2).
9. `FUN_0041d730` is the music director (vehicle/flag/win cues). Then sound `FUN_0042cb40(2,...)`, `FUN_0042cff0`, and `FrameTimingUpdate`, which flips the double-buffer index `DAT_0045f3a0` and computes the next delta.

`DAT_0043fcdc` = "game running". The frame aborts when it is cleared.

### Game start and init
- **`StartNewGame` 0x40b710:**
  1. Frees all objects.
  2. Calls `LoadLevelMap` 0x4322f0.
  3. Sets `DAT_0048110c` to the loadout at 0x4438b8.
  4. Calls `GameInit` 0x41a330.
  5. Calls `GameSetup1P` 0x41a480 or `GameSetup2P` 0x41a750.
- **`GameInit`:**
  - Sets the palette, time scale (`InitTimeScaleTable` 0x4332d0), etc.
  - Clears the view structs at 0x48bd90, 0x48bfdc (stride 0x24c) and 0x48c260.
  - Calls `ViewInit(0x48c260,...)`.
  - Links views to teams: `0x48bdf8 = 0x480f50` (team 0) and `0x48c044 = 0x481020` (team 1).
  - Loads ART.CAR via `FUN_004095f0`.
- **`GameSetup1P`:**
  - Creates the screen region (`FUN_004333f0`) and calls `ViewInit(&view0, 0x480eb0, x,y,w,h)`. The 1P view is 320x152 in low-res, or 0x165x0xa9 at offset 13 in hi-res.
  - Team 0 gets view ptr and input ptrs. The loadout is copied into team+0xac.
  - Calls `HudInit` 0x422640 and `ViewEnterBunkerSelect` 0x404d30; the game starts in the bunker vehicle-select.

### Level load: `LoadLevelMap` 0x4322f0 (file format owned by the RFM agent)
- The file is read by `FUN_00433620` into a temporary buffer. Header checks: magic at +0, `+0x16` = player count, `+8/+0xa` = map width/height (u16), `+0x48` = offset of the tile grid. Tagged chunks between 0x50 and `+0x48` are 4-char id plus a length dword at +4; tags are at 0x452c2c, 0x44128c, 0x452c24.
- The map is centred in the 128x128 grid **`DAT_0045f3c0`** (`uint32 cell[128][128]`, row-major, `cell = y*128+x`).
- Each tile byte `t` (values ≥0xf0 are forced to 0) indexes the **tile table at 0x452858** (4 bytes per id):

  | Byte | Meaning |
  |---|---|
  | [0] | terrain id, bits 0-6 (0xff = none) |
  | [1] | static-object (BNO) type to place |
  | [2] | side (0/1 = team, 2 = neutral) |
  | [3] | index into the special-spawn function table 0x452c18 (`[1]=0x431fb0`, `[2]=0x431f60`) |

  - Tile ids 0xa0-0xc5 are team-0 structures; 0xc8-0xed are the same structures for team 1.
- Post-pass:
  - Bridge spans between marker terrains 0x54..0x55 (horizontal) and 0x56..0x57 (vertical) are filled with BNO 0x4a/0x4b.
  - Flag buildings are collected into lists at 0x471840 and 0x471c40, with counts in `DAT_00472054`/`DAT_00472050`. One is picked at random as the flag holder (`DAT_0047181c`/`DAT_00471818` per team).
  - Bunkers are marked on the radar.
- **Map cell dword layout:**

  | Bits | Meaning |
  |---|---|
  | 0-6 | terrain type (1 = water, 2 = water variant, 4..0x17 = shoreline pieces, <0x34 = water-ish; see `GetObjWaterState` 0x4185e0) |
  | 7-13 | BNO static-object type |
  | 14-15 | side / orientation |
  | 16-24 | index of the first dynamic object in this cell (object pool index) |
  | 25-27 | static-object hit points |
  | bit 31 | radar flag |

- `PTR_00452d40` is the "outside the map" pseudo-cell.

### Static map objects (BNO table at 0x451438, stride 0x38, 91 entries, names like "BNO_BUSH_1S")
- **Fields:**

  | Offset | Meaning |
  |---|---|
  | +0 | id |
  | +4 | name |
  | +8 | graphic/shape descriptor |
  | +0xc | flags. Bits 1/2 give random terrain variants. Bit 4 offsets the explosion. Bits 16-19/20-23 are the min/max men spawned when destroyed. Bit 0x8000 means freed men join the other side (prisons). |
  | +0x10 | byte0 = terrain override, byte1 = hit points |
  | +0x14 | on-hit function (bushes 0x41f290, rocks 0x41f260, ammo/fuel/gate 0x41f340) |
  | +0x20 | init/destroy hook (walls `BnoDestroy` 0x417a00, towers `TowerDestroyed` 0x423510, flag building `FlagBuildingDestroyed` 0x424170, bridge 0x41f9a0) |
  | +0x2c | explosion descriptor |
  | +0x30 | byte = terrain after destruction |
  | +0x31 | destroyed-replacement BNO type |
  | +0x34 | radar colours (lo16 side 0, hi16 side 1) |

- **Kinds:** bushes, palms, trees, cactus, rocks, planters, ammo dump, prison, watchtowers, flag, buildings/factories, bunkers, outpost, hospitals, fuel dumps, gates, walls (with damage states), tents, gun towers (tower / large / ready / active / damaged / destroyed), bridges, storage light.
- `SetCellStaticObject` 0x417850 writes a BNO into a cell. `BnoDestroy` 0x417a00 spawns the explosion, swaps in the destroyed type and updates the radar (`RadarUpdateCell` 0x422930).
- `SpawnMenFromBuilding` 0x4071f0 spawns soldiers when buildings are destroyed.

### Dynamic object system
- **Pool:** 512 slots of 0x74 bytes at 0x47261c (`slot = id & 0x1ff`).
- **Lists:** free 0x443dd0; active 0x443de0; 0x443df0 and 0x443e00 are the other two lists shown in the debug text "Free/Active/Inactive/Useless". When the pool is full, `ObjCreate` recycles the head of 0x443e00. The delete queue is `DAT_00480e34`, linked via +0x38.
- **Lifecycle calls:**
  - `ObjCreate(class, team, x, y, z, arg)` 0x41e240 calls `class->init(class,obj,arg)`. A return of 0 fails; >0 links the object into its cell.
  - `ObjMarkForDelete` 0x41e480 queues the object; `ObjDestroyNow` 0x41e0a0 destroys it immediately.
  - `ObjMove(obj, dxyz)` 0x41e7e0 moves the object, relinks its cell and calls `ObjCheckCollision`. On a hit it reverts and returns 1.
  - `ObjSetParent(child,parent)` 0x41ec00 (attach/carry) calls class hooks +0x20/+0x24/+0x28/+0x2c.
- **Object struct (0x74 bytes):**

  | Offset | Meaning |
  |---|---|
  | +0/+4 | list links |
  | +8 | id (bits 0-8 slot, higher bits serial += 0x200) |
  | +0xc | flags (0x40 alive, 0x80 queued for delete, 0x200, 0x1000000 moving/sound, 0x2000000 new vehicle, 0x4000000 blocked) |
  | +0x10 | team (0/1; 2 = neutral) |
  | +0x14 | class |
  | +0x18 | think/controller function (vehicles: returns the control word) |
  | +0x1c | cell pointer |
  | +0x20/+0x24 | next/prev object in the same cell |
  | +0x2c | parent |
  | +0x30 | first child |
  | +0x34 | next sibling |
  | +0x38 | delete-queue link |
  | +0x3c | graphic/shape descriptor (from class+0x14; +8 = collision shape) |
  | +0x40/+0x44/+0x48 | x/y/z (16.16) |
  | +0x4c | heading |
  | +0x54 | speed |
  | +0x5c | team struct pointer (vehicles) |
  | +0x60 | vehicle-state pointer |
  | +0x60.. | class-specific |

- **Class descriptor:**

  | Offset | Meaning |
  |---|---|
  | +0 | class id |
  | +4 | name |
  | +8 | init |
  | +0xc | destroy (return 0 to veto) |
  | +0x10 | update |
  | +0x14 | graphic descriptor |
  | +0x1c | default think function |
  | +0x20..+0x2c | attach/detach hooks |
  | +0x30 | collision priority |
  | +0x34 | onHitStatic(obj,cell,bnoType) |
  | +0x38 | onHitObject(obj,other) |
  | +0x3c | extra hook |

- **Classes found** (id, name, descriptor address):

  | id | name | descriptor | notes |
  |---|---|---|---|
  | 0 | Missle | 0x450950 | generic projectile: shells, bullets, rockets |
  | 1 | Vehicle | 0x44b128 | |
  | 2 | Turret Gun | 0x44ad40 / 0x44ad90 (large) | |
  | 3 | FWall | 0x454f48 | flying wall/tower debris |
  | 4 | Shadow | 0x454fb8 | |
  | 5 | Storage | 0x443288 (rising) / 0x4432d8 (lowering) | bunker lift platform carrying a vehicle |
  | 6 | Destroyed Vehicle | 0x44b0d8 | wreck; crew bails out via `SpawnMan` |
  | 9 | Drone | 0x455778 | anti-camping hunter |
  | 10 | Mine | 0x455008 | |
  | 11 | Expl | 0x44aa50 | sub-behaviours in the table at 0x44a9c0 (23 functions 0x41fc50..0x4203b0) |
  | 12 | Flag | 0x44ae30 | |
  | 13 | Gate | 0x44ade0 | |
  | 14 | MAN | 0x43fb10 | soldiers |
  | 15 | SUB | 0x43fc10 | submarine |
  | 16 | Death Missle | 0x4509f8 | |
  | 17 | Stay | 0x455820 | corpses/remains |
  | 18 | Grenade | 0x450d48 | |
  | 19 | TRACER | 0x4509a0 | |

- **Collision:**
  - `ObjCheckCollision` 0x41dcf0 checks the object's cell and the cells above/below. `CollideWithCell` 0x41dee0 and `CollideWithCellContents` 0x41dac0 test the static BNO first (BNO +0x14 hit function, then class+0x34), then each dynamic object in the cell.
  - Objects are tested with `ShapesOverlap` 0x41c800, which takes position, heading and shape. Priority is by class+0x30; the hooks are class+0x34 and +0x38.
- **Turrets:** a tower BNO in a cell with HP becomes a live Turret object (`ActivateTurret` 0x423c10). Counts per team are kept in 0x472048. The turret dies after about 300 ticks when inactive (`TurretUpdate`).

### Teams, views, vehicles
- **Team struct:** 0x480f50 + team*0xd0.

  | Offset | Meaning |
  |---|---|
  | +0x14 | view pointer |
  | +0x18/+0x1c | pointers to the current/previous raw input word |
  | +0x28/+0x2c | bunker door cell / bunker terrain id |
  | +0x34 | pointer to the bunker position (`DAT_00480f84`) |
  | +0x78 | HUD block |
  | +0x7c/+0x80 | enemy-direction radar arrow (0-7, 8 = in view) |
  | +0xac..+0xbf | loadout copied from 0x4438b8. Default: +0xb0..b3 controller scheme per vehicle type; +0xb8..bb stock Tank 2 / Jeep 3 / MSV 2 / Heli 1 (0xff = infinite); +0xbc mines 30. |
  | +0xcc | tick when the vehicle last changed cell |

- **Other per-team globals:**
  - Current vehicle per team: `DAT_00471468[team]`, with its id in 0x471460.
  - Flag object per team: `DAT_00472038[team]`.
  - Flag home position: 0x4588b0 + team*12.
- **View struct:** 0x48bd90 + i*0x24c (P1, P2); 0x48c260 is the full-screen view.

  | Offset | Meaning |
  |---|---|
  | +0 | screen region |
  | +4 | team |
  | +8/+0xc | width/height |
  | +0x18/+0x1c | camera position |
  | +0x24 | camera tilt |
  | +0x68 | team pointer |
  | +0xc0 | mode function |
  | +0xcc | selected vehicle type |
  | +0xfc | HUD pointer |

  - View modes: `ViewModeBunkerSelect` 0x404570, `ViewModeZoomIn` 0x404e80, and `RenderWorldView`.
- **Vehicle definitions:** pointer table at 0x44afc8 → type 0 "Tank" 0x44b3a8, 1 "JEEP" 0x44b690, 2 "MSV" 0x44b978, 3 "HELI" 0x44bc60, 0xb8 dwords each. `VehicleInit` copies def+8 (0x50 dwords) into the per-team vehicle state at 0x459390 + team*0x140.
- **Vehicle definition fields** (dword index, Tank / Jeep / MSV / Heli):

  | Index | Tank | Jeep | MSV | Heli | Meaning |
  |---|---|---|---|---|---|
  | 0x3a | 100 | 250 | 100 | 200 | top speed, px/s-ish |
  | 0x3c | 49152 | 35108 | 61440 | 27306 | turn rate |
  | 0x84 | 400 | 500 | 320 | 400 | fuel, burned by \|speed\|·dt>>5; FuelWarn below 1/8 |
  | 0x65 / 0x6a / 0x69 | proj 0 (cannon) / 150 / reload 20 | | proj 8 (missile) / 100 / 30 | proj 7 (gun) / 100 / 15 | weapon slot 0: projectile type / ammo / reload ticks |
  | 0x72 / 0x77 | | | 10 (mines) | proj 6 missiles / 50 | weapon slot 1: projectile / ammo |
  | 0x5f, 0x60, 0x61 | | | | | fire functions for button A (0xe0), B (0x700), C (0x3800). Tank `TankFireCannon` 0x429d30; Jeep 0x42aa00/0x42aae0/0x42ab70 (grenades, flag); MSV 0x42a010/0x42a310 (missile, mine); Heli 0x42b100/0x42b2a0 |
  | 0x8c / 0x8d | | | | | init hook / death hook |
  | 0x90 | | | | | engine sound (tread / JeepStart / Servo) |
  | 0xaf | 0 | 4 | 6 | 8 | byte 0 = music track (Heli picks 8 or 9 at random); byte 1 = music priority 0x80 |

- **Vehicle state (0x140 bytes):**

  | Offset | Meaning |
  |---|---|
  | +0 | def |
  | +8 | control word |
  | +0xc | override-state function (lift/death) |
  | +0x10 | movement function |
  | +0x14 | fuel |
  | +0x24 + slot*0x10 | weapon slot: +8 next-fire tick, +0xc ammo |
  | +0x78 | fuel-warning timer |

- **Control word** (`ControllerDecodeDigital` 0x41edf0 / `ControllerDecodeAnalog` 0x41eee0, chosen by `VehicleSetController` from `PTR_00443e18[type*3+scheme]`):
  - Motion: 1 idle, 2 forward, 4 back; 0x10 left, 8 right.
  - Button A: press 0x20, hold 0x40, release 0x80. Button B: 0x100/0x200/0x400. Button C: 0x800/0x1000/0x2000.
  - 0x4000/0x8000 are extra buttons.
  - The raw input word is shifted into the top 16 bits.
  - Raw input bits: 0x80000000 up, 0x40000000 down, 0x10000000 left, 0x20000000 right, 0x8000000 A, 0x4000000 B, 0x2000000 C, 0x200000/0x400000 extra.
- **Projectiles:** `FireProjectile(pos, offset, heading, pitch, type, team, owner)` 0x431760 uses the type table at 0x450a78 (12 entries, stride 0x3c). Fields: +0 class (Missle/TRACER/Death Missle), +8 category, +0xc speed, +0x18 sound, +0x24 range, +0x2c/+0x30 sprites, +0x34 explosion, +0x38 packed damage.

### Lifecycle and rules
- **Bunker, vehicle select and lift:**
  1. `ViewEnterBunkerSelect` opens the vehicle picker.
  2. `SpawnLiftVehicle` 0x428020 creates a Storage lift that rises with the chosen vehicle.
  3. `StorageUpdate` 0x417fb0: when the lift is up, `SpawnVehicle` 0x427cd0 creates the Vehicle object and marks it on the radar.
- **Returning a vehicle:** driving onto your own bunker door terrain and pressing any fire key (0x920) calls `DockVehicleInBunker` 0x418470. It increments the stock and lowers the vehicle. Standing on the door without pressing runs `BunkerDoorAnimate`.
- **Death:** at 0 fuel or when the damage bits in the vehicle cell equal 0xe000000, the vehicle becomes a "Destroyed Vehicle" wreck and the player returns to the bunker select.
- **Idle penalty:** if a vehicle stays in the same cell for more than 0x168 (360) ticks and fewer than 3 drones exist, `SpawnDrone` 0x435950 sends a Drone.
- **Flag:**
  - The flag is hidden in one random flag building per team (`HideFlagInRandomBuilding` 0x424060).
  - Destroying that building spawns the Flag object (`FlagBuildingDestroyed` 0x424170). It sets music bit 0x100 for the "Flag Discovery" cue.
  - Only a Jeep (type 1) can pick up the flag (`FlagOnTouch` 0x424760, via `ObjSetParent`). Carrying it sets music bit 0x200 ("Flag Pickup").
- **Win:**
  - `JeepFlagCheck` 0x42a480 (jeep think): if the jeep carries the enemy flag and is on its own bunker terrain, it calls `EndGame(team)` 0x40f380.
  - The lift also checks: enemy flag on your lift calls `TeamWins`; your own flag on your lift re-hides it.
  - The jeep also drives a beacon compass (`team+0x5c` beacon strength, Beacon sound).
- **End of game:** `EndGame` sets `DAT_00457100` = winner (0/1; other values = no winner) and switches the main loop state to 0x40f050 (win movie / high score).

### Open questions
- The exact AI of soldiers, turrets and drones is not yet traced. The man states are 0x4065f0 (walk/attack), 0x406e40 (swim) and 0x406a90/0x406bc0 (think).
- The movement physics functions are not traced: vehicle def+0x14 (0x429f20 tank, 0x42a410 jeep, 0x42a250 MSV, 0x42b3c0 heli) and 0x42abe0 (heli).
- `ControllerDecodeAnalog` is not fully read.
- The damage model is not fully read. Damage is packed in the projectile +0x38 field, and cell HP is in bits 25-27.
- The Expl sub-behaviour table at 0x44a9c0 and the HUD gauge callbacks at 0x44acc4 are unnamed.


## 7. Sound and music


All addresses are VAs in RFIRE.BIN. Names below are applied in Ghidra (`returnfire/rfire_game.exe`).

### 1. DirectSound / sample loading

- `Snd_InitDirectSound` (0x411e20): `DirectSoundCreate`, `SetCooperativeLevel(hwnd, DSSCL_PRIORITY=2)`, creates the primary buffer (flags 0x81) and sets its format to **44100 Hz, 16-bit, stereo** (the WAVEFORMATEX at `DAT_004814b8`). It is called from `FUN_0040b9b0` (sound bootstrap), which then calls `Snd_InitMixer` and `Snd_LoadAllSamples`.
- **Sample table** at `0x43e550`: 37 entries x 0x1c bytes (the loop runs until `0x470e80..0x470f14`, one record pointer per sample):
  - +0x00 `char* path` ("Sound/ExplLar.SDT" ...)
  - +0x04 flags (0x100 = looping sample: Tread, Heli, JeepIdle, Drone; ExplLar has 0x400)
  - +0x08 `SampleRec*`, filled at load time
  - +0x10 native sample rate, filled at runtime and read by the pitch callbacks
  - +0x14 `marker list*` or 0: pairs {0x1f, loopStart}, {0x20, loopEnd}, where 0x80000000 = whole sample. Tread, JeepIdle, Heli and Drone use the whole sample. Raise uses 1440..3312 and PreRais uses 820..1836.
  - +0x18 index
- `Snd_LoadAllSamples` (0x42cbc0) handles each entry in turn: `Wav_LoadFile` (0x4079f0, mmio RIFF WAV reader, since SDT = plain WAV) → `CreateSoundBuffer` (static, flags 0xE0 = CTRLPAN|CTRLVOLUME|CTRLFREQ) → Lock/copy/Unlock → SetVolume(-500) → SetPan(0). SampleRec is 0x138 bytes: [0] PCM ptr (freed after upload), [1] byte size, [2] WAVEFORMATEX*, [10] (+0x28) IDirectSoundBuffer*.
- **Sound-event table** (`0x43e978`, 0x18 bytes per entry, 46 entries up to 0x43edc8). This is what game code actually "plays":
  - +0x00 debug name ("JeepIdle", "SmallBoom", "MetalHit1"...)
  - +0x04 `SampleTable*`
  - +0x08 u8 start priority
  - +0x09 u8 decayed priority
  - +0x0a u8 decay time. After `start + decay` ticks (16 ms each), the priority falls from +8 to +9.
  - +0x0b u8 flags. 0x01 = stop the sound when the owner dies (engine loops); otherwise it keeps playing at the owner's last position. 0x20 = use the owner's own L/R gains (Drone). 0x40 = start muted. 0x02/0x04 = mute the right/left channel for unowned sounds.
  - +0x0c i32 frequency. 0 = native; <0 = -(16.16 ratio) x native rate; >0 = absolute Hz. The debug editor steps it up to 22050<<16.
  - +0x10 16.16 gain / mix share (0x1111 ≈ 1/15)
  - +0x14 per-instance update callback (JeepIdle → `SndCb_JeepEnginePitch`, Heli → `SndCb_HeliRotorPitch`)
- Tables of event pointers at 0x43ede0.. and 0x43ee2c.. are random-variant / per-material hit tables: MetalHit1-4, Concrete, Dirt, Throw1-3 and so on.

### 2. Sound command queue and mixer

- Game code never touches DirectSound directly. It calls **`Snd_QueueCommand(cmd, a, b, flushNow)`** (0x42cb40, called about 60 times). This pushes {cmd, a, b} on the queue `0x481370`. `Snd_Service(DAT_00443728)` drains the queue through the table `PTR_FUN_00443758`:

| cmd | fn | meaning |
|---|---|---|
| 0 | SndCmd0_Sync | no-op / sync marker |
| 1 | SndCmd1_Play | a = `SoundEvent*`, b = owner object (0 = global/UI). Result handle in node[5] |
| 2 | SndCmd2_Refresh | mark all dirty (called every frame as `(2,0,0,1)`) |
| 3 | SndCmd3_Stop | a = instance, b = owner |
| 4 | SndCmd4_Update | recompute gains/priority of instance a |
| 5 | SndCmd5_OwnerCallbacks | run the callbacks of every instance attached to object b |
| 6 / 7 | SndCmd6/7_SetListenerNPos | listener 1/2 position ptr = b (player struct +0x18: 0x48bda8 / 0x48bff4) |
| 8 | SndCmd8_SetListenerGain | listener[a] gain ptr = b (player +0xe0: 0x48be70 / 0x48c0bc) |
| 9 | SndCmd9_KillByHandle | a = handle, b = instance |
| 10 | SndCmd10_StopAll | a = 0: stop unowned sounds only; a = 1: stop everything |

- Pools (set up by `Snd_InitMixer`, 0x419970): **20 hardware voices** (0x457210, stride 0x50, list 0x481330) and **15 sound instances** (0x457860, stride 0x58, free list 0x481380). The active-instance list is 0x481320, sorted by (priority, gainL+gainR) using `Snd_InstancePriorityCmp`.
- Instance fields (dword index):
  - [4] event*
  - [5] handle
  - [6] decay deadline
  - [7]/[8] current/decayed priority
  - [9] flags
  - [10] voice
  - [0xb] SampleRec
  - [0xc] owner object
  - [0xe]/[0xf] gain L/R (16.16)
  - [0x10] frequency
  - [0x11] event gain
  - [0x12] gain function
  - [0x13..0x15] last owner position
- **Distance attenuation** (`Snd_CalcGainTwoListeners`, 0x419280): `g = clamp((0x1a8 - dist)/0x180, 0, 1)`, multiplied by the listener gain. Distance comes from `FUN_0041ece0` (float distance → 16.16). Sounds are full volume within 40 units and silent beyond 424 units.
  - **Gain L is computed from listener 1 (player 1) and gain R from listener 2.** In 1-player mode both listener slots point at player 1, so sounds are centred. In 2-player split-screen, player 1's sounds come from the left speaker and player 2's from the right. There is no per-source stereo panning beyond that.
- Voice mixing (`Snd_MixAndUpdateVoices`, 0x418e90) runs in priority order. Each side has a budget of 0x7fff. Each instance takes `min(gain x eventGain, remainingBudget)`, so loud or high-priority sounds starve the rest.
  - Per voice: `volume_dB = max(L,R)/3 - 2200`, `pan = (R-L)*10000/max` → `Snd_VoiceSetParam(ch, v, 1=volume / 3=pan / 2=frequency)`.
  - Newly bound voices start through `Snd_VoicePlay` (0x42c930). This calls `DuplicateSoundBuffer` from the SampleRec, then SetPan/SetVolume and `Play(looping = instFlags & 1)`.
- Engine pitch callbacks:
  - `SndCb_JeepEnginePitch` (0x42b960): `freq = lerp(desc[+0x244] idle Hz, desc[+0x248] max Hz, |obj.speed(+0x54)|)`. The jeep descriptor at 0x44b690 has 7058 → 14860 Hz.
  - `SndCb_HeliRotorPitch` (0x42b9b0, function created by me): `freq = 2621 + (nativeRate-2621) * heli[+0x84 rotor spin]`.
- Vehicle activation (`FUN_00428490`) plays `vehicleDesc[+0x240]` (engine-start event; JeepStart for the jeep) through cmd 1 with the vehicle as owner.
- Debug "Play Sound / Stop Sounds" editor: `Dbg_SoundEditorLoop` (0x409e10) and `Dbg_SoundEditorDrawField` (0x40a420). It edits event fields +8, +9, +a, +c and +10 and cycles the +4 sample through the sample table.

### 3. Music

Two backends sit behind one driver table `PTR_FUN_0044aab8` (`MusDrv_Command(n, arg)`, 0x421090):

- **Streaming WAV (normal path).** A music thread (`Mus_ThreadProc`, 0x430910, created in WinMain `FUN_0040bc20`) waits on a semaphore. `Mus_ThreadRequestTrack(track)` (0x430a20) stores `DAT_0044f320 = track` and releases the semaphore. The thread then calls `Mus_StartTrack` (0x430650).
  - `MusStream_Open` (0x42c320) opens `DAT_0044f328 + track*0x100` (Score.WAV x17, Drums.wav for 17/18) with `AVIStreamOpenFromFileA(streamtypeAUDIO)`. It creates a DirectSound streaming buffer of 44100/16/2, and sets [start,end) from the track table: the "alt" start is used when `DAT_004439e0` = 1, i.e. when looping.
  - Every 200 ms, `MusStream_Pump` (0x42bf00) refills the ring buffer from `AVIStreamRead` (`MusStream_FillBuffer` / `MusStream_ReadChunk`, which pads with silence at the end). At the end it calls `Mus_OnTrackEnd`.
  - **Offsets are relative to the WAV data chunk**, as byte offsets that `AVIStreamRead` turns into sample indices (/blockAlign 4). For Score.WAV the data chunk starts at file offset 0x2c. Drums.wav has a `fact` chunk, so its data starts later; always locate the data chunk.
- **MCI path** (`DAT_0044f31c` device, `mciSendCommand PLAY 0x806` with from/to taken from the runtime table 0x46f3d0) is a legacy fallback for CD audio or waveaudio. Treat it as dead for the reimplementation.
- **Volume fades**: `MusDrv5_SetVolumeNow` / `MusDrv6_SetVolumeTarget` set target `DAT_00472280` and current `DAT_00472274` (0..0x7fff). `Mus_VolumeFadeTick` (0x4210c0) moves the current value toward the target at 0x444 per 16 ms tick, then converts to dB `(v-0x7fff)/10` via `MusStream_SetVolume`.

#### Track table (0x443aa0, 18 x 0x2c)

- +0 u8 id
- +1 u8 groupMin
- +2 u8 groupMax
- +3 u8 flags. bit0: allowed even when `DAT_004438b0` (music enabled) = 0.
- +4 `char* name`
- +9 u8 default transition index
- +0x10 `transition list*`: {u8 fromTrack, u8 transitionIdx, pad[6]}, terminated by 0xff
- +0x14 six dwords copied to the driver params `DAT_00480e70`:
  - mode: bit1 = one-shot, bit2 = restart, bit3 = flush, bit4 = continue
  - startIdx, altIdx, endIdx: negated indices into the byte-offset table `0x44bf78`
  - 0x7fff: volume now (-1 = none)
  - -1: volume target

Transitions (`PTR_FUN_00443db8`) run when switching from the current track to a new one:

| idx | function | behaviour |
|---|---|---|
| 0 | MusTrans0_FadeOutRestart | fade out, stop, then start the new track |
| 1 | MusTrans1_ContinueSeamless | keep the stream position and only change the end/loop parameters. Used between Tank 1/2/3, between Heli 1/2, and Jeep→Jeep |
| 2 | MusTrans2_CutRestart | hard cut to the new track. Used for deaths, Flag Discovery, Win, Death, Drums and from Flag Pickup to Jeep Death |
| 3 | MusTrans3_Revert | revert to the previous track |

**Request / priority** — `Mus_Request(track, prio, ownerObj)` (0x41d530), where track -1 = silence:
- The request is refused if `prio < curPrio`.
- At equal priority it is refused unless the track lies in the current track's group [min,max].
- `curPrio` decays by 1 per `Mus_Service` call (once per frame) down to `prio - 0x28`. Short stingers therefore become interruptible after about 40 frames.
- If an owner is given, the music stops (`(-1, 0x7fffffff)`) when the owner's object id changes, i.e. when the owner dies.
- `Mus_Service` (0x41d350) is called every frame. It performs pending transitions and issues driver cmd 8.

**Director** — `Mus_Director` (0x41d730, called once per game frame) uses state `DAT_00480e5c`. Event bits in `DAT_00480e3c` are cleared every frame:
- **Vehicle death** (highest precedence): `DAT_00480e4c` = the dying vehicle, set by `FUN_004292d0`. The director plays `deathTrack[type]` from the byte table at `0x443dcc` = {3, 5, 7, 10}. The vehicle types are 0 = tank, 1 = jeep, 2 = ASV/MSV, 3 = heli. If the dying vehicle wasn't the one whose music was playing, it plays 15 "Death" instead. Priority 0x80, state 9.
- **0x100** (`FUN_00424170`, flag building destroyed → flag object spawned): 11 Flag Discovery, priority 0xFE.
- **0x1000** (`Sub_UpdateSetsMusicFlag` 0x4073a0, the "SUB" object class at 0x43fc10 is alive): 16 Sub, priority 0x8A. The state returns to 1 when the sub disappears.
- **0x200** (`FUN_00424380`, a flag picked up by a player of the other team): 12 Flag Pickup, priority 0x8A.
- **0x400** (`FUN_00428490`, the player-vehicle update runs; `DAT_00480e48` = the vehicle): the director plays `Mus_PickVehicleTrack` (0x41d660) at priority 0x80.
  - Tank: 2 if the own flag is being carried or the enemy flag is within 0x4000. Otherwise 0 if `stock[type]==2`, else 0 or 1 at random 50%.
  - Heli: 8 if `stock==2`, else 8 or 9.
  - Other vehicles use `desc byte +0x2bc` (jeep = 4, ASV = 6), with priority `desc +0x2bd` (jeep 0x80). The vehicle-enter path calls `Mus_Request` directly with these.
- **All players in the bunker / vehicle select** (`DAT_00480e9c` ≥ player count; incremented in `FUN_00404d30`, decremented in `FUN_00404ae0`): 14 Bunker, priority 0x68. At game start, `FUN_0040cbb0` / `FUN_0040e930` request it at 0x80.
- **Front-end** (`FUN_0040ed70` / `FUN_0040ef00`): 17 Drums, priority 0x32.
- **Win screen** (`FUN_0040f050`): `Mus_StartWinMusic` (0x430b30) stops all SFX, resets the volume and plays 13 Win at priority 0xFF.
- **Track end** (`Mus_OnTrackEnd`, 0x41d970):
  - Loop tracks re-request themselves with alt-start.
  - One-shots return to the previous track (priority 100) if a player is back in the bunker update (`FUN_00419dd0`). Otherwise they fall back to Bunker at priority 0x40.
  - Win stays silent afterwards.
- `Mus_Disable` / `Mus_Enable` (0x430af0 / 0x430b10) implement the menu music toggle (`DAT_004814a4`).

#### Final track table (offsets relative to the data chunk; 176400 B/s)

| # | name | file | byte range | length | loop restart (alt) | mode | group min/max/flags | default trans | trigger |
|---|---|---|---|---|---|---|---|---|---|
| 0 | Tank 1 | Score.wav | 82449636..156106756 | 417.56 s | 245.83 s (off 125814868) | loop | 0/3/0 | 0 | Tank selected (stock==2 i.e. first tank, or random 50%) |
| 1 | Tank 2 | Score.wav | 88747116..156106756 | 381.86 s | -35.70 s (off 82449636) | loop | 0/3/0 | 0 | Tank selected (random alt variant) |
| 2 | Tank 3 | Score.wav | 125814868..156106756 | 171.72 s | -245.83 s (off 82449636) | loop | 0/3/0 | 0 | Tank near/with flag (own flag object carried, or enemy flag within 0x4000) |
| 3 | Tank Death | Score.wav | 154597380..156106756 | 8.56 s | start | one-shot | 0/3/0 | 0 | Tank destroyed (deathTrack[type0]) |
| 4 | Jeep 1 | Score.wav | 156445224..170838104 | 81.59 s | start | loop | 4/4/0 | 0 | Jeep entered (vehicle desc byte +0x2bc = 4) |
| 5 | Jeep Death | Score.wav | 216090712..217147480 | 5.99 s | start | one-shot | 5/5/0 | 0 | Jeep destroyed |
| 6 | MSV 1 | Score.wav | 54361096..81390812 | 153.23 s | start | loop | 6/7/0 | 0 | ASV/MSV entered (desc track byte) |
| 7 | MSV Death | Score.wav | 81390812..82144480 | 4.27 s | start | one-shot | 6/7/0 | 0 | ASV destroyed |
| 8 | Heli 1 | Score.wav | 0..53944328 | 305.81 s | 82.25 s (off 14508036) | loop | 8/10/0 | 0 | Heli selected (stock==2, or 50% random) |
| 9 | Heli 2 | Score.wav | 14508036..53944328 | 223.56 s | -82.25 s (off 0) | loop | 8/3/0 | 0 | Heli selected (random alt) |
| 10 | HELI Death | Score.wav | 53567496..53944328 | 2.14 s | start | one-shot | 8/10/0 | 0 | Heli destroyed |
| 11 | Flag Discovery | Score.wav | 170969176..172968024 | 11.33 s | start | one-shot | 11/11/1 | 2 | Flag revealed (flag building destroyed, bit 0x100), prio 0xFE |
| 12 | Flag Pickup | Score.wav | 173156440..207502428 | 194.71 s | start | loop | 12/12/0 | 0 | Enemy flag picked up (bit 0x200), prio 0x8A |
| 13 | Win | Score.wav | 207502428..208939096 | 8.14 s | start | one-shot | 13/13/1 | 2 | Win sequence (Mus_StartWinMusic, prio 0xFF) |
| 14 | Bunker | Score.wav | 210364504..215427160 | 28.70 s | start | loop | 14/14/0 | 0 | All players in bunker/vehicle-select (prio 0x68/0x80); fallback after one-shot ends (prio 0x40) |
| 15 | Death | Score.wav | 216090712..217147480 | 5.99 s | start | one-shot | 15/15/0 | 2 | Generic death (player died while no vehicle music, prio 0x80) |
| 16 | Sub | Score.wav | 217622636..222562636 | 28.00 s | start | one-shot | 16/16/1 | 0 | Enemy submarine active (bit 0x1000), prio 0x8A |
| 17 | Drums | Drums.wav | 0..2695172 | 15.28 s | 7.38 s (off 1302532) | loop | 17/17/1 | 2 | Front-end menus (prio 0x32) - Drums.wav |
Notes:
- Tank 1/2/3 are three entry points into one ~7-minute piece (82.4M..156.1M). The tank death sting (154.6M) is the tail of that same piece. Tank 1 loops back to Tank 3's start; Tank 2 and 3 loop back to Tank 1's start. The same holds for Heli 1/2 (0..53.9M, Heli Death is the tail).
- Jeep Death and Death share one range.
- The byte-offset table at 0x44bf78 has 32 slots. Slots 26..31 belong to Drums.wav: 0, 1302532, 2695172.

### Key globals
- `DAT_0043fce4` IDirectSound; `DAT_004814d8` primary buffer; `DAT_0043fc90` sound enabled; `DAT_00470f7c` music stream buffer; `DAT_0047102c` AVI audio stream.
- `DAT_00480e3c` music event bits; `DAT_00480e5c` director state; `DAT_00480e50` current music priority; `DAT_00480e60` requested track*, `DAT_00480e90` playing track*, `DAT_00480e8c` previous track*.
- `DAT_0044f320` track id for the thread; `DAT_004438b0` Score.WAV available / music enabled; `DAT_004814a4` music muted by menu.
- `DAT_00472274`/`DAT_00472280` music volume current/target.
- `DAT_00443748`/`DAT_0044374c` listener gain ptrs; `DAT_00481350`/`DAT_00481354` listener positions.

### Reimplementation notes
- SFX: implement a 20-voice mixer with 15 logical instances, priority plus decay, and the two-listener L/R model. Frequency is set per event (JeepIdle 7058 Hz base, Servo 3932 Hz) and by the engine callbacks.
- SFX port: `src/sfx.c` (+ `src/sfx_tables.c` from `tools/gen_sfx_tables.py`, test `tests/sfx_test.c`). Verified details:
  - The event table really starts at 0x43e960 ("tread", the tank/ASV engine loop) and has 48 entries through 0x43edc8. 0x43ede0 is a 44-entry pointer list (events 4..47) indexed by byte scripts; 0x43ee2c (list+19) = the 3 Throw Grenade variants (`RandRange(3)`); 0x43f278 = {PreRaise, Raise} for the bunker zoom-script sound step `FUN_004042b0` (step a = event, b = -ratio, voice freq = b*-11000>>16, c = ticks until cmd 9 kill).
  - The sample-table rate field (+0x10) is never written, so event frequency ratios evaluate to 0, and no code sends the event frequency to a voice: voices play at the sample's native rate. Only the pitch callbacks (and the bunker step) call SetFrequency. The rotor pitch is therefore `2621 + 2621*rotorSpeed(+0x84, 0..4.0)`. Loop markers are never read; looping (event flag 1) loops the whole buffer.
  - The instance list is sorted by priority descending, and within equal priority by gainL+gainR *ascending*. One-shots that get no voice at the next Snd_AssignVoices are dropped (so inaudible one-shots are never heard later); looping ones wait.
  - Voice volume is `min(0, max(L,R)/3 - 2200)`, so a bound voice with level 0 still plays at -22 dB, and levels >= 6600 saturate at 0 dB. A new voice starts at `max/2 - 3000` (Snd_VoicePlay) until the next mix update.
  - Distance is 3D (integer units, truncated sqrt). In 1P the listeners are player 1's camera (view+0x18: camx, camy, H) at x-10 and x+10, which gives a slight stereo spread. The listener gain is view+0xe0: 0 in bunker select, copied from the fade in zoom-in/fade-out, 0x10000 while driving. Unowned sounds ignore distance and listener gain.
- Music: stream Score.WAV ranges. Keep the transition semantics, especially continuing the same stream position between Tank/Heli variants, and the priority rules.


## 8. Renderer


### Key finding: this is the 3DO renderer running on a software shim
Return Fire is a 3DO port. The Windows binary keeps the 3DO **Cel Control Block (CCB)** model and puts a
small software "Portfolio" emulation layer under it: Screen/Bitmap items, `DrawCels(bitmap, ccbList)`,
clip width/height/origin setters, and 3DO PLUTs (RGB555 colour lookup tables) inside the art.
`ART.CAR` (magic `CCBA`) is an array of 3DO-style CCBs (17 dwords = 0x44 bytes each) plus pixel/PLUT data.
Everything in the game view (floor tiles, vehicles, buildings, bullets, HUD digits/icons, radar) is a CCB
that gets appended to a display list and rasterised in software straight into the **locked DirectDraw back
buffer** (8-bit, palettised). The only things not drawn as cels are full-screen BMP/RFA images
(title, pause, status-bar background), which are DirectDraw `BltFast`/`Blt` copies from off-screen surfaces.

### Display modes, surfaces, presentation
- `DAT_0043fcf4` = display mode. 1/2/3 = **fullscreen** (SetCooperativeLevel 0x51 → SetDisplayMode(w,h,8) →
  primary with 1 back buffer, caps 0x218 (PRIMARY|FLIP|COMPLEX), back buffer from GetAttachedSurface).
  5/6/7 = the same thing **windowed** (coop NORMAL, primary plus a 640x480 SYSTEMMEMORY off-screen "back
  buffer" (caps 0x840), clipper on the window). Mode 1/5 = 640x480, 2/6 = 320x240. Mode 3/7 also exists and
  is treated like 2/6 in most switches. `SetDisplayMode` = 0x408030, `InitDirectDraw` = 0x407f30.
- `DAT_0043fcf0` = **hi-res flag** (0 = 320x240, 1 = 640x480). `DAT_0043fce8/ec` = width/height.
  Menu IDs 0x412 (lo) and 0x413 (hi) switch between them at runtime.
- `DAT_0043fcbc` IDirectDraw, `DAT_0043fcc0` primary, `DAT_0043fcc4` back/off-screen buffer,
  `DAT_0043fccc` clipper, `DAT_00459fd0` IDirectDrawPalette.
- Lock/unlock: `DD_LockBackBuffer` 0x433080 stores `lpSurface` → `DAT_00472288` and `lPitch` → `DAT_0047228c`
  (and restores lost surfaces). All rasterisers write to `DAT_00472288 + y*DAT_0047228c + x`.
- `PresentFrame` 0x436ba0: fullscreen → `Flip` (vtbl+0x2c). If `DAT_004815ec`, uses `BltFast` instead.
  Windowed → `Blt` (vtbl+0x14) back buffer → client rect (so it stretches). It only presents when the dirty
  flag `DAT_004815e8` is set. It increments frame counter `DAT_00481608`, then calls `UpdatePaletteFade`.
- **Palette/fades**: `SetFadeTarget(target,ms)` 0x436890 and `UpdatePaletteFade` 0x436970. Brightness is
  `DAT_00455998` (0..0x10000) and ramps toward `DAT_0045599c` in real time (timeGetTime). The palette is
  built by subtracting `(0x10000-b)>>8` from each RGB of the master palette `DAT_0045ab64` (copied from
  `DAT_0045a640` when `DAT_0045ab58` is set), then `SetEntries`. Fullscreen sets all 256 entries; windowed
  sets 10..245 only.

### Screen contexts / double buffering (3DO ScreenGroup emulation)
- `Screen_InitContexts` 0x427a00 builds **two screen contexts** at `0x471580` and `0x4715a4`
  (stride 0x24 = 9 dwords, index = `DAT_0045f3a0`, which `FrameTiming` 0x433220 toggles every frame).
  Each context holds:
  - `+0`: an item of 0x7c bytes (screen), with an owner copy at `+4`
  - `+8`: a **Bitmap** of 0x84 bytes, with an owner copy at `+0xc`
  - `+0x28/+0x2c`: width/height
  - `+0x24`: buffer
  - `+0x38` clipW, `+0x3c` clipH, `+0x40` clipX, `+0x44` clipY (`Bitmap_SetClip*` 0x42bc40/0x42bc00/0x42bc20)
- `DAT_0045f3ac` = the current screen context. It is set to the context that is not being displayed.
  In practice both point into the same DD back buffer; the dual set exists only to mirror the 3DO
  double-buffered dirty logic.
- `Screen_CreateViewBitmaps` 0x4333f0 creates per-player view Bitmaps (via `Item_CreateBitmap(0x203,tags)`
  0x42ba00) that carry the viewport clip rect. Viewport sizes in 320x240 space:
  - 1P: (0,0,320x152). With alt layout `DAT_004810f4`: (13,0,357x169).
  - 2P: P1 (0,0,156x149) and P2 (164,0,156x149). Alt layout: (13,0,175x169) and (197,0,175x169).
  - The camera screen centre is set by `Camera_SetScreenWindow` 0x403550.
  - Set up by `SetupViewports1P` 0x41a480 and `SetupViewports2P` 0x41a750.
- `View_SetClipHeight` 0x419ca0 clamps the view bitmaps' clip height to the top of the HUD.

### Display list (CCB list)
- CCB pool: `DAT_0048ca90` walks from `0x45af90` to `PTR_DAT_004546a0` = 0x45f390, which holds 256 CCBs.
  `DAT_0048ca94` = tail next-pointer slot, `DAT_0045f3a4` = list head, `DAT_0048ca8c` = last CCB,
  `DAT_0044bf44` = cels-drawn counter.
- `Cel_AddToList` 0x401220 copies a 0x44-byte template CCB (normally `DAT_00440c54 + idx*0x44`, a CCB from
  ART.CAR) into the pool, links it, and returns it for patching. It auto-flushes when the pool is full.
- `Cel_FlushList(ctx)` 0x433730 marks the last CCB (flag 0x40000000 = CCB_LAST), calls
  `Cel_DrawToBitmap(ctx[buf].bitmap, list)` 0x41fc20, then resets the pool. If `ctx` is non-NULL it also
  switches the target context (`PTR_DAT_004546a4`). `Cel_DrawToBitmap` = lock back buffer →
  `(*PTR_FUN_0044ae90)` (= `Cel_DrawList` 0x4262f0) → unlock.

#### CCB layout as used (0x44 bytes, 3DO-derived)
| off | meaning |
|---|---|
| +0x00 | flags. byte0 bit 0x20 = transparent (index 0 skipped). 0x1000 (byte1 0x10) = the four corners at +0x10.. are explicit points, not X/Y+deltas. byte3 0x40 = LAST. byte3 0x80 = skip |
| +0x04 | next CCB |
| +0x08 | source pixels (8-bit indexed, row stride = width) |
| +0x0c | PLUT pointer (3DO RGB555), or remap table for modes 0x11/0x12 |
| +0x10,+0x14 | X, Y (16.16) — or corner0 x,y when explicit |
| +0x18,+0x1c | HDX, HDY (per-pixel horizontal step, 12.20 as 3DO: `>>4` then `*w>>16`) |
| +0x20,+0x24 | VDX, VDY (per-row step, 16.16) |
| +0x28,+0x2c | HDDX, HDDY (per-row delta of HDX/HDY) — for explicit corners: corners 2..3 |
| +0x34 | **draw mode** (3DO PIXC slot reused; see table) |
| +0x38 | mode parameter (shade level / fill colour / override width) |
| +0x3c,+0x40 | width, height in source pixels |

`Cel_ComputeCorners` 0x4260a0 (lo-res, `>>16`) and `Cel_ComputeCorners2x` 0x426180 (hi-res, `>>15`) turn
X/Y/HDX/HDY/VDX/VDY/HDDX/HDDY into a screen-space quad. **Hi-res uses the same art and the same 320x240
logical coordinates, scaled 2x by the rasteriser.** Only the full-screen BMPs have separate hi-res versions
(`*H.rfa`, HiBck*, PS480).

#### Draw modes (`Cel_DrawList` switch on ccb+0x34) → span function (`DAT_004717fc`)
TRANS.TBL sub-tables: `T_BLEND` = +4 (256x256), `T_SHADE[k]` = +0x10004 + k*256 (k = 0..31),
`T_BRIGHT[k]` = +0x12004 + k*256.

| mode | rasteriser | pixel op |
|---|---|---|
| 0 | quad (0x410b9c) | copy; skip 0 if flags&0x20 |
| 1 | quad | **shadow**: where src≠0, `dst = T_SHADE[4][dst]` (0x481748) |
| 2 | quad | lighter shadow, `T_SHADE[2]` (0x481724) |
| 3,4 | quad | **translucent**: `dst = T_BLEND[src*256+dst]` (src≠0) |
| 5 | quad | `dst = T_BRIGHT[0][dst]` where src≠0 (glow) |
| 6 | `Cel_BlitUnscaled2xH` 0x41124d | unscaled blit, rows doubled in hi-res (HUD) |
| 7/9/10 | `Cel_FillRect` 0x411628 | solid fill. Colour: 0 / ccb+0x38 / nearest-index of PLUT[1] RGB555 (+10) |
| 8 | `Cel_ShadeRect` 0x41176c | `dst = T_SHADE[(p>>11)][dst]` — full-screen fade (see `Cel_SetupFullscreenFade` 0x407b90) |
| 0xb,0xf | `Cel_BlitUnscaledB` 0x4118ba | clipped 1:1 blit (width override = +0x38) |
| 0xc | `Cel_DrawTrapezoid` 0x40fe4f | floor tile: horizontal-edged trapezoid, texture-stepped |
| 0xd | `Cel_DrawShadowSpans` 0x411ba5 | trapezoid through `T_SHADE[4]` |
| 0xe | `Cel_BlitUnscaledE` 0x41100f | 1:1 blit at explicit point |
| 0x10 | quad | keyed blend: 4 key colours (ccb+0x38 bytes) are copied, the rest blended |
| 0x11 | quad | `dst = table[src]` (table = ccb+0xc, e.g. team colours) |
| 0x12 | quad | `dst = T_BLEND[table[src]*256+dst]` |
| 0x13 | quad | opaque textured quad (used for some floor tiles) |

The quad rasteriser `Cel_DrawQuad` 0x410b9c builds 4 edges with `Cel_ScanEdge` 0x410120 and fills
spans through one of:
- `Span_Opaque` 0x40fb30
- `Span_Transparent` 0x40fcc3
- `Span_ShadeDst` 0x426d10
- `Span_Blend` 0x426f10
- `Span_BlendKeyed` 0x427110
- `Span_RemapSrc` 0x427320
- `Span_RemapBlend` 0x427510
- `Span_RemapOpaque` 0x427700

Clip rect: `DAT_004717f8`..`DAT_00471800` (x) and `DAT_00471804`..`DAT_00471808` (y), taken from
bitmap+0x40.. (doubled in hi-res).

### TRANS.TBL (`Pal_InitTransTables` 0x4090c0, called from `Car_Load` 0x4095f0)
File size 0x14004 = 81924 bytes: `byte0 = 1` (version), 3 pad bytes, then:
- +0x00004: 256x256 **average blend** table, `T[a*256+b] = nearest(avg(pal[a],pal[b]))` → `DAT_0048172c`
- +0x10004: 32 **darken** tables, `T[k][c] = nearest(pal[c]*(31-k)/32)` → `DAT_00481740`
  (k=2 → `DAT_00481724`, k=4 → `DAT_00481748`)
- +0x12004: 32 **brighten** tables, `T[k][c] = nearest(pal[c] + 3*(k+1))` (clamped at 255) → `DAT_0048173c`

The tables are computed against the second palette in ART.CAR (`car+0x10+0xc` → +0x400). If the file is
missing or its version byte is wrong, the game regenerates it with `GetNearestPaletteIndex` and writes it
back. A reimplementation can simply regenerate it.

### ART.CAR interface (`Car_Load` 0x4095f0; the format itself is another agent's job)
- The whole file is read into `DAT_00440c50` (size+0x4000 slack). `Car_Relocate` 0x409560 turns offsets
  into pointers. Header: +4 = file size, +8 = count, +0xc = offset.
- CCB array `DAT_00440c54` = file+0x10, stride 0x44. Each entry's +4/+8/+0xc (next/pixels/PLUT) is
  relocated. `DAT_00440c58` = file+[0xc]. Palettes are at `*(DAT_00440c54+0xc)` (+0 main, +0x400 second).
- **Sprites are addressed by CCB index**: `DAT_00440c54 + idx*0x44`.
  - Floor tiles use index `cell & 0x7f`.
  - HUD examples: digits 0x862..0x86b, icons 0x86c–0x874, 0x794/0x795.
  - `0x238b4/0x44` is the full-screen fade cel.
  - `0x20e34/0x44` = 6 radar cels pointing at the 128x128 radar buffer `DAT_00440c64` (placed right after
    the file data).
- The water/colour cycle rotates 6 RGB555 entries of the PLUTs of CCB 1 (+0x50) and CCB 2 (+0x94)
  (`Pal_InitWaterCycle` 0x419ac0, `Pal_CycleWater` 0x419b50, speeds 0xccc and 0xf5c per tick). It also
  updates the 8-bit remap `DAT_00481620[0xc3..0xc6]`.

### World rendering (per player, `View_RenderWorld` 0x419dd0 = the player's view callback)
> Exact maths, model format, BNO→model tables and a reference renderer: see `docs/render.md`, `tools/view.py`, `tools/models.py`.
- **Camera struct** = the player struct at `0x48bd90` / `0x48bfdc` (0x24c bytes). Render-relevant fields:
  - `[0]` screen ctx; `[1]` player index; `[2],[3]` view w,h
  - `[4],[5]` screen centre (16.16); `[6],[7]` camera world x,y; `[8]` horizon/pitch base
  - `+0x24` pitch angle; `+0x28..+0x5c` 3x3 view matrix (fixed 16.16, from cos/sin of +0x24)
  - `+0x3c` matrix used for model transforms
  - `[0x2f]` intro zoom ramp; `[0x30]` view callback (starts as `View_ZoomInIntro` 0x404e80, becomes
    `View_RenderWorld` once the ramp reaches 1.0)
  - `+0x10c/+0x110` smoothed camera target; `+0x138/+0x13c` camera velocity
  - `+0x158` list of camera-behaviour nodes; `+0x1c4..+0x1d8` screen window
  - `Camera_Update` 0x403a40 (spring/velocity-limited follow via `Camera_StepAxis` 0x403ec0) runs first
    every frame.
- **Map**: `DAT_0045f3c0` holds 128x128 cells of 4 bytes, 32x32 world units per tile. Cell bits:
  - `&0x7f` = floor CCB index
  - `(>>7)&0x7f` = terrain/building type (table `0x451440`/`0x45146c`, stride 0x38)
  - `(>>14)&3` = variant
  - `(>>16)&0x1ff` = head of the object chain in the object array `0x47261c` (stride 0x74, next at +0x20)
- **Algorithm**: walk tile rows far → near (painter's order). For each row:
  - Compute the perspective scale from table `PTR_DAT_00440c30` = 0x483780: 0x600 entries of
    `0x12c0000/z`, i.e. focal ≈ 300, built by `InitProjectionAndRotTables` 0x408440.
  - Emit one floor CCB per visible cell with explicit trapezoid corners, mode 0xc (or 0x13 for types
    1,2,4..0x33).
  - Call `View_QueueCellContents` 0x419cf0. It queues the cell's building model and every object model on
    the cell's chain.
- **Models**: `View_QueueModel` 0x4085a0 takes a model def:
  - def fields: +0 draw fn, +4 next part, +0x10 flags, +0x14..+0x1c offset, +0x20 z-bias,
    +0x24/+0x28 hooks, +0x2c nverts, +0x30 verts, +0x34 nfaces, +0x38 faces (0x20 each), +0x3c
    per-orientation face orders.
  - It transforms the model origin by the camera matrix (`Mat_MulVec3` 0x42bb00) and computes a depth key.
  - It **insertion-sorts** the model into list `DAT_00456fd0`. The item pool is 0x483f80..0x48a780
    (512 x 0x34).
- `View_DrawQueuedModels` 0x408e50 then calls each item's def draw fn:
  - `Model_DrawStatic` 0x408840, `Model_DrawYaw` 0x408a20, `Model_DrawYawPitch` 0x408aa0,
    `Model_DrawTurret` 0x408b80, `Model_DrawTilted` 0x408d40.
  - Each builds a matrix from the 64-step yaw table `0x481780` (9 ints, stride 0x24) and pitch table
    `0x48a7b0` (`Mat_Mul3x3` 0x438460), then transforms the vertices (`Mat_TransformVerts` 0x4383c0).
  - `Model_EmitFaces` 0x4088a0 projects them (`ProjectVertices` 0x401290) and emits one CCB per face with
    explicit corners (`Cel_SetCornersFromVerts` 0x41fc00 → `Cel_SetQuadCorners` 0x426c10). Faces use
    back-face-style visibility tests on vertex pairs, or a precomputed per-orientation order.
  - So vehicles and buildings are **3D models made of textured quads (cels)**, not pre-rendered sprites.
    Shadows are separate faces/cels drawn with mode 1/2 (darken table).

### HUD / status bar / radar
- Status-bar background image (DirectDraw `BltFast` to the bottom of the back buffer), drawn by
  `DrawStatusBarBackground` 0x437f90 / `LoadStatusBarArt` 0x437e80:
  - `1pBScrL.rfa` (1P lo) / `1pBScrH` (1P hi) / `2pBScrL` / `2pBScrH`.
  - In 2P also `2pMScr.rfa` (24x299), the middle divider strip: lo-res source rect (16,0)-(24,150) at
    x = 0x9c, hi-res (0,0)-(16,299) at x = 0x138, at the top (and, without the status bar, also at the bottom);
    then the status bar over the bottom (2pBScrL 320x92 at y 148, 2pBScrH 640x183 at y 297: its first row is
    covered by the 149-row views). Colour indices +10 like the status bar.
  - 2P HUD blocks (HudInit(2)): origins (14,162) and (165,162) (alt layout (47,182) / (198,182)).
  - It is redrawn for 2 frames when dirty (`DAT_0045997c`), and `State_Game1P/2P` re-blit it for 6 frames
    after a mode change (`DAT_004410b0`).
- HUD widgets are cels. `Hud_Init` 0x422640 copies template `0x44abb8` into per-player HUD blocks
  (`0x472060` / `0x472168`, 0x108 bytes each):
  - +0/+4 dirty masks per buffer; +8/+0xc origin (16.16); +0x10 height
  - +0x14: array of 6-dword widgets `{drawFn, chainMask, celIdx, dx, dy, ...}`
  - `Hud_MarkDirty(hud,mask)` 0x422820. `Hud_Draw(nPlayers)` 0x422750 calls the drawFn of each dirty bit.
  - Widgets:
    - `Hud_DrawFrame` 0x4211d0
    - `Hud_DrawCelWidget` 0x421170
    - `Hud_DrawWeaponCounts` 0x421220 (digits via `Hud_DrawCelAt` 0x421690)
    - `Hud_DrawRadarWindow` 0x421d20 (plots radar pixels with `Bitmap_PlotPixel` 0x426c90, 2x2 in
      hi-res)
    - more in table 0x44acc4 (0x421730..0x422570, not named)
- **Radar**: `Radar_Update` 0x422fb0 keeps a 128x128 8-bit image `DAT_00440c64` (one pixel per map cell):
  - Default colour 0x87; terrain types use colours from table 0x45146c; 0xc9 = flag bit 31; 0x91 is set
    by `FUN_00418a30`.
  - Moving blips from object shape lists (`Radar_PlotBlip` 0x4233d0), special cells marked 0xd3
    (`Radar_MarkSpecialCells` 0x422ae0).
  - Pixel from level 1 of the dirty chain: `FUN_00432020(0,0xcf)` / `(1,0x6c)`.

### Other images
- `LoadBmpSurfaceCached` 0x4366f0 → `CreateImageFromDIB` 0x436560 → `DD_CreateSurfaceFromBits` 0x436400:
  a BMP/RFA becomes an off-screen DD surface and a palette, cached by filename.
- `ShowFullscreenImage` 0x436fb0 blits it (BltFast if same size, else stretch Blt) and schedules its
  palette. Used for:
  - `LoBck*/HiBck*` language back-screens
  - `PS240/PS480` pause screen (`ShowPauseScreen` 0x40e370, drawn 3x to fill both flip buffers)
  - title/win banners
- Windowed pause uses GDI with `PAUSE16.RFA` (`LoadDibFile` 0x41cea0, StretchDIBits in 0x40e1c0).
- `LoadRfaIndexed` 0x430160 loads a BMP into a flipped 8-bit buffer plus an HPALETTE (with
  `CreatePaletteFromBMI` 0x4302c0 and `SelectRealizePalette` 0x4302a0). Used for `2x2.RFA` = the 260x260
  level-select map preview in the GDI dialog 0x413460.
- `SMALL.RFA`, `NOWRL.RFA`, `FIX.RFA` (strings at 0x44f2e8–0x44f308) have **no xrefs** and look like
  leftovers.

### Per-frame render order (1P; see `State_Game1P` → 0x41a660)
1. Input, timers, object update.
2. `Radar_Update`, then `Pal_CycleWater`.
3. The player view callback runs (`View_ZoomInIntro` → `View_RenderWorld`):
   - `Camera_Update`, `View_ResetModelQueue`, `Cel_FlushList(ctx)`
   - floor rows with interleaved queued models
   - `View_DrawQueuedModels`, then flush
4. `Cel_FlushList(DAT_0048c260)` → `RunFrameTasks` (overlay tasks), then `Hud_MarkDirty(0x3f0)` for 10
   frames after resets.
5. `Hud_Draw` → `Cel_FlushList(0)`.
6. Music director.
7. Sound queue (`0x42cb40`/`0x42cff0` are the **sound** command queue, not rendering).
8. `FrameTiming` (toggles buffer index).
9. The main loop calls `PresentFrame(0)` (Flip/Blt + palette fade).

### Reimplementation notes
- Render into a 320x240 (or 640x480) 8-bit buffer with the same CCB semantics; hi-res = 2x coordinates.
  Alternatively render at 320x240 and scale up (visually identical except the HUD row-doubling path).
- A faithful look needs: the painter's order (rows far→near, then the depth-sorted model list),
  TRANS.TBL-style blend/shade tables (regenerate from the ART.CAR palette), PLUT water cycling, the
  palette-fade brightness model, the status-bar BMP under the HUD cels, and the 128x128 radar image.


## 9. Function index (current Ghidra names, game/engine code; CRT and import thunks omitted)

Module: platform / game / render / sound. Size = bytes of function body. Purpose blank = name is self-describing; see the section text.

| addr | name | module | size | purpose |
|---|---|---|---|---|
| 00401000 | TimerQueueAdd | game | 134 | delta-list timer add |
| 00401090 | TimerQueueAdvance | game | 197 | run expired timers |
| 004011a0 | RunCallbackList | game | 71 | per-frame callback list |
| 00401220 | Cel_AddToList | render | 101 |  |
| 00401290 | ProjectVertices | render | 173 | perspective project via 1/z table |
| 00401340 | ReadPlayerInput | platform | 687 |  |
| 004015f0 | InitJoysticks | platform | 409 |  |
| 00401790 | ReadKeyboardBindings | platform | 1208 |  |
| 00401d90 | LoadJoystickConfig | platform | 845 |  |
| 004020e0 | LoadKeyboardConfig | platform | 471 |  |
| 004022c0 | RegLoadInputConfig | platform | 384 |  |
| 00402440 | DefaultKeyBindings | platform | 228 |  |
| 00402530 | DefaultJoyBindings | platform | 178 |  |
| 004025f0 | JoyInputActive | platform | 1033 |  |
| 00402a60 | NumpadToNavKey | platform | 102 |  |
| 00402c70 | ViewAddTracker | game | 277 | per-view marker/indicator (tentative) |
| 00403550 | Camera_SetScreenWindow | render | 501 | screen centre and window extents |
| 00403750 | ViewInit | render | 739 | init player camera/view struct |
| 00403a40 | Camera_Update | render | 1145 | per-frame follow/smoothing, matrix rebuild |
| 00403ec0 | Camera_StepAxis | render | 210 | rate-limited approach of one camera axis |
| 00404570 | ViewModeBunkerSelect | game | 641 | bunker select view mode |
| 00404d30 | ViewEnterBunkerSelect | game | 246 | enter vehicle-select mode |
| 00404e80 | ViewModeZoomIn | render | 88 | level-start fade/zoom ramp, then View_RenderWorld |
| 00405740 | InitApplication | platform | 740 | full init sequence |
| 00405a30 | NextCmdLineToken | platform | 135 | tokenizer |
| 00405ac0 | ParseCommandLine | platform | 916 | options -2 -a -c -f -j -z -l -m -r -w |
| 00405f30 | CreateMainWindow | platform | 447 | RegisterClass "ReturnFire", CreateWindowEx, menu 101 |
| 004060f0 | ReinitInputDevices | platform | 25 |  |
| 00406110 | SetMusicEnabled | platform | 108 |  |
| 004062c0 | ManInit | game | 79 |  |
| 00406310 | ManDestroy | game | 6 |  |
| 00406320 | ManUpdate | game | 371 |  |
| 004071c0 | SpawnMan | game | 34 | spawn soldier |
| 004071f0 | SpawnMenFromBuilding | game | 292 | men from destroyed building |
| 00407350 | SubInit | game | 34 |  |
| 00407380 | SubDestroy | game | 32 |  |
| 004073a0 | Sub_UpdateSetsMusicFlag | sound | 205 | SUB object update; sets music bit 0x1000 |
| 004076f0 | SpawnSub | game | 38 | create sub |
| 00407720 | Wav_OpenRiff | sound | 408 | mmio open + find fmt |
| 004078c0 | Wav_ReadData | sound | 216 | mmio read data chunk |
| 004079a0 | Wav_Close | sound | 65 | close wav |
| 004079f0 | Wav_LoadFile | sound | 272 | load RIFF WAV (fmt+data) via mmio |
| 00407b90 | Cel_SetupFullscreenFade | render | 103 | full-screen mode-8 shade cel |
| 00407c00 | AngleBetweenPoints | game | 45 | heading from point to point |
| 00407e60 | ShowDDrawError | platform | 59 | HRESULT → text table 0x440648 |
| 00407f30 | InitDirectDraw | platform | 249 |  |
| 00408030 | SetDisplayMode | platform | 950 | create surfaces per mode |
| 00408440 | InitProjectionAndRotTables | render | 319 | 1/z table, 64 yaw/pitch matrices |
| 00408580 | View_ResetModelQueue | render | 24 | reset item pool/list |
| 004085a0 | View_QueueModel | render | 470 | transform, depth-key and sorted insert of a model item |
| 00408840 | Model_DrawStatic | render | 94 | model draw, camera matrix only |
| 004088a0 | Model_EmitFaces | render | 373 | emit one CCB per visible model face |
| 00408a20 | Model_DrawYaw | render | 125 | model draw with yaw |
| 00408aa0 | Model_DrawYawPitch | render | 220 | yaw+pitch |
| 00408b80 | Model_DrawTurret | render | 433 | two-angle (hull+turret) matrix |
| 00408d40 | Model_DrawTilted | render | 269 | yaw/pitch plus fixed tilt |
| 00408e50 | View_DrawQueuedModels | render | 40 | draw sorted model items |
| 00408e80 | LoadFileToGlobal | render | 172 | read file into GlobalAlloc |
| 00408f30 | Plut_BuildFadeRamp | render | 303 |  |
| 00409060 | Pal_FreeTransTables | render | 93 |  |
| 004090c0 | Pal_InitTransTables | render | 1178 |  |
| 00409560 | Car_Relocate | render | 131 |  |
| 004095f0 | Car_Load | render | 701 |  |
| 00409e10 | Dbg_SoundEditorLoop | sound | 1495 | debug sound-event editor loop |
| 0040a420 | Dbg_SoundEditorDrawField | sound | 621 | debug editor field draw |
| 0040b680 | CaptureSystemPalette | platform | 130 | GDI palette → master palette |
| 0040b710 | StartNewGame | platform | 670 | load world and init game |
| 0040b9b0 | InitSoundAndGameWindows | platform | 176 | DSound, SFX, 1P/2P window init |
| 0040ba60 | EnableGameMenus | platform | 164 | phase 2 |
| 0040bb10 | DisableMenusForIntro | platform | 164 | phase 1 |
| 0040bbc0 | InitialFadeAndPresent | platform | 88 |  |
| 0040bc20 | WinMain | platform | 850 | lang DLL, music thread, single instance, message/idle loop |
| 0040bf80 | MainWndProc | platform | 3017 | WM_PAINT/CREATE/DESTROY/CLOSE/ACTIVATEAPP/SYSCOMMAND/menu loop/palette |
| 0040cbb0 | OnMenuCommand | platform | 4040 | WM_COMMAND handler |
| 0040dcf0 | DestroyMainWindowFail | platform | 137 |  |
| 0040dd80 | RestoreLostSurfaces | platform | 649 |  |
| 0040e060 | RedrawAfterModeChange | render | 303 | force HUD/status redraw or pause image after mode switch |
| 0040e1c0 | PaintBackScreenGDI | platform | 207 | StretchDIBits fallback |
| 0040e2d0 | CenterWindowOnDesktop | platform | 145 |  |
| 0040e370 | ShowPauseScreen | render | 197 | PS240/PS480 pause image |
| 0040e470 | SetupViewports | platform | 1046 | player view rectangles |
| 0040e930 | NewGameDialogAndStart | platform | 534 | New Game |
| 0040eb70 | State_IntroPlaying | platform | 90 | Steps the intro script (`RunScriptStep`). When `DAT_00455990==0`, sets fade vars and goes to Game1P/Game2P (the demo game runs behind the back-screen), or to a queued state. |
| 0040ebd0 | State_Game1P | platform | 123 | Not paused: fade in to 0x10000 over 500 ms, redraw the status bar (`DrawStatusBarBackground`) for 6 frames, music ping, then the game tick `FUN_0041a660`. |
| 0040ec50 | State_Game2P | platform | 146 | Same, with the 2-player tick `FUN_0041aa70`. |
| 0040ecf0 | State_StartGameFade | platform | 112 | Fade out 500 ms. When black: `SetupGame`, `SetupViewports(1)`, fade in, go to Game1P/2P. |
| 0040ed60 | State_Idle | platform | 1 | Empty (used while minimized). |
| 0040ed70 | State_BackScreen | platform | 399 | Title/menu backdrop; waits with `WaitMessage`. After a finished game (`DAT_004410d0`): `RecordHighScore(winner)` and the level-progress registry update (`FUN_00416330`, `FUN_00415330`). May auto-post "New Game" (0x1e64). A left click in the bottom 1/16 of the client area also posts New Game. |
| 0040ef00 | State_EnterBackScreen | platform | 253 | Draws the language back-screen `\Art\{Hi,Lo}BckXxx.RFA` (or `ART\PS480/PS240.RFA` in windowed pause), stops game sound, starts music track 0x11 (Drums, prio 0x32), then goes to `State_BackScreen`. |
| 0040f050 | State_EndOfGameSequence | platform | 774 | Sub-state `DAT_004410c0`, see below. |
| 0040f380 | EndGame | platform | 61 | called by game logic with the winner |
| 0040f3c0 | ApplyDisplayModeSetting | platform | 756 | switch between modes 1/2/5/6 |
| 0040f6d0 | ConfigDlgProc | platform | 1108 | Config dialog 0x47c |
| 0040fb30 | Span_Opaque | render | 403 | quad span: copy |
| 0040fcc3 | Span_Transparent | render | 396 | quad span: copy, skip 0 |
| 0040fe4f | Cel_DrawTrapezoid | render | 721 | floor-tile trapezoid rasteriser (mode 0xc) |
| 00410120 | Cel_ScanEdge | render | 949 | edge walker for Cel_DrawQuad |
| 004104d5 | Cel_TexSpanRows | render | 1697 |  |
| 00410b9c | Cel_DrawQuad | render | 1106 |  |
| 0041100f | Cel_BlitUnscaledE | render | 574 | 1:1 blit (mode 0xe) |
| 0041124d | Cel_BlitUnscaled2xH | render | 977 | 1:1 blit, doubled in hi-res (mode 6, HUD) |
| 00411628 | Cel_FillRect | render | 324 | solid rect (mode 7/9/10) |
| 0041176c | Cel_ShadeRect | render | 334 | rect through shade table (mode 8) |
| 004118ba | Cel_BlitUnscaledB | render | 747 | 1:1 clipped blit (mode 0xb/0xf) |
| 00411ba5 | Cel_DrawShadowSpans | render | 624 |  |
| 00411e20 | Snd_InitDirectSound | sound | 952 | DirectSoundCreate, priority coop, primary buffer 44.1k/16/stereo |
| 004121e0 | Snd_ShutdownDirectSound | sound | 391 | release all DS buffers/objects |
| 00412370 | ErrorBoxId | platform | 520 | MessageBox with a string ID |
| 004125a0 | ErrorBoxId2 | platform | 501 |  |
| 004127c0 | ConfirmBoxId | platform | 540 |  |
| 00412a00 | ErrorBoxIdFmt | platform | 536 |  |
| 00412c40 | NewGameDialogs | platform | 187 |  |
| 00413460 | LevelSelectDlgProc | platform | 2157 |  |
| 00413cd0 | PlayerNamesDlgProc | platform | 1081 |  |
| 00414f50 | RegScanWorldDir | platform | 521 |  |
| 00416e10 | RegScanWorldDirForPlayers | platform | 882 |  |
| 00417190 | FindWorldsInInstallPath | platform | 511 |  |
| 004177b0 | CellToPixelPos | game | 48 | cell pointer → px |
| 004177e0 | CellToWorldPos | game | 59 | cell pointer → 16.16 centre |
| 00417850 | SetCellStaticObject | game | 264 | put BNO into cell |
| 00417a00 | BnoDestroy | game | 326 | destroy static object, spawn explosion/replacement |
| 00417f50 | StorageInit | game | 94 |  |
| 00417fb0 | StorageUpdate | game | 452 |  |
| 00418180 | StorageDestroy | game | 37 |  |
| 00418470 | DockVehicleInBunker | game | 364 | return vehicle to bunker |
| 004185e0 | GetObjWaterState | game | 385 | land/water/shore test |
| 00418a90 | GetCellWaterType | game | 67 | terrain water class |
| 00418b30 | Snd_InstancePriorityCmp | sound | 55 | priority comparator |
| 00418b70 | Snd_ResortInstances | sound | 250 | re-sort active instances by priority |
| 00418c70 | Snd_BindInstanceToVoice | sound | 151 | bind instance to voice (sample select) |
| 00418d10 | Snd_AssignVoices | sound | 280 | assign hardware voices to top instances |
| 00418e30 | Snd_FreeInstance | sound | 84 | return instance to free pool |
| 00418e90 | Snd_MixAndUpdateVoices | sound | 393 | compute L/R budgets, set volume/pan, start voices |
| 00419020 | Snd_AttachInstanceToOwner | sound | 209 | attach/detach instance to owner object |
| 00419100 | Snd_DetachAllFromOwner | sound | 37 | detach all sounds from an object (on delete) |
| 00419140 | Snd_CreateInstance | sound | 313 | create sound instance from event |
| 00419280 | Snd_CalcGainTwoListeners | sound | 402 | gain from distance to listener1 (L) / listener2 (R) |
| 00419420 | Snd_CalcGainStereoOwner | sound | 327 | gain variant using owner-provided L/R |
| 00419570 | Snd_ProcessCommandQueue | sound | 72 | dispatch queued commands via 0x443758 |
| 004195c0 | SndCmd0_Sync | sound | 6 | cmd0 |
| 004195d0 | SndCmd1_Play | sound | 46 | cmd1 play event |
| 00419600 | SndCmd2_Refresh | sound | 21 | cmd2 refresh |
| 00419620 | SndCmd3_Stop | sound | 63 | cmd3 stop |
| 00419660 | SndCmd4_Update | sound | 112 | cmd4 update |
| 004196d0 | SndCmd5_OwnerCallbacks | sound | 111 | cmd5 owner callbacks |
| 00419740 | SndCmd6_SetListener1Pos | sound | 63 | cmd6 listener1 pos |
| 00419780 | SndCmd7_SetListener2Pos | sound | 63 | cmd7 listener2 pos |
| 004197c0 | SndCmd8_SetListenerGain | sound | 43 | cmd8 listener gain ptr |
| 004197f0 | SndCmd9_KillByHandle | sound | 119 | cmd9 kill by handle |
| 00419870 | SndCmd10_StopAll | sound | 216 | cmd10 stop all |
| 00419950 | Snd_ResetInstanceLists | sound | 27 | reset lists |
| 00419970 | Snd_InitMixer | sound | 170 | init 20 voices, 15 instances, budgets |
| 00419ac0 | Pal_InitWaterCycle | render | 135 | build PLUT rotation tables |
| 00419b50 | Pal_CycleWater | render | 319 | animate PLUT and remap indices 0xc3–0xc6 |
| 00419c90 | thunk_FUN_004279e0 |  | 5 |  |
| 00419ca0 | View_SetClipHeight | render | 67 | clip view bitmaps above HUD |
| 00419cf0 | View_QueueCellContents | render | 215 | queue building and objects on a map cell |
| 00419dd0 | RenderWorldView | render | 1373 | per-player world render (tiles + models) |
| 0041a330 | GameInit | game | 331 | per-game init |
| 0041a480 | GameSetup1P | render | 471 | 1P viewport, camera, HUD init |
| 0041a660 | GameFrame1P | game | 234 | per-frame update 1P |
| 0041a750 | GameSetup2P | render | 793 | 2P split-screen viewports, cameras, HUDs |
| 0041aa70 | GameFrame2P | game | 252 | per-frame update 2P |
| 0041abb0 | SysInfoDlgProc | platform | 156 |  |
| 0041ac50 | FillSysInfo | platform | 2187 |  |
| 0041b510 | ShowSysInfoDialog | platform | 31 |  |
| 0041b530 | RegReadDisplayType | platform | 305 |  |
| 0041b670 | RegReadCDMusic | platform | 218 |  |
| 0041b750 | RegCreateDefaultConfig | platform | 369 |  |
| 0041c800 | ShapesOverlap | game | 294 | oriented shape overlap test |
| 0041cea0 | LoadDibFile | render | 313 | load DIB (PAUSE16, EM1) |
| 0041d270 | MusTrans0_FadeOutRestart | sound | 91 | transition 0 |
| 0041d2d0 | MusTrans1_ContinueSeamless | sound | 40 | transition 1 |
| 0041d300 | MusTrans2_CutRestart | sound | 33 | transition 2 |
| 0041d330 | MusTrans3_Revert | sound | 31 | transition 3 |
| 0041d350 | Mus_Service | sound | 475 | music service per frame (transitions, driver cmd 8) |
| 0041d530 | Mus_Request | sound | 211 | music request (track, prio, owner) |
| 0041d610 | Mus_ClearOwner | sound | 66 | clear owner |
| 0041d660 | Mus_PickVehicleTrack | sound | 208 | choose tank/heli/other vehicle track |
| 0041d730 | Mus_Director | sound | 567 | music director (event bits -> tracks) |
| 0041d970 | Mus_OnTrackEnd | sound | 326 | end-of-track handling (loop / return / fallback) |
| 0041dac0 | CollideWithCellContents | game | 556 | vs static BNO and cell objects |
| 0041dcf0 | ObjCheckCollision | game | 486 | collision vs map/objects |
| 0041dee0 | CollideWithCell | game | 272 | per-cell collision |
| 0041e0a0 | ObjDestroyNow | game | 346 | immediate destroy |
| 0041e240 | ObjCreate | game | 570 | allocate object from pool, class init |
| 0041e480 | ObjMarkForDelete | game | 39 | queue object for deletion |
| 0041e640 | ObjUnlinkFromCell | game | 140 | remove from cell list |
| 0041e6d0 | ObjLinkToCell | game | 266 | insert into map cell object list |
| 0041e7e0 | ObjMove | game | 254 | move by delta with collision/cell relink |
| 0041e970 | ObjUpdateAll | game | 645 | run class update on all active objects, reap deletes |
| 0041ec00 | ObjSetParent | game | 176 | attach/detach child (carry) |
| 0041ecb0 | ObjDebugCheckLinks | game | 48 | no-op link walk |
| 0041ed50 | DistSqPixels | game | 36 | squared distance in px |
| 0041ed80 | ApproachValue | game | 37 | move a value toward a target by step |
| 0041edf0 | ControllerDecodeDigital | game | 233 | raw input → control word |
| 0041eee0 | ControllerDecodeAnalog | game | 618 | alternative decoder (analog) |
| 0041f150 | VehicleSetController | game | 57 | choose input decoder |
| 0041fc00 | Cel_SetCornersFromVerts | render | 25 | wrapper → Cel_SetQuadCorners |
| 0041fc20 | Cel_DrawToBitmap | render | 46 | lock back buffer, run `Cel_DrawList` on a CCB list for a bitmap, unlock |
| 004204c0 | ExplInit | game | 61 |  |
| 00420500 | ExplUpdate | game | 181 |  |
| 004205c0 | ExplDestroy | game | 37 |  |
| 004209a0 | SpawnExplosion | game | 45 | create Expl |
| 00420a60 | MusDrv0_Init | sound | 39 | drv0 |
| 00420a90 | MusDrv1_Open | sound | 23 | drv1 open |
| 00420ea0 | MusDrv3_Play | sound | 49 | drv3 play current |
| 00420ee0 | MusDrv4_Stop | sound | 13 | drv4 stop |
| 00420ef0 | MusDrv5_SetVolumeNow | sound | 39 | drv5 volume now |
| 00420f20 | MusDrv6_SetVolumeTarget | sound | 34 | drv6 volume target |
| 00420f50 | MusDrv7_SetPlaying | sound | 13 | drv7 |
| 00420f60 | MusDrv8_PlayWithParams | sound | 220 | drv8 play with params |
| 00421040 | MusDrv9_StopMci | sound | 28 | drv9 stop |
| 00421060 | MusDrv10_Nudge | sound | 42 | drv10 |
| 00421090 | MusDrv_Command | sound | 36 | music driver dispatch 0x44aab8 |
| 004210c0 | Mus_VolumeFadeTick | sound | 175 | music volume fade tick |
| 00421170 | Hud_DrawCelWidget | render | 95 | draw a HUD cel widget |
| 004211d0 | Hud_DrawFrame | render | 77 | HUD frame cel |
| 00421220 | Hud_DrawWeaponCounts | render | 1135 | ammo/weapon digits |
| 00421690 | Hud_DrawCelAt | render | 89 | one HUD cel at offset |
| 00421d20 | Hud_DrawRadarWindow | render | 288 | radar window around player |
| 00422640 | HudInit | render | 266 | init HUD blocks from template 0x44abb8 |
| 00422750 | HudUpdateAll | render | 208 | draw dirty HUD widgets |
| 00422820 | HudMarkDirty | render | 18 | OR mask into both buffer dirty words |
| 00422840 | HudSetSlot | game | 158 | configure HUD gauge slot |
| 00422930 | RadarUpdateCell | game | 161 | recolour radar pixel for a cell |
| 00422ae0 | Radar_MarkSpecialCells | render | 221 | mark cells 0xd3 |
| 00422fb0 | RadarUpdateMovers | render | 1050 | update 128x128 radar image |
| 004233d0 | Radar_PlotBlip | render | 132 | plot object shape on radar |
| 00423460 | TurnTowardsAngle | game | 163 | rotate heading toward target at rate·dt |
| 00423510 | TowerDestroyed | game | 137 | tower debris burst |
| 004235a0 | TurretInit | game | 113 |  |
| 00423620 | TurretUpdate | game | 58 |  |
| 00423660 | TurretDestroy | game | 172 |  |
| 00423c10 | ActivateTurret | game | 190 | tower cell → Turret object |
| 00423cd0 | GateInit | game | 118 |  |
| 00423d50 | GateUpdate | game | 358 |  |
| 00423ec0 | GateDestroy | game | 131 |  |
| 00423fb0 | SpawnGate | game | 174 | create gate object |
| 00424060 | HideFlagInRandomBuilding | game | 271 | choose flag building |
| 00424170 | FlagBuildingDestroyed | game | 418 | spawn flag when its building dies |
| 00424320 | FlagInit | game | 47 |  |
| 00424350 | FlagDestroy | game | 34 |  |
| 00424380 | FlagUpdate | game | 979 |  |
| 00424760 | FlagOnTouch | game | 120 | jeep picks up flag |
| 004247e0 | DestFlagOnTouch | game | 190 | same, from destroyed-flag BNO |
| 004260a0 | Cel_ComputeCorners | render | 217 |  |
| 00426180 | Cel_ComputeCorners2x | render | 217 |  |
| 00426260 | Cel_ExplicitCornersToScreen | render | 135 |  |
| 004262f0 | Cel_DrawList | render | 2252 |  |
| 00426c10 | Cel_SetQuadCorners | render | 116 |  |
| 00426c90 | Bitmap_PlotPixel | render | 125 | plot one pixel (2x2 in hi-res) |
| 00426d10 | Span_ShadeDst | render | 497 | dst=tbl[dst] where src≠0 (shadow/glow) |
| 00426f10 | Span_Blend | render | 506 | dst=blend[src][dst] |
| 00427110 | Span_BlendKeyed | render | 521 | blend except 4 key colours |
| 00427320 | Span_RemapSrc | render | 487 | dst=tbl[src] (src≠0) |
| 00427510 | Span_RemapBlend | render | 484 | dst=blend[tbl[src]][dst] |
| 00427700 | Span_RemapOpaque | render | 466 | dst=tbl[src] |
| 004279f0 | GetTicks16ms | platform | 16 |  |
| 00427a00 | Screen_InitContexts | render | 473 | create the 2 screen contexts 0x471580/0x4715a4 |
| 00427be0 | Screen_FreeContexts | render | 162 | free them and the view bitmaps |
| 00427cd0 | SpawnVehicle | game | 151 | create Vehicle object |
| 00427e80 | TeamWins | game | 17 | wrapper for EndGame |
| 00427ed0 | GetTeamVehicle | game | 51 | current vehicle of a team |
| 00427f10 | BunkerDoorAnimate | game | 203 | bunker pad animation |
| 00428020 | SpawnLiftVehicle | game | 136 | create rising Storage lift |
| 00428210 | VehicleInit | game | 421 |  |
| 004283c0 | VehicleDestroy | game | 206 |  |
| 00428490 | VehicleUpdate | game | 1958 |  |
| 004292d0 | WreckInit | game | 264 |  |
| 004293e0 | WreckUpdate | game | 648 |  |
| 004296c0 | WreckDestroy | game | 6 |  |
| 00429d30 | TankFireCannon | game | 493 | tank weapon |
| 0042a480 | JeepFlagCheck | game | 485 | jeep flag-carry win + beacon |
| 0042b960 | SndCb_JeepEnginePitch | sound | 78 | jeep engine pitch from speed |
| 0042b9b0 | SndCb_HeliRotorPitch | sound | 73 | heli rotor pitch (function created) |
| 0042ba00 | Item_CreateBitmap | render | 79 | 3DO CreateItem(0x203 BITMAP, tags) shim |
| 0042ba50 | Item_Free | render | 28 | free item |
| 0042bb00 | VecMulMat3 | render | 158 | 3x3 * vec |
| 0042bba0 | GetGameClock | platform | 54 | op 7 = read the 16 ms clock, op 3 = cached value |
| 0042bc00 | Bitmap_SetClipHeight | render | 21 | bitmap+0x3c |
| 0042bc20 | Bitmap_SetClipOrigin | render | 27 | bitmap+0x40/0x44 |
| 0042bc40 | Bitmap_SetClipWidth | render | 21 | bitmap+0x38 |
| 0042bc80 | MusStream_StopBuffer | sound | 34 | stop stream buffer |
| 0042bcb0 | MusStream_SetVolume | sound | 56 | stream volume |
| 0042bcf0 | MusStream_Play | sound | 514 | play stream buffer |
| 0042bf00 | MusStream_Pump | sound | 383 | stream pump (200 ms) |
| 0042c080 | MusStream_ReadChunk | sound | 291 | read chunk (pad silence) |
| 0042c1b0 | MusStream_FillBuffer | sound | 249 | lock+fill ring buffer |
| 0042c2b0 | MusStream_Close | sound | 101 | close stream |
| 0042c320 | MusStream_Open | sound | 1118 | open AVI audio stream + DS stream buffer |
| 0042c7f0 | LoadLanguageDll | platform | 270 |  |
| 0042c930 | Snd_VoicePlay | sound | 525 | duplicate buffer, set pan/vol, play |
| 0042cb40 | Snd_QueueCommand | sound | 114 | queue sound command (cmd,a,b,flush) |
| 0042cbc0 | Snd_LoadAllSamples | sound | 966 | load 37 SDT samples into static DS buffers |
| 0042cf90 | Snd_FreeAllSamples | sound | 75 | free sample records |
| 0042cfe0 | Snd_VoiceSelectSample | sound | 9 | identity stub |
| 0042cff0 | Snd_Service | sound | 363 | per-frame sound service (drain queue, resort, assign voices, mix) |
| 0042d2b0 | Snd_VoicePlayWrap | sound | 34 | wrapper |
| 0042d2e0 | Snd_VoiceStop | sound | 142 | stop voice, free instance if one-shot |
| 0042d370 | Snd_VoiceSetParam | sound | 308 | set voice volume(1)/freq(2)/pan(3); ch -1 = music volume |
| 0042d4b0 | Snd_GetPrimaryFormat | sound | 60 | primary GetFormat |
| 0042d4f0 | Snd_SetPrimaryParam | sound | 98 | primary param set |
| 0042d560 | RecordWriteDword | platform | 38 |  |
| 0042d590 | RecordReadDword | platform | 53 |  |
| 0042d650 | OpenReplayFile | platform | 78 |  |
| 0042d6a0 | StartRecording | platform | 103 |  |
| 0042d710 | WriteWholeFile | platform | 208 |  |
| 0042d7e0 | SaveRecording | platform | 93 |  |
| 0042d840 | GameClockStart | platform | 22 |  |
| 0042d860 | GameClockStop | platform | 24 |  |
| 0042d880 | GameClockPause | platform | 12 |  |
| 0042d890 | GameClockResume | platform | 19 |  |
| 0042d8b0 | RecordHighScore | platform | 1904 |  |
| 0042ea20 | HighScoresDlgProc | platform | 1111 |  |
| 0042ee80 | ShowHighScoresDialog | platform | 241 |  |
| 0042fc80 | InitInputDevices | platform | 175 |  |
| 0042fd30 | PollAllPlayerInputs | platform | 154 |  |
| 0042fe40 | PollAllPlayerInputs |  | 11 |  |
| 00430160 | LoadRfaIndexed | render | 316 | BMP → 8-bit buffer + HPALETTE |
| 004302a0 | SelectRealizePalette | render | 31 | GDI select+realize |
| 004302c0 | CreatePaletteFromBMI | render | 129 | HPALETTE from BMP colour table |
| 00430350 | LoadSmallFile | render | 358 | read ≤32000-byte file |
| 004305d0 | Mus_MciClose | sound | 49 | close MCI |
| 00430610 | Mus_CheckScoreWavExists | sound | 59 | check SOUND\Score.WAV exists |
| 00430650 | Mus_StartTrack | sound | 408 | start track (stream or MCI) |
| 004307f0 | Mus_ShutdownThread | sound | 274 | stop thread + stream |
| 00430910 | Mus_ThreadProc | sound | 265 | music thread |
| 00430a20 | Mus_ThreadRequestTrack | platform | 54 | set the track and signal the music thread |
| 00430a60 | Mus_Stop | sound | 140 | stop music |
| 00430af0 | Mus_Disable | sound | 30 | music off |
| 00430b10 | Mus_Enable | sound | 15 | music on |
| 00430b20 | Mus_IsPlaying | sound | 6 | is playing |
| 00430b30 | Mus_StartWinMusic | sound | 116 | win music |
| 00430bb0 | ProjectileInit | game | 436 |  |
| 00430d70 | ProjectileDestroy | game | 85 |  |
| 00430dd0 | ProjectileUpdate | game | 723 |  |
| 004313e0 | DeathMissileUpdate | game | 794 | Death Missle |
| 00431760 | FireProjectile | game | 281 | spawn projectile by type |
| 004319c0 | GrenadeInit | game | 6 |  |
| 004319d0 | GrenadeDestroy | game | 55 |  |
| 00431a10 | GrenadeUpdate | game | 440 |  |
| 00431c80 | ThrowGrenade | game | 346 | spawn grenade |
| 004322f0 | LoadLevelMap | platform | 1449 | RFM load (format handled by another agent) |
| 004328a0 | ShowAboutDialog | platform | 64 |  |
| 004328e0 | AboutDlgProc | platform | 149 |  |
| 00433010 | DD_SetDisplayMode | platform | 44 |  |
| 00433060 | DD_RestoreDisplayModeTwice | platform | 26 |  |
| 00433080 | DD_LockBackBuffer | render | 248 | Lock back buffer → DAT_00472288 ptr / DAT_0047228c pitch |
| 00433180 | DD_UnlockBackBuffer | render | 44 | Unlock |
| 00433220 | FrameTimingUpdate | platform | 164 | frame delta DAT_0045f3a8 |
| 004332d0 | InitTimeScaleTable | game | 164 | delta table / clamp |
| 00433380 | Screen_FreeViewBitmaps | render | 106 | free |
| 004333f0 | Screen_CreateViewBitmaps | render | 378 | per-player viewport bitmaps (x,y,w,h) |
| 004335f0 | RandRange | game | 40 | rand in [0,n) |
| 004336c0 | Cel_SetScale | render | 109 |  |
| 00433730 | Cel_FlushList | render | 147 |  |
| 004338c0 | Obj_SetCel |  | 402 |  |
| 004344b0 | DebrisInit | game | 112 |  |
| 00434520 | DebrisDestroy | game | 37 |  |
| 00434550 | DebrisUpdate | game | 278 |  |
| 00434670 | SpawnDebris | game | 302 | single piece |
| 004347a0 | SpawnDebrisBurst | game | 1002 | burst of debris |
| 00434b90 | HeapInit | platform | 27 |  |
| 00434bb0 | HeapShutdown | platform | 27 |  |
| 00434cf0 | ShadowInit | game | 122 | shadow class |
| 00434e40 | MineInit | game | 45 |  |
| 00434e70 | MineUpdate | game | 175 |  |
| 00434f20 | MineDestroy | game | 75 |  |
| 00434fd0 | SpawnMine | game | 110 | lay mine |
| 00435140 | DroneInit | game | 105 |  |
| 004351b0 | DroneDestroy | game | 270 |  |
| 004352f0 | DroneUpdate | game | 291 |  |
| 00435950 | SpawnDrone | game | 722 | anti-camping drone |
| 00435da0 | StayInit | game | 6 |  |
| 00435db0 | StayDestroy | game | 6 |  |
| 00435dc0 | StayUpdate | game | 1 |  |
| 00435eb0 | SpawnStay | game | 109 | create remains |
| 00435f20 | GetSystemPaletteToBMI | platform | 132 |  |
| 00435fb0 | MuteMusicVolume | platform | 47 |  |
| 00435fe0 | RestoreMusicVolume | platform | 65 |  |
| 00436030 | TogglePause | platform | 304 |  |
| 00436160 | BeginModalDialog | platform | 88 |  |
| 004361c0 | EndModalDialog | platform | 94 |  |
| 00436400 | DD_CreateSurfaceFromBits | render | 293 | create off-screen surface, copy pixels |
| 00436560 | CreateImageFromDIB | render | 389 | image record from DIB |
| 004366f0 | LoadBmpSurfaceCached | render | 253 | BMP/RFA → DD surface (cached) |
| 00436890 | SetFadeTarget | platform | 115 |  |
| 00436970 | UpdatePaletteFade | platform | 553 | subtractive fade, SetEntries |
| 00436ba0 | PresentFrame | platform | 594 | Flip / Blt |
| 00436e00 | ClearBackBuffer | platform | 178 |  |
| 00436ec0 | ClearAndPresent | platform | 25 |  |
| 00436fb0 | ShowFullscreenImage | platform | 296 | RFA → back buffer and palette |
| 004370e0 | SetWinnerAndWinVideo | platform | 55 |  |
| 00437120 | LoadWinBanners | platform | 65 |  |
| 00437170 | DrawWinBanner | platform | 440 |  |
| 00437330 | ShowWinBannerWithPalette | platform | 321 |  |
| 00437480 | AbortIntroScript | platform | 327 |  |
| 004375d0 | Script_WinBannerAndVideo | platform | 645 |  |
| 00437860 | Script_FadeOut | platform | 122 |  |
| 004378e0 | Script_ShowImage | platform | 399 |  |
| 00437a70 | Script_PlayVideo | platform | 353 |  |
| 00437be0 | Script_End | platform | 405 |  |
| 00437d80 | RunScriptStep | platform | 84 |  |
| 00437de0 | StartIntroScript | platform | 21 |  |
| 00437e00 | SkipIntroScript | platform | 40 |  |
| 00437e30 | StartWinScript | platform | 44 |  |
| 00437e80 | LoadStatusBarArt | platform | 268 | 1pBScr/2pBScr/2pMScr |
| 00437f90 | DrawStatusBarBackground | platform | 361 |  |
| 004382e0 | thunk_FUN_00426080 |  | 5 |  |
| 00438300 | FixMul | render | 9 | 16.16 multiply |
| 00438310 | Atan2Fixed | game | 78 | fpatan |
| 00438360 | CosFixed | game | 38 | cos, 0x1000000 = 360° |
| 004383c0 | Mat_TransformVerts | render | 156 | transform n verts by 3x3 |
| 00438460 | Mat_Mul3x3 | render | 192 | 3x3 multiply |
| 00438520 | SinFixed | game | 38 | sin |
