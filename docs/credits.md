# Credits and licences

**OpenRF** is released under the [GNU General Public License v3.0](https://github.com/emdzej/openrf/blob/main/LICENSE).
It contains no code, art, sound or data from the original game: everything is read from your own copy at run
time.

**Return Fire** is © 1995–1996 Silent Software / Prolific Publishing. OpenRF is not affiliated with them.

## Published and community sources

All format knowledge comes from disassembling `RFIRE.BIN`; no community wikis were used.

- **ECMA-119 (ISO 9660)** — the CD-ROM file system standard behind the disc-image reader.
- **A public description of the Cinepak bitstream** — the basis for the movie decoder.
- **The 3DO cel engine API** (CCB, PLUT, PRE0/PRE1) — field and structure names in the cel renderer, inherited
  from the 1995 3DO original the PC engine grew out of.

## Reference implementations

- [FFmpeg](https://ffmpeg.org) — only to convert decoded movies to MP4 for inspection, not as a codec reference.

## Tools

[Ghidra](https://ghidra-sre.org), [ghidra-cli](https://github.com/akiselev/ghidra-cli), [Capstone](https://www.capstone-engine.org), Pillow, [wasi-sdk](https://github.com/WebAssembly/wasi-sdk), [VitePress](https://vitepress.dev); SDL 3 for the early native macOS app (removed in 0.4.0).

## Runtime

[gasm](https://gasm.emdzej.pl) (MIT): `gasm-run`, the C SDK, and on this site `@emdzej/gasm-host`. OpenRF is part of
[ENCORE](https://encore.emdzej.pl), with its sibling reimplementations.

If we have missed someone whose work this project relied on, please
[open an issue](https://github.com/emdzej/openrf/issues) — gaps in these credits are bugs.
