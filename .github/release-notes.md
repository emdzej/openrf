**OpenRF** is a faithful reimplementation of Return Fire (1996). Every download needs the game data from your own
Return Fire CD (the disc, a mounted image, a copy, or a raw `.bin` / `.iso`), which is not included. Each file has a
`.sha256` next to it.

- `OpenRF-…-macos-universal.zip`: the native macOS app (Apple Silicon and Intel), recommended on a Mac.
- `openrf-gasm-…-macos-universal.zip`: `Return Fire (gasm).app`, the gasm build for macOS.
- `openrf-gasm-…-linux-x86_64.tar.gz` / `openrf-gasm-…-linux-arm64.tar.gz`: Linux, start `./openrf.sh`.
- `openrf-gasm-…-windows-x86_64.zip`: Windows 10/11, start `OpenRF.cmd`.
- `openrf-….wasm`: the module alone, for your own [gasm](https://gasm.emdzej.pl) `gasm-run` 0.3.0 or newer.
- No download: [play in the browser](https://openrf.emdzej.pl/play/).

First run:

- macOS: the apps are not notarized. The first time, right-click the app → **Open** (or
  `xattr -dr com.apple.quarantine "Return Fire.app"`). Native app: put your extracted CD in
  `Return Fire.app/Contents/Resources/data` or pass it as an argument
  ([Game data](https://openrf.emdzej.pl/guide/game-data)). The gasm app asks for the CD; hold Option to change it.
- Windows: extract the zip; if SmartScreen warns, **More info → Run anyway**. `OpenRF.cmd` asks for the CD.
- Linux: `tar xzf` keeps the executable bits (else `chmod +x openrf.sh gasm-run`); `./openrf.sh <CD>` once, then
  `./openrf.sh`; `./openrf.sh --install-desktop` adds a menu entry. Needs ALSA (`libasound2t64`).

Details: [Installing](https://openrf.emdzej.pl/guide/install), [Running on gasm](https://openrf.emdzej.pl/guide/gasm).
