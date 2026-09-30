# OpenRF — native macOS reimplementation of Return Fire (Win95, 1996, Silent Software)

> Start with [`AGENTS.md`](../AGENTS.md) at the repository root (rules, build/test, traps); this file keeps the original per-format working notes.

- Original disc extracted at `cd/` (read-only reference; never modify).
- Game binary: `cd/RFIRE.BIN` (PE32 x86, DirectDraw/DirectSound). `cd/RFIRE.EXE` is only a language launcher.
- Ghidra project: `returnfire`, program `rfire_game.exe`. CLI: `ghidra decompile 0xADDR --project returnfire --program rfire_game.exe`,
  `ghidra x-ref ...`, `ghidra strings ...`, `ghidra disasm ...`. Full decompile of all functions: `re/all.c` (grep it).
- Image base likely 0x400000; to map file offsets use the PE section table (python `pefile` not guaranteed; parse manually).

## Known formats
- `.RFA`, `TITLE/*.BMP`: plain 8-bit Windows BMP (256-colour palette, bottom-up).
- `.SDT`, `SOUND/*.WAV`: plain RIFF WAV. `SCORE.WAV` = 21 min 44.1kHz stereo music.
- `.STM`: pre-interleaved AVI (Cinepak 320x240 15fps + PCM 22050 stereo) in 64 KB super-blocks. DONE: see `docs/stm.md`, `tools/stm.py`.
- `ART/ART.CAR`: magic `CCBA`, sprite archive. TBD.
- `WORLDS/{1,2}PLAYER/LEVELn/RFMAP00n.RFM`: level maps (~16 KB). TBD.
- `ART/TRANS.TBL`: 81924 bytes, colour translation tables (4-byte header + 81920). TBD.

## Output conventions
- Python decoders go in `tools/` (python3 + PIL available; capstone available).
- Decoded samples go in `out/<topic>/` (PNG/WAV/etc).
- Findings go in `docs/<topic>.md` with exact byte layouts and the Ghidra function addresses that read them.

## Engine map
- `docs/architecture.md`: module map, main loop/timing, entity/renderer/sound/music details, function index.
- `re/all_named.c`: fresh decompile (897 functions, current Ghidra names); regenerate with `re/DumpAllNamed.java` via `ghidra script run`.
- World view renderer / models: DONE, see `docs/render.md`, `tools/view.py`, `tools/models.py` (`out/models/models.json`, `out/view/`).
