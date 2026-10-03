**OpenRF** is a faithful reimplementation of Return Fire (1996). Every download needs the game data from your own
Return Fire CD (the disc, a mounted image, a copy, or a raw `.bin` / `.iso`), which is not included. Each file has a
`.sha256` next to it.

- `openrf-gasm-…-macos-universal.zip`: `Return Fire (gasm).app` for macOS (Apple Silicon and Intel).
- `openrf-gasm-…-linux-x86_64.tar.gz` / `openrf-gasm-…-linux-arm64.tar.gz`: Linux, start `./openrf.sh`.
- `openrf-gasm-…-windows-x86_64.zip`: Windows 10/11, start `OpenRF.cmd`.
- `openrf-….wasm`: the module alone, for your own [gasm](https://gasm.emdzej.pl) `gasm-run` 0.5.0 or newer.
- No download: [play in the browser](https://openrf.emdzej.pl/play/).

Each bundle is `openrf.wasm` with the released gasm runner and a launcher that asks for the CD once.

First run:

- macOS: the app is not notarized. The first time, right-click it → **Open** (or
  `xattr -dr com.apple.quarantine "Return Fire (gasm).app"`). It asks for the CD; hold Option to change it.
- Windows: extract the zip; if SmartScreen warns, **More info → Run anyway**. `OpenRF.cmd` asks for the CD.
- Linux: `tar xzf` keeps the executable bits (else `chmod +x openrf.sh gasm-run`); `./openrf.sh <CD>` once, then
  `./openrf.sh`; `./openrf.sh --install-desktop` adds a menu entry. Needs ALSA (`libasound2t64`).

Scaling: the gasm 0.6.0 runner shows the game with even pixels at any window size; pass `--filter xbr|fsr|crt|nearest`
or `--integer-scale` to the launcher to change it (in the browser: the toolbar under the game).

Details: [Installing](https://openrf.emdzej.pl/guide/install), [Running on gasm](https://openrf.emdzej.pl/guide/gasm).
