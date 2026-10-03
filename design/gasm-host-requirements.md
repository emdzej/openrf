# gasm host requirements for data-heavy guests (driven by OpenRF)

> **Status: implemented in gasm 0.3.0** (commit `39830c7`, "Data-heavy guests: file-backed assets,
> folders, Worker mode + OPFS, keymaps"). R1–R4 done; R5 (asset enumeration) deferred. Kept as the
> record of what OpenRF needed and why.

Audience: an agent (or person) implementing changes in the **gasm** repository
(`~/Projects/my/gasm`, https://github.com/emdzej/gasm). Read gasm's `AGENTS.md` and
`spec/ABI.md` first and follow their invariants (ABI changes start in `spec/abi.json`,
both runners change together, hash output format is a contract, determinism rules,
security model, no emojis on the site, tags without `v`).

## Context

**OpenRF** (https://github.com/emdzej/openrf, https://openrf.emdzej.pl) is a faithful
reimplementation of the 1996 game Return Fire in C11. It is being ported to run as a gasm
guest (`openrf.wasm`, built with gasm's C SDK) next to its SDL build of the time (since removed: gasm is now its only platform). It is a
software-rendered game (no `gasm:gfx`): it presents 640×480 RGBA via `video_present`, pushes
44.1 kHz stereo `f32` audio every frame, runs at `set_frame_rate(62.5)` (one original 16 ms tick
per frame), uses up to two pads, and `gasm:storage` for high scores. Later it will use
`gasm:net` for lockstep two-player through `gasm-relay`.

The problem is **data**. OpenRF ships no game data; the player supplies their own Return Fire
CD: about 450 files, 285 MB, including a 223 MB music file that is streamed (random access
via `asset_read_at`) and ~40 MB of movies. Today:

- the native runner loads every asset fully into memory (`HashMap<String, Vec<u8>>` in
  `runners/native/src/host.rs`);
- assets can only be passed one file at a time (`--asset name=path`);
- the web runner takes assets as in-memory `Uint8Array`s and runs the guest on the main
  thread, so a large data set must be read completely before the game starts;
- the keyboard maps to pad 0 only.

OpenRF works with gasm as it is (the whole disc image as one asset), but at the cost of
~300 MB of RAM and a slow start, and without the natural workflow: **the player mounts their
CD or ISO (the OS does this) and points the runner at the folder.**

These requirements are generic: nothing here is OpenRF-specific, and other data-heavy
guests (emulators with CD images, games with large asset packs) benefit the same way.

## Goals

1. Large assets cost no RAM and no start-up time on the native runner.
2. A folder can be exposed as assets, on both runners.
3. The web runner can serve assets lazily and synchronously from storage the page prepared
   (OPFS), so a large data set starts instantly on second visit and never sits in memory.
4. Two players can share one keyboard.

## Non-goals

- No ISO 9660 or other container parsing in gasm. Users mount images with their OS.
- No change to the semantics of existing imports; existing games, hashes and tests are
  unaffected.
- Populating OPFS (importing a picked folder, showing progress) is the embedding page's job
  (OpenRF will use its own page, built on https://github.com/emdzej/csfs). gasm only reads.
- `gasm:gfx` inside a Worker (needs `OffscreenCanvas`); see R3 scope.

## Requirements

Priority: **R1, R2, R4 are required. R3 is required for the browser experience. R5 is
optional.**

### R1 — File-backed assets in the native runner

- Assets given by path (`--asset`, `--rom`, and R2's `--asset-dir`) are opened, not read:
  `asset_size` comes from file metadata, `asset_read` / `asset_read_at` do positioned reads
  (`pread`/`read_at`, or a read-only memory map — implementer's choice; document it).
- Behaviour is byte-identical to today: same return values at and past the end, same `-1`
  for missing assets, same traps for bad guest pointers. Hash output unchanged.
- A file that shrinks or disappears while running must not crash the runner: reads return
  what is available (possibly 0) or `-1`; never panic.
- Headless runs stay reproducible (inputs are files on disk; nothing else changes).

**Acceptance:** all existing suites pass unchanged (`make test`, `scripts/determinism-test.sh`,
`scripts/net-test.sh`, `make parity`). A new test runs a guest that reads a ≥200 MB asset at
random offsets and compares a hash with an in-memory run; resident memory stays within a few
MB of the same run with a tiny asset (report the measured numbers, per gasm's rule that
numbers in docs come from real runs).

### R2 — Folders as assets

**Native:** `--asset-dir <dir>` (repeatable) exposes every regular file under `<dir>`
recursively. Asset name = path relative to `<dir>`, `/`-separated, as stored on disk
(e.g. `ART/ART.CAR`, `WORLDS/1PLAYER/LEVEL1/RFMAP001.RFM`). Optional prefix form
`--asset-dir <prefix>=<dir>` gives names `<prefix>/<relative path>`.

- Explicit `--asset name=path` wins over a folder entry with the same name.
- Security: never expose anything outside `<dir>`. Symlinks that resolve outside it are
  skipped (or, if simpler, all symlinks are skipped — document which). No `..` components.
- **Case-insensitive lookup** for folder-provided assets: a guest asking for `Art/art.car`
  gets `ART/ART.CAR` when exactly one entry matches case-insensitively (ASCII folding). An
  exact-case match always wins. If several entries differ only in case, the runner logs a
  warning at start-up and resolves case-insensitive lookups deterministically (e.g. first
  in sorted order). Reason: CD file systems are upper case, games use mixed case, and
  mounts on some systems lower-case the names.
- Directory traversal happens once at start-up; the set of names is fixed for the run.
- Hidden files (`.DS_Store` etc.) may be excluded; document the rule.

**Web:** the same semantics from the page's side:

- A helper that builds an asset source from a `FileSystemDirectoryHandle`
  (`showDirectoryPicker()`, Chromium) and one from a `FileList` produced by
  `<input type="file" webkitdirectory>` (all current browsers). For the `FileList` form, the
  leading root-folder segment of `webkitRelativePath` is stripped, so names match the
  native runner's.
- Same naming, precedence and case-insensitive rules as native.
- In main-thread mode these sources may preload into memory (with a progress callback);
  R3 makes them lazy.

**Acceptance:** a test guest (C or Rust, in `guests/`) that reads a small fixture tree
through mixed-case names produces identical hashes when run with `--asset-dir`, with the
equivalent list of `--asset` flags, and in the headless Node runner. Documented in
`spec/ABI.md` (runner behaviour section) and the site docs.

### R3 — Web: asset providers, OPFS, and guests in a Worker

1. **Asset-provider interface** in `@emdzej/gasm-host` (and its `.d.ts`), replacing the
   hard-wired `assets` object internally while keeping it accepted for compatibility:

   ```ts
   interface GasmAssetProvider {
     size(name: string): number;                                   // -1 if missing
     readAt(name: string, offset: number, dst: Uint8Array): number; // bytes copied, -1 if missing
   }
   ```

   Synchronous by design, because the ABI is. Built-ins: **memory** (today's behaviour,
   from `Record<string, Uint8Array>`), and the R2 folder sources (preloading).
2. **Worker mode** for guests that do not import `gasm:gfx`: the guest instance runs in a
   dedicated Worker. Frames are delivered to the page (transfer the RGBA buffer or use a
   `SharedArrayBuffer` when available); audio feeds the existing AudioWorklet; input
   (keyboard and Gamepad API state) is sent to the worker before each frame and is stable
   within a frame; `gasm:storage` and `gasm:net` keep working (bridged to the page or run
   in the worker — implementer's choice). Frame pacing, catch-up rules and the
   `begin_frame`/present semantics stay as specified.
3. **OPFS provider**, usable only in Worker mode: reads a directory in the origin private
   file system (the page chooses it, e.g. `openrf-cd/`) through
   `FileSystemSyncAccessHandle` — lazy, synchronous, no preload. Same naming and
   case-insensitive rules as R2. Handles are opened on first use and cached.
4. Optional: a `FileReaderSync`-backed provider for `File`/`Blob` sources (picked folder or
   files) in Worker mode, giving lazy reads without OPFS.
5. Main-thread mode remains the default and works exactly as today; `gasm:gfx` guests keep
   using it.

Cross-origin isolation (COOP/COEP) must **not** be required: the players will be hosted on
GitHub Pages, which cannot set those headers. Features that need `SharedArrayBuffer` must
fall back to transferables.

**Acceptance:** the existing web smoke test still passes. The headless Node runner and the
browser in Worker mode produce identical hashes for the existing C and NES guests. A demo
page (or test) imports a fixture folder into OPFS and runs a guest from it via the OPFS
provider in Worker mode; memory stays flat while the guest streams a large asset (report
measured numbers). Verified in Chrome; Safari and Firefox results reported.

### R4 — A second keyboard layout for pad 1

- Native and web runners map a second, non-overlapping key set to pad 1 by default, e.g.
  I/J/K/L = d-pad, U/O = L/R, N/M/comma/period = face buttons, Backspace = Select,
  Right Ctrl or keypad Enter = Start (implementer's choice; must not overlap pad 0's keys,
  and must be documented in `--help`, the site and the web runner's UI).
- Physical gamepads still take pads in connection order; the keyboard layout for pad 1
  applies when no second gamepad is connected (document the rule).
- Headless `--input` scripts unaffected.

**Acceptance:** a two-player guest (sumo) is playable with both players on one keyboard in
both runners; `scripts/net-test.sh` unaffected.

### R5 — Optional: asset enumeration

Additive imports in the `gasm` module (no ABI version bump if the project treats additive
imports as compatible; otherwise follow its rules):

| Import | Signature | Semantics |
|---|---|---|
| `asset_count` | `() -> u32` | Number of assets |
| `asset_name` | `(index, dst, cap) -> i32` | Name length of asset `index` (sorted order), copied only if ≤ `cap`; `-1` if out of range |

Needed only by guests that must discover what a folder contains. OpenRF does not need it (it
knows the file names on its CD); defer unless another guest wants it.

## Constraints and compatibility

- Asset sizes and offsets are 32-bit in the ABI (`asset_size -> i32`). Files over 2 GiB are
  not addressable; the runners should refuse them clearly rather than wrap. (OpenRF's largest
  file is 223 MB.)
- No new runtime dependencies in `@emdzej/gasm-host` (keep it dependency-free).
- Keep everything behind existing security rules: guests still see only what the runner
  exposes; storage namespaces still chosen by the runner.
- Update `spec/ABI.md` (runner behaviour: file-backed assets, folders, case-insensitive
  lookup, Worker mode), `AGENTS.md`, the site docs and both runners' `--help`/README in the
  same change set as the code, per gasm's conventions.

## What OpenRF will do with this

- Native: `gasm-run openrf.wasm --asset-dir /Volumes/RFIRE` (a mounted CD or ISO) or
  `--asset-dir ~/ReturnFire/cd`; until R2 exists, `--asset cd=<image>` with OpenRF's own disc
  image reader.
- Web (openrf.emdzej.pl/play): the player picks the mounted CD folder; the page imports it
  into OPFS (`openrf-cd/`) once with a progress bar, then starts `openrf.wasm` in Worker mode
  with the OPFS provider. Next visits start immediately with no prompt.
- High scores through `gasm:storage`; later, lockstep two-player through `gasm:net` and
  `gasm-relay`, and couch two-player with R4.
