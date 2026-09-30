#!/bin/bash
# Launcher of Return Fire (gasm).app: OpenRF @VERSION@ (openrf.wasm) on the bundled gasm-run @GASM_VERSION@.
# Installed as Contents/MacOS/OpenRF by tools/package-gasm.sh. macOS ships bash 3.2: no bash 4 features.
#
# Finds the Return Fire CD (remembered in ~/Library/Application Support/OpenRF/cd-location), asks for it
# with a dialog when it is missing, then runs gasm-run; its output goes to ~/Library/Logs/OpenRF/gasm.log.
# Test hooks (no dialogs): OPENRF_CD=<folder|image> uses that CD without saving it, OPENRF_DRY_RUN=1 or
# --dry-run prints the gasm-run command instead of running it. Run with --help for the options.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
RES="$(cd "$HERE/../Resources" && pwd)"
CONF="$HOME/Library/Application Support/OpenRF"
LOCFILE="$CONF/cd-location"
LOGDIR="$HOME/Library/Logs/OpenRF"
LOG="$LOGDIR/gasm.log"
TITLE="Return Fire (gasm)"
DRY=${OPENRF_DRY_RUN:-0}
CHANGE=0

usage() {
  cat <<EOF
Return Fire (gasm).app: OpenRF @VERSION@ on gasm-run @GASM_VERSION@

  open "Return Fire (gasm).app" [--args [options] [CD] [gasm-run options...]]
  "Return Fire (gasm).app/Contents/MacOS/OpenRF" [options] [CD] [gasm-run options...]

CD is your Return Fire CD: the disc or a mounted disc image (/Volumes/...), a folder you copied the
CD to (it has RFIRE.BIN and ART/ART.CAR), or a raw .bin or an .iso image file (for a .bin/.cue pair,
the .bin). It is remembered in
  $LOCFILE
Without one the app asks for it. Hold Option while opening the app (or pass --change-cd) to pick
another; --forget-cd deletes the saved location.

Options:
  --change-cd    ask for the CD even if one is saved
  --forget-cd    delete the saved CD location and exit
  --dry-run      print the gasm-run command instead of running it (also OPENRF_DRY_RUN=1)
  --help         this text
Anything after the CD goes to gasm-run, e.g. --param level=12 --param play=1, --keymap FILE, --mute.
OPENRF_CD=<CD> uses that CD for one run without saving it.

Log: $LOG
More: https://openrf.emdzej.pl/guide/gasm
EOF
}

lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }

# ci_find <dir> <name> <f|d>: the entry of <dir> called <name> in any letter case
ci_find() { find "$1" -mindepth 1 -maxdepth 1 -iname "$2" -type "$3" -print 2>/dev/null | head -n 1; }

# check_cd <path>: exit 0 if usable; otherwise prints why
check_cd() {
  local p=$1 art
  if [ -d "$p" ]; then
    art=$(ci_find "$p" ART d)
    if [ -n "$(ci_find "$p" RFIRE.BIN f)" ] && [ -n "$art" ] && [ -n "$(ci_find "$art" ART.CAR f)" ]; then
      return 0
    fi
    echo "This folder does not look like the Return Fire CD (it needs RFIRE.BIN and ART/ART.CAR):"
    echo "$p"
    return 1
  fi
  if [ -f "$p" ]; then
    case "$(lower "$p")" in
      *.bin | *.iso) return 0 ;;
      *.cue) echo "A .cue sheet only names the image: choose the .bin file next to it."; return 1 ;;
      *) echo "Not a disc image (.bin or .iso):"; echo "$p"; return 1 ;;
    esac
  fi
  echo "The Return Fire CD was not found at:"
  echo "$p"
  echo "Insert or mount it, or choose it again."
  return 1
}

# absolute <path>: absolute path without a trailing slash
absolute() {
  local p=$1
  case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
  while [ "${#p}" -gt 1 ] && [ "${p%/}" != "$p" ]; do p=${p%/}; done
  printf '%s' "$p"
}

option_held() { # the Option key is down (NSEvent modifier flag 1 << 19)
  [ "$(osascript -l JavaScript -e 'ObjC.import("AppKit"); ($.NSEvent.modifierFlags & 0x80000) ? "1" : "0"' 2>/dev/null)" = 1 ]
}

# ask <message>: a dialog, then a folder or file chooser; prints the chosen path (empty on Quit)
ask() {
  local choice
  choice=$(osascript -e 'on run argv' \
    -e 'button returned of (display dialog (item 1 of argv) with title (item 2 of argv) buttons {"Quit", "Choose disc image…", "Choose CD folder…"} default button 3 cancel button 1 with icon note)' \
    -e 'end run' "$1" "$TITLE" 2>/dev/null) || return 0
  case "$choice" in
    "Choose CD folder…")
      osascript -e 'POSIX path of (choose folder with prompt "Choose the Return Fire CD (the folder with RFIRE.BIN), e.g. under /Volumes:" default location (POSIX file "/Volumes" as alias))' 2>/dev/null ;;
    "Choose disc image…")
      osascript -e 'POSIX path of (choose file with prompt "Choose the Return Fire disc image (.iso or the .bin of a .bin/.cue pair):")' 2>/dev/null ;;
  esac
}

INTRO="OpenRF needs your Return Fire CD (it is not included).

Choose the CD itself or a mounted disc image (they appear under /Volumes), a folder you copied the CD to, or a raw .bin or .iso image file.

Your choice is remembered. To change it later, hold Option while opening the app."

# Launcher options, then an optional CD, then gasm-run's options.
while [ $# -gt 0 ]; do
  case "$1" in
    --help | -h) usage; exit 0 ;;
    --dry-run) DRY=1; shift ;;
    --change-cd) CHANGE=1; shift ;;
    --forget-cd) rm -f "$LOCFILE"; echo "forgot the CD location ($LOCFILE)"; exit 0 ;;
    -psn_*) shift ;;   # process serial number from older Finder launches
    *) break ;;
  esac
done
CD="" SAVE=0
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then CD=$(absolute "$1"); SAVE=1; shift; fi
if [ -z "$CD" ] && [ -n "${OPENRF_CD:-}" ]; then CD=$(absolute "$OPENRF_CD"); fi
if [ -z "$CD" ] && [ "$CHANGE" = 0 ] && [ -f "$LOCFILE" ]; then
  CD=$(head -n 1 "$LOCFILE")
  if [ "$DRY" = 0 ] && option_held; then CD=""; fi
fi

MSG=$INTRO
[ -n "$CD" ] && { MSG=$(check_cd "$CD") || true; }
while [ -z "$CD" ] || ! check_cd "$CD" >/dev/null; do
  if [ "$DRY" = 1 ] || [ -n "${OPENRF_CD:-}" ]; then   # never open dialogs in test runs
    echo "no usable Return Fire CD${CD:+: $MSG}" >&2
    exit 2
  fi
  CD=$(ask "$MSG")
  [ -n "$CD" ] || exit 0
  CD=$(absolute "$CD")
  SAVE=1
  MSG=$(check_cd "$CD") || true
done
if [ "$SAVE" = 1 ] && [ "$DRY" = 0 ]; then
  mkdir -p "$CONF" && printf '%s\n' "$CD" > "$LOCFILE"
fi

if [ -d "$CD" ]; then DATA=(--asset-dir "$CD"); else DATA=(--asset "cd=$CD"); fi
CMD=("$HERE/gasm-run" "$RES/openrf.wasm" "${DATA[@]}" --window 1280x960 "$@")
if [ "$DRY" = 1 ]; then printf '%q ' "${CMD[@]}"; echo; exit 0; fi

mkdir -p "$LOGDIR"
{ echo "--- $(date '+%Y-%m-%d %H:%M:%S') OpenRF @VERSION@, gasm-run @GASM_VERSION@"; printf '%q ' "${CMD[@]}"; echo; } >>"$LOG"
"${CMD[@]}" >>"$LOG" 2>&1
rc=$?
if [ "$rc" != 0 ]; then
  osascript -e 'on run argv' \
    -e 'display alert "OpenRF stopped with an error" message (item 1 of argv) as critical' -e 'end run' \
    "$(tail -n 4 "$LOG")

Full log: $LOG" >/dev/null 2>&1
fi
exit "$rc"
