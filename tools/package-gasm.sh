#!/usr/bin/env bash
# Package openrf.wasm with a released gasm-run into a double-clickable bundle (no game data inside).
#   tools/package-gasm.sh <version> <platform> <gasm-run dir> <out dir>
# <platform>: macos-universal, linux-x86_64, linux-arm64 or windows-x86_64. <gasm-run dir> holds gasm-run[.exe]
# and gasm's LICENSE (tools/fetch-gasm-runner.sh <platform> makes one). OPENRF_WASM=<file> picks the module
# (default build-gasm/openrf.wasm). Produces in <out dir>:
#   macos-universal  Return Fire (gasm).app, openrf-gasm-<version>-macos-universal.zip
#   linux-<arch>     openrf-gasm-<version>-linux-<arch>.tar.gz
#   windows-x86_64   openrf-gasm-<version>-windows-x86_64.zip
# each archive with a .sha256 next to it. The macOS app is signed ad hoc when codesign is available.
# The launchers are in tools/gasm-bundle/. No AOT .cwasm: gasm-run --compile only targets the host it runs
# on, so it can't be made for the other platforms (or both halves of the universal app) at packaging time.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
usage="usage: package-gasm.sh <version> <macos-universal|linux-x86_64|linux-arm64|windows-x86_64> <gasm-run dir> <out dir>"
VERSION=${1:?$usage}; PLATFORM=${2:?$usage}; RUNDIR=${3:?$usage}; OUT=${4:?$usage}
WASM=${OPENRF_WASM:-$ROOT/build-gasm/openrf.wasm}
GASM_VERSION=$(cat "$RUNDIR/VERSION" 2>/dev/null || "$ROOT/tools/fetch-gasm-sdk.sh" --version)
SRC="$ROOT/tools/gasm-bundle"
EXE=""; [[ "$PLATFORM" == windows-* ]] && EXE=.exe
case "$PLATFORM" in macos-universal | linux-x86_64 | linux-arm64 | windows-x86_64) ;; *) echo "$usage" >&2; exit 2 ;; esac
for f in "$WASM" "$RUNDIR/gasm-run$EXE" "$RUNDIR/LICENSE"; do
  [ -f "$f" ] || { echo "missing $f (tools/fetch-gasm-runner.sh $PLATFORM fetches the runner and its LICENSE)" >&2; exit 1; }
done
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
NAME="openrf-gasm-$VERSION-$PLATFORM"

# fill <template> <out>: substitute the versions
fill() { sed -e "s|@VERSION@|$VERSION|g" -e "s|@GASM_VERSION@|$GASM_VERSION|g" "$1" > "$2"; }
crlf() { sed -e 's/\r*$/\r/' "$1" > "$1.tmp" && mv "$1.tmp" "$1"; }
sha() { (cd "$OUT" && if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi > "$1.sha256"); }

# readme <out file>: README.txt for this platform (~, $USER and backslashes are meant literally)
# shellcheck disable=SC2088,SC2016,SC1003
readme() {
  local start cd_how change saves logs lic=""
  [ -n "$EXE" ] && lic=.txt
  case "$PLATFORM" in
    macos-*)
      start='Open "Return Fire (gasm).app". It is signed ad hoc, not notarized: the first time,
right-click it and choose Open (or: xattr -dr com.apple.quarantine "Return Fire (gasm).app").'
      cd_how='The first time, the app asks for your CD: "Choose CD folder..." for the CD itself, a
mounted disc image (double-click an .iso; it appears under /Volumes) or a folder you copied the CD
to; "Choose disc image..." for a raw .bin (of a .bin/.cue pair) or an .iso file.'
      change='Hold Option while opening the app to choose another CD, or delete
  ~/Library/Application Support/OpenRF/cd-location
From Terminal: "Return Fire (gasm).app/Contents/MacOS/OpenRF" --help'
      saves='~/Library/Application Support/gasm/openrf/RFire_HS'
      logs='~/Library/Logs/OpenRF/gasm.log' ;;
    linux-*)
      start='Run ./openrf.sh (from a terminal or your file manager). ./openrf.sh --install-desktop adds
a menu entry. gasm-run needs ALSA (libasound2, package libasound2t64 on newer Debian and
Ubuntu) and a Vulkan or OpenGL capable graphics driver.'
      cd_how='Give it your CD once: ./openrf.sh /media/$USER/RFIRE (the CD or a mounted image), a folder
you copied the CD to, or a raw .bin (of a .bin/.cue pair) or an .iso file. Without an argument it
opens a chooser (zenity or kdialog) if one is installed.'
      change='./openrf.sh --change-cd, or give it another CD as the argument, or delete
  ${XDG_CONFIG_HOME:-~/.config}/openrf/cd-location
./openrf.sh --help lists the options; anything after the CD goes to gasm-run.'
      saves='~/.local/share/gasm/openrf/RFire_HS'
      logs='the terminal, or ~/.local/state/openrf/gasm.log when started from a menu' ;;
    windows-*)
      start='Double-click OpenRF.cmd. (If Windows SmartScreen warns about gasm-run.exe: More info,
Run anyway.)'
      cd_how='The first time, it asks for your CD: "Choose CD folder..." for the CD drive, a mounted disc
image (right-click an .iso, Mount) or a folder you copied the CD to; "Choose disc image..." for a
raw .bin (of a .bin/.cue pair) or an .iso file. From a command prompt: OpenRF.cmd D:\'
      change='OpenRF.cmd --change-cd, or delete %APPDATA%\OpenRF\cd-location.
OpenRF.cmd --help lists the options; anything after the CD goes to gasm-run.'
      saves='%APPDATA%\gasm\openrf\RFire_HS'
      logs='the console window' ;;
  esac
  cat > "$1" <<TXT
OpenRF $VERSION for gasm ($PLATFORM)
https://openrf.emdzej.pl

OpenRF is a faithful reimplementation of Return Fire (Silent Software, 1996). This bundle runs it
as a WebAssembly module (openrf.wasm) in the gasm runtime: gasm-run $GASM_VERSION is included
(https://gasm.emdzej.pl). It is the same game as the native macOS app and the browser player.

You need your own Return Fire CD (Windows 95 edition), or an image of it: none of the original
game's files are included.

START
$start

YOUR CD
$cd_how
The choice is remembered (the folder or file must still be there next time; a CD or mounted
image has to be inserted or mounted again).

CHANGE THE CD
$change

CONTROLS (the original keys; gamepads work too and take players 1 and 2)
                    Player 1             Player 2 (same keyboard)
  Drive / turn      W A S D or arrows    Keypad 8 5 4 6
  Fire (button 1)   H                    Keypad -
  Button 2          J                    Keypad +
  Button 3          K                    Keypad Enter
  Buttons 4 / 5     Q / E                Keypad 7 / 9
Title screen: F2 starts a one-player game, F3 a two-player game on one keyboard, 1-9 and
Shift+1-9 the first map of a difficulty level. In play: Alt+3 swaps sides, Esc leaves the
level. Hold Esc for a second to quit.
Full list: https://openrf.emdzej.pl/guide/controls

HIGH SCORES
$saves

LOG
$logs

LICENSES
OpenRF is GPL-3.0 (LICENSE$lic). gasm-run $GASM_VERSION is MIT (LICENSE-gasm$lic),
https://github.com/emdzej/gasm. Return Fire is (c) Silent Software.
TXT
}

case "$PLATFORM" in
macos-*)
  APP="$OUT/Return Fire (gasm).app"
  rm -rf "$APP"
  mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
  cp "$RUNDIR/gasm-run" "$APP/Contents/MacOS/gasm-run"
  fill "$SRC/launch-macos.sh" "$APP/Contents/MacOS/OpenRF"
  chmod +x "$APP/Contents/MacOS/OpenRF" "$APP/Contents/MacOS/gasm-run"
  cp "$WASM" "$APP/Contents/Resources/openrf.wasm"
  cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE"
  cp "$RUNDIR/LICENSE" "$APP/Contents/Resources/LICENSE-gasm"
  "$ROOT/tools/make-icns.sh" "$APP/Contents/Resources/openrf.icns"
  cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Return Fire (gasm)</string>
  <key>CFBundleDisplayName</key><string>Return Fire (gasm)</string>
  <key>CFBundleIdentifier</key><string>pl.emdzej.openrf.gasm</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleGetInfoString</key><string>OpenRF $VERSION on gasm-run $GASM_VERSION</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>OpenRF</string>
  <key>CFBundleIconFile</key><string>openrf</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>OpenRF contributors, GPL-3.0; gasm-run MIT. Return Fire is (c) Silent Software.</string>
</dict></plist>
PLIST
  if command -v codesign >/dev/null; then
    codesign --force --sign - "$APP/Contents/MacOS/gasm-run"
    codesign --force --sign - "$APP"   # ad hoc (not notarized)
    codesign --verify --strict "$APP"
  fi
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  mkdir "$STAGE/$NAME"
  cp -R "$APP" "$STAGE/$NAME/"
  readme "$STAGE/$NAME/README.txt"
  cp "$ROOT/LICENSE" "$STAGE/$NAME/LICENSE"
  cp "$RUNDIR/LICENSE" "$STAGE/$NAME/LICENSE-gasm"
  rm -f "$OUT/$NAME.zip"
  if command -v ditto >/dev/null; then (cd "$STAGE" && ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent "$NAME" "$OUT/$NAME.zip")
  else (cd "$STAGE" && zip -qry "$OUT/$NAME.zip" "$NAME"); fi
  sha "$NAME.zip"
  echo "$APP"; echo "$OUT/$NAME.zip" ;;
linux-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run" "$WASM" "$D/"
  [ "$(basename "$WASM")" = openrf.wasm ] || mv "$D/$(basename "$WASM")" "$D/openrf.wasm"
  fill "$SRC/openrf.sh" "$D/openrf.sh"
  cp "$SRC/openrf-gasm.desktop" "$D/"
  python3 "$ROOT/tools/icon.py" 256 "$D/openrf.png" "$ROOT/docs/public/favicon.png"
  chmod 755 "$D/gasm-run" "$D/openrf.sh"; chmod 644 "$D/openrf.wasm"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm"
  if tar --version 2>/dev/null | grep -q GNU; then own=(--owner=0 --group=0 --numeric-owner); else own=(--uid 0 --gid 0 --no-xattrs --no-mac-metadata); fi
  COPYFILE_DISABLE=1 tar "${own[@]}" -C "$STAGE" -czf "$OUT/$NAME.tar.gz" "$NAME"
  sha "$NAME.tar.gz"
  echo "$OUT/$NAME.tar.gz" ;;
windows-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run.exe" "$D/"
  cp "$WASM" "$D/openrf.wasm"
  fill "$SRC/OpenRF.cmd" "$D/OpenRF.cmd"; fill "$SRC/openrf.ps1" "$D/openrf.ps1"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE.txt"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm.txt"
  for f in OpenRF.cmd openrf.ps1 README.txt LICENSE.txt LICENSE-gasm.txt; do crlf "$D/$f"; done
  rm -f "$OUT/$NAME.zip"
  (cd "$STAGE" && zip -qr "$OUT/$NAME.zip" "$NAME")
  sha "$NAME.zip"
  echo "$OUT/$NAME.zip" ;;
esac
