# Installing

Every download is on the [releases page](https://github.com/emdzej/openrf/releases), each with a
`.sha256` file next to it. None of them contains the original game: you need your own Return Fire CD
(Windows 95 edition) or an image of it, see [Game data](./game-data).

| System | Download | Notes |
|---|---|---|
| macOS (Apple Silicon, Intel) | `openrf-gasm-<version>-macos-universal.zip` | `Return Fire (gasm).app`, asks for the CD itself |
| Linux x86_64 | `openrf-gasm-<version>-linux-x86_64.tar.gz` | gasm bundle: `openrf.sh` launcher |
| Linux arm64 | `openrf-gasm-<version>-linux-arm64.tar.gz` | gasm bundle: `openrf.sh` launcher |
| Windows 10/11 x86_64 | `openrf-gasm-<version>-windows-x86_64.zip` | gasm bundle: `OpenRF.cmd` launcher |
| Any browser | nothing to download | [Play in the browser](/play/){target="_self"} (Chrome, Edge, Firefox, Safari) |
| Your own gasm runner | `openrf-<version>.wasm` | Run it with `gasm-run` 0.5.0 or newer (0.6.0 recommended), see [Running on gasm](./gasm) |

The bundles are `openrf.wasm` with the matching released `gasm-run` (the
[gasm](https://gasm.emdzej.pl) WebAssembly game runtime) and a small launcher that finds your CD. Every
system, and the browser, runs the same module: same frames, same sound, same gameplay.

Up to 0.3.0 there was also a native macOS app (`OpenRF-<version>-macos-universal.zip`); from 0.4.0 on,
`Return Fire (gasm).app` replaces it. Its high scores carry over, see
[Running on gasm](./gasm#high-scores).

## macOS

1. Download `openrf-gasm-<version>-macos-universal.zip`, unzip it and move `Return Fire (gasm).app` where
   you like.
2. It is signed ad hoc, not notarized: the first time, **right-click it → Open** (or
   `xattr -dr com.apple.quarantine "Return Fire (gasm).app"`).
3. It asks for your CD: **Choose CD folder…** for the CD itself, a mounted disc image (double-click an
   `.iso`; it appears under `/Volumes`) or a folder you copied the CD to; **Choose disc image…** for a raw
   `.bin` (of a `.bin`/`.cue` pair) or an `.iso` file.

The choice is saved in `~/Library/Application Support/OpenRF/cd-location`. To pick another CD, **hold
Option** while opening the app, or run it with `--change-cd`; `--forget-cd` deletes the saved location.
The log is `~/Library/Logs/OpenRF/gasm.log`. From Terminal:

```sh
"Return Fire (gasm).app/Contents/MacOS/OpenRF" --help
"Return Fire (gasm).app/Contents/MacOS/OpenRF" /Volumes/RFIRE --param level=12 --param play=1
"Return Fire (gasm).app/Contents/MacOS/OpenRF" /Volumes/RFIRE --filter crt   # after the CD: gasm-run options
```

## Linux

1. Download `openrf-gasm-<version>-linux-x86_64.tar.gz` (or `-linux-arm64`) and unpack it:
   `tar xzf openrf-gasm-*.tar.gz`. The launcher keeps its executable bit; if your file manager lost it,
   `chmod +x openrf.sh gasm-run`.
2. gasm-run needs ALSA (`libasound2`, package `libasound2t64` on newer Debian and Ubuntu) and a Vulkan or
   OpenGL capable graphics driver.
3. Run it once with your CD: `./openrf.sh /media/$USER/RFIRE` (the CD or a mounted image), a folder you
   copied the CD to, or a raw `.bin` or `.iso`. Without an argument it opens a chooser (zenity or
   kdialog) if one is installed.
4. Optional: `./openrf.sh --install-desktop` adds a menu entry.

The CD is saved in `${XDG_CONFIG_HOME:-~/.config}/openrf/cd-location`, so later runs need no argument.
To change it: `./openrf.sh --change-cd`, or give another CD as the argument. `./openrf.sh --help` lists
the options; anything after the CD goes to gasm-run.

## Windows

1. Download `openrf-gasm-<version>-windows-x86_64.zip` and extract it (right-click → **Extract All**)
   to a folder of your choice.
2. Double-click `OpenRF.cmd`. If Windows SmartScreen warns about an unrecognised app, click **More info →
   Run anyway** (gasm-run is not code-signed).
3. It asks for your CD: **Choose CD folder…** for the CD drive, a mounted disc image (right-click an
   `.iso` → **Mount**) or a folder you copied the CD to; **Choose disc image…** for a raw `.bin` or an
   `.iso`. From a command prompt: `OpenRF.cmd D:\`.

The CD is saved in `%APPDATA%\OpenRF\cd-location`. To change it: `OpenRF.cmd --change-cd`, or delete that
file; `--forget-cd` deletes it. `OpenRF.cmd --help` lists the options; anything after the CD goes to
gasm-run. On an error the console window stays open so you can read it.

## Where things are kept

| | Saved CD | High scores | Log |
|---|---|---|---|
| `Return Fire (gasm).app` | `~/Library/Application Support/OpenRF/cd-location` | `~/Library/Application Support/gasm/openrf/RFire_HS` | `~/Library/Logs/OpenRF/gasm.log` |
| Linux bundle | `~/.config/openrf/cd-location` | `~/.local/share/gasm/openrf/RFire_HS` | the terminal, or `~/.local/state/openrf/gasm.log` from a menu |
| Windows bundle | `%APPDATA%\OpenRF\cd-location` | `%APPDATA%\gasm\openrf\RFire_HS` | the console window |

A CD or a mounted image has to be inserted or mounted again before the next start; a saved folder or
image file must still be where it was.

Prefer to build it yourself? See [Build from source](/howto/build-from-source).
