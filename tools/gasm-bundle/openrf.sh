#!/bin/sh
# OpenRF @VERSION@ (openrf.wasm) on the bundled gasm-run @GASM_VERSION@: Linux launcher.
#   ./openrf.sh [options] [CD] [gasm-run options...]          (./openrf.sh --help)
# The CD location comes from the argument (then saved), else $XDG_CONFIG_HOME/openrf/cd-location, else a
# zenity or kdialog chooser (when a display is available), else the usage text. Test hooks (no dialogs):
# OPENRF_CD=<folder|image> uses that CD without saving it, OPENRF_DRY_RUN=1 or --dry-run prints the
# gasm-run command instead of running it.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/openrf"
LOCFILE="$CONF/cd-location"
LOGDIR="${XDG_STATE_HOME:-$HOME/.local/state}/openrf"
LOG="$LOGDIR/gasm.log"
TITLE="Return Fire (gasm)"
DRY=${OPENRF_DRY_RUN:-0}
CHANGE=0

usage() {
  cat <<EOF
OpenRF @VERSION@ on gasm-run @GASM_VERSION@

  $0 [options] [CD] [gasm-run options...]

CD is your Return Fire CD: the disc or a mounted disc image (e.g. /media/${USER:-you}/RFIRE), a folder you
copied the CD to (it has RFIRE.BIN and ART/ART.CAR), or a raw .bin or an .iso image file (for a
.bin/.cue pair, the .bin). It is saved in
  $LOCFILE
so later runs need no argument. Without one, a chooser opens (zenity or kdialog).

Options:
  --change-cd        ask for the CD even if one is saved
  --forget-cd        delete the saved CD location and exit
  --install-desktop  add a menu entry (~/.local/share/applications/openrf-gasm.desktop) and exit
  --dry-run          print the gasm-run command instead of running it (also OPENRF_DRY_RUN=1)
  --help             this text
Anything after the CD goes to gasm-run, e.g. --param level=12 --param play=1, --mute.
OPENRF_CD=<CD> uses that CD for one run without saving it.

More: https://openrf.emdzej.pl/guide/gasm
EOF
}

lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }

# ci_find <dir> <name> <f|d>: the entry of <dir> called <name> in any letter case
ci_find() { find "$1" -mindepth 1 -maxdepth 1 -iname "$2" -type "$3" -print 2>/dev/null | head -n 1; }

# check_cd <path>: exit 0 if usable; otherwise prints why
check_cd() {
  if [ -d "$1" ]; then
    art=$(ci_find "$1" ART d)
    if [ -n "$(ci_find "$1" RFIRE.BIN f)" ] && [ -n "$art" ] && [ -n "$(ci_find "$art" ART.CAR f)" ]; then
      return 0
    fi
    printf 'This folder does not look like the Return Fire CD (it needs RFIRE.BIN and ART/ART.CAR):\n%s\n' "$1"
    return 1
  fi
  if [ -f "$1" ]; then
    case "$(lower "$1")" in
      *.bin | *.iso) return 0 ;;
      *.cue) echo "A .cue sheet only names the image: use the .bin file next to it."; return 1 ;;
      *) printf 'Not a disc image (.bin or .iso):\n%s\n' "$1"; return 1 ;;
    esac
  fi
  printf 'The Return Fire CD was not found at:\n%s\nInsert or mount it, or choose it again.\n' "$1"
  return 1
}

absolute() { # absolute path without a trailing slash
  p=$1
  case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
  while [ "${#p}" -gt 1 ] && [ "${p%/}" != "$p" ]; do p=${p%/}; done
  printf '%s' "$p"
}

have_gui() { [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && { command -v zenity >/dev/null 2>&1 || command -v kdialog >/dev/null 2>&1; }; }

# ask <message>: prints the chosen folder or image (empty on Quit)
ask() {
  if command -v zenity >/dev/null 2>&1; then
    choice=$(zenity --question --title "$TITLE" --text "$1" --ok-label "Choose CD folder" \
      --cancel-label Quit --extra-button "Choose disc image" 2>/dev/null)
    rc=$?
    if [ "$rc" = 0 ]; then
      zenity --file-selection --directory --title "Choose the Return Fire CD (the folder with RFIRE.BIN)" 2>/dev/null
    elif [ "$choice" = "Choose disc image" ]; then
      zenity --file-selection --title "Choose the Return Fire disc image" \
        --file-filter "Disc images | *.bin *.BIN *.iso *.ISO" --file-filter "All files | *" 2>/dev/null
    fi
  else
    kdialog --title "$TITLE" --yesnocancel "$1" --yes-label "Choose CD folder" \
      --no-label "Choose disc image" --cancel-label Quit 2>/dev/null
    case $? in
      0) kdialog --title "Choose the Return Fire CD (the folder with RFIRE.BIN)" --getexistingdirectory "$HOME" 2>/dev/null ;;
      1) kdialog --title "Choose the Return Fire disc image" --getopenfilename "$HOME" "*.bin *.BIN *.iso *.ISO" 2>/dev/null ;;
    esac
  fi
}

error_box() {
  if command -v zenity >/dev/null 2>&1; then zenity --error --title "$TITLE" --text "$1" 2>/dev/null
  elif command -v kdialog >/dev/null 2>&1; then kdialog --title "$TITLE" --error "$1" 2>/dev/null; fi
}

install_desktop() {
  dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
  mkdir -p "$dir"
  sed -e "s|@DIR@|$HERE|g" "$HERE/openrf-gasm.desktop" > "$dir/openrf-gasm.desktop"
  echo "installed $dir/openrf-gasm.desktop"
}

INTRO="OpenRF needs your Return Fire CD (it is not included).

Choose the CD itself or a mounted disc image (for example under /media/${USER:-you}), a folder you copied the CD to, or a raw .bin or .iso image file.

Your choice is remembered; run openrf.sh --change-cd to pick another."

while [ $# -gt 0 ]; do
  case "$1" in
    --help | -h) usage; exit 0 ;;
    --dry-run) DRY=1; shift ;;
    --change-cd) CHANGE=1; shift ;;
    --forget-cd) rm -f "$LOCFILE"; echo "forgot the CD location ($LOCFILE)"; exit 0 ;;
    --install-desktop) install_desktop; exit 0 ;;
    *) break ;;
  esac
done
CD="" SAVE=0
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then CD=$(absolute "$1"); SAVE=1; shift; fi
if [ -z "$CD" ] && [ -n "${OPENRF_CD:-}" ]; then CD=$(absolute "$OPENRF_CD"); fi
if [ -z "$CD" ] && [ "$CHANGE" = 0 ] && [ -f "$LOCFILE" ]; then CD=$(head -n 1 "$LOCFILE"); fi

MSG=$INTRO
[ -n "$CD" ] && { MSG=$(check_cd "$CD") || true; }
while [ -z "$CD" ] || ! check_cd "$CD" >/dev/null; do
  if [ "$DRY" = 1 ] || [ -n "${OPENRF_CD:-}" ] || ! have_gui; then
    [ -n "$CD" ] && printf '%s\n\n' "$MSG" >&2
    [ "$DRY" = 1 ] || [ -n "${OPENRF_CD:-}" ] || usage >&2
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

if [ -d "$CD" ]; then set -- --asset-dir "$CD" "$@"; else set -- --asset "cd=$CD" "$@"; fi
set -- "$HERE/gasm-run" "$HERE/openrf.wasm" "$@"
if [ "$DRY" = 1 ]; then
  for a in "$@"; do printf "'%s' " "$(printf '%s' "$a" | sed "s/'/'\\\\''/g")"; done
  echo
  exit 0
fi

if [ -t 1 ] || [ -t 2 ]; then exec "$@"; fi
# Started from a menu or file manager: keep the output in a log and show errors in a dialog.
mkdir -p "$LOGDIR"
{ echo "--- $(date '+%Y-%m-%d %H:%M:%S') OpenRF @VERSION@, gasm-run @GASM_VERSION@"; echo "$*"; } >>"$LOG"
"$@" >>"$LOG" 2>&1
rc=$?
[ "$rc" = 0 ] || error_box "OpenRF stopped with an error:

$(tail -n 4 "$LOG")

Full log: $LOG"
exit "$rc"
