# Reverse-engineering workflow

OpenRF was written by studying the original Windows executable (`RFIRE.BIN` on the CD — the
real game; `RFIRE.EXE` is only a language launcher). None of it is included in the repository;
to work on OpenRF you need your own copy.

## Setup

1. Extract the CD to `cd/` in the checkout ([how](./extract-cd)).
2. Copy `cd/RFIRE.BIN` to `rfire_game.exe` and import it into Ghidra
   (the [ghidra-cli](https://github.com/akiselev/ghidra-cli) works well headlessly):
   ```sh
   ghidra import rfire_game.exe --project returnfire --program rfire
   ```
3. Dump the decompilation for grepping (`re/` is git-ignored):
   ```sh
   mkdir -p re
   ghidra script run tools/ghidra/DumpAllNamed.java --project returnfire --program rfire_game.exe -- "$PWD/re/all_named.c"
   ```

## Conventions

- Every ported function cites the original address in a comment, e.g.
  `/* Camera_Update 0x403a40 */`, and keeps the original fixed-point maths.
- **No data from the executable is in the source tree.** OpenRF reads the tables it needs (3D models,
  collision shapes, object/vehicle descriptors, sound tables, tunables, strings such as BNO and class
  names) from the user's own `RFIRE.BIN` at start-up: `src/exe.c` locates it in the data root, checks
  its size and CRC-32, maps the PE sections and runs the table loaders; code reads data by virtual
  address with `exe_ptr()` / `exe_u32()` / `exe_s32()` / `exe_str()` ...
- The table modules are **generated** by scripts in `tools/` (`gen_models_c.py`, `gen_game_tables.py`,
  `gen_rules_tables.py`, `gen_sfx_tables.py`, `gen_world_tables.py`). They emit only layout
  descriptors (addresses, counts, types) plus loader code that fills the same C structures from the
  mapped image (`EXE_LOADER(fn)` registers the loader with `exe_load()`). The executable is needed to
  *regenerate* them (to discover chains and counts), never to build. Hand-written modules that need a
  small table do the same: a static buffer plus an `EXE_LOADER` function.
- Allowed in code: addresses as identifiers, counts and strides, and small constants that are part
  of an algorithm. Not allowed: copied tables, geometry, descriptor contents or strings.
- Python tools read the executable at run time through `tools/rfexe.py` (`Exe`, `tile_table()`,
  `obj_table()`; lookup `$OPENRF_EXE`, `cd/RFIRE.BIN`, `rfire_game.exe`) instead of embedding copies.
- Verifying a change is behaviour-neutral: the headless tests, `python3 tools/render_cmp.py`, and
  `gasm-run --headless` hashes and screenshots (fixed 16 ms step, so frames are reproducible) compared
  before and after.
- Findings go into the [internals docs](/internals/) with exact layouts and addresses.
- Every subsystem gets a headless test ([Run the tests](./tests)); the renderer is checked
  pixel-for-pixel against a Python reference implementation (`tools/view.py`).

## Python tools

| Tool | Purpose |
|---|---|
| `tools/car.py` | ART.CAR sprite bank → PNGs |
| `tools/rfm.py` | RFM maps → overview PNGs + JSON |
| `tools/stm.py` | STM movies → AVI/MP4 |
| `tools/models.py` | 3D model parts → JSON |
| `tools/rfexe.py` | Shared RFIRE.BIN reader (PE sections, VA reads, tile/BNO tables) |
| `tools/view.py` | Reference world renderer |
