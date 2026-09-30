**OpenRF** is a native reimplementation of Return Fire (1996); this release has the macOS app and the gasm module. Both need the
game data from your own Return Fire CD, which is not included.

1. Download `OpenRF-…-macos-universal.zip` and unzip it.
2. The app is not notarized: the first time, right-click `Return Fire.app` → **Open**.
3. Put your extracted CD in `Return Fire.app/Contents/Resources/data`
   (see [Installing game data](https://openrf.emdzej.pl/guide/game-data)).

`openrf-….wasm` is the same game as a module for the [gasm](https://gasm.emdzej.pl) runtime (macOS, Linux,
Windows, browser): `gasm-run openrf-….wasm --asset cd=<your disc image>.bin`
(see [Running on gasm](https://openrf.emdzej.pl/guide/gasm)).
