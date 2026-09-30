#!/usr/bin/env bash
# Check a gasm bundle from tools/package-gasm.sh without game data (CI runs it on each bundle's own OS):
#   tools/smoke-gasm-bundle.sh <openrf-gasm-...-macos-universal.zip | ...-linux-<arch>.tar.gz>
# Unpacks it, checks the files, runs the bundled gasm-run without a CD (it must stop with OpenRF's "game
# data not found" message and exit code 1, not crash), and the launcher's --help and --dry-run paths
# against a fake CD folder / image (no dialogs: OPENRF_CD, --dry-run, a scratch HOME).
# With OPENRF_SMOKE_CD=<CD folder or image> it also runs the fire demo and compares the hashes.
set -euo pipefail
ARCHIVE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
case "$ARCHIVE" in
  *.zip) (cd "$T" && unzip -q "$ARCHIVE") ;;
  *.tar.gz) tar xzf "$ARCHIVE" -C "$T" ;;
  *) fail "unknown archive $ARCHIVE" ;;
esac
DIR=$(find "$T" -mindepth 1 -maxdepth 1 -type d -name 'openrf-gasm-*' | head -n 1)
[ -n "$DIR" ] || fail "no openrf-gasm-* folder in the archive"
for f in README.txt LICENSE LICENSE-gasm; do [ -s "$DIR/$f" ] || fail "missing $f"; done
if [ -d "$DIR/Return Fire (gasm).app" ]; then
  APP="$DIR/Return Fire (gasm).app"
  RUN="$APP/Contents/MacOS/gasm-run"; WASM="$APP/Contents/Resources/openrf.wasm"; L="$APP/Contents/MacOS/OpenRF"
  codesign --verify --strict "$APP" || fail "code signature"
  [ "$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$APP/Contents/Info.plist")" = pl.emdzej.openrf.gasm ] ||
    fail "bundle id"
  lipo -info "$RUN"
else
  RUN="$DIR/gasm-run"; WASM="$DIR/openrf.wasm"; L="$DIR/openrf.sh"
fi
if [ ! -x "$RUN" ] || [ ! -s "$WASM" ] || [ ! -x "$L" ]; then fail "gasm-run, openrf.wasm or the launcher missing"; fi
grep -q "gasm-run [0-9]" "$DIR/README.txt" || fail "README does not name the gasm version"

# 1. No data: a clean error from the game, not a crash.
set +e
out=$("$RUN" "$WASM" --headless 5 --mute 2>&1); rc=$?
set -e
echo "$out"
[ "$rc" = 1 ] || fail "gasm-run without data: exit $rc, expected 1"
echo "$out" | grep -q "game data not found" || fail "no 'game data not found' message"

# 2. Launcher: help, and the command it builds (no dialogs, scratch HOME so nothing real is touched).
export HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config" XDG_STATE_HOME="$T/home/.local/state"
mkdir -p "$HOME"
"$L" --help | grep -q "OpenRF" || fail "--help"
mkdir -p "$T/cd/art" && : > "$T/cd/RFIRE.BIN" && : > "$T/cd/art/Art.car"   # any letter case
OPENRF_CD="$T/cd" "$L" --dry-run --param level=2 | tee "$T/cmd"
{ grep -q -- "--asset-dir" "$T/cmd" && grep -q "level=2" "$T/cmd"; } || fail "dry run with a folder"
: > "$T/rf.ISO"
OPENRF_CD="$T/rf.ISO" "$L" --dry-run | grep -q "cd=" || fail "dry run with an image"
: > "$T/rf.cue"
if OPENRF_CD="$T/rf.cue" "$L" --dry-run 2>/dev/null; then fail ".cue accepted"; fi
if OPENRF_CD="$T" "$L" --dry-run 2>/dev/null; then fail "a folder without RFIRE.BIN accepted"; fi
if "$L" --dry-run </dev/null >/dev/null 2>&1; then fail "no CD, dry run: should exit non-zero"; fi
if [ -e "$XDG_CONFIG_HOME/openrf/cd-location" ] || [ -e "$HOME/Library/Application Support/OpenRF/cd-location" ]; then
  fail "a dry run saved the CD location"
fi

# 3. Optional: the real game with the user's CD.
if [ -n "${OPENRF_SMOKE_CD:-}" ]; then
  if [ -d "$OPENRF_SMOKE_CD" ]; then data=(--asset-dir "$OPENRF_SMOKE_CD"); else data=(--asset "cd=$OPENRF_SMOKE_CD"); fi
  "$RUN" "$WASM" "${data[@]}" --headless 689 --mute --param skip_intro=1 --param play=1 --param demo=fire 2>&1 |
    tee "$T/hash" | grep fnv
  grep -q "video_fnv32=d014dcfb audio_fnv32=f1520dc1" "$T/hash" || fail "fire demo hashes"
fi
echo "PASS $(basename "$ARCHIVE")"
