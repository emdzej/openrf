#!/usr/bin/env bash
# Regenerate the documentation screenshots from the running game: openrf.wasm on the released gasm-run,
# headless (deterministic, no window, no sound). Needs the game data in ./cd, or OPENRF_DATA=<cd folder|.bin|.iso>.
# Usage: tools/screenshots.sh [openrf.wasm]   → docs/public/screenshots/*.png
#   GASM_RUN=<gasm-run> uses another runner (default: tools/fetch-gasm-runner.sh for this machine).
set -euo pipefail
cd "$(dirname "$0")/.."
WASM="${1:-build-gasm/openrf.wasm}"
DATA="${OPENRF_DATA:-cd}"
OUT=docs/public/screenshots
if [[ -z "${GASM_RUN:-}" ]]; then
  case "$(uname -s)-$(uname -m)" in
    Darwin-*)       plat=macos-universal ;;
    Linux-x86_64)   plat=linux-x86_64 ;;
    Linux-aarch64)  plat=linux-arm64 ;;
    *) echo "no released gasm-run for $(uname -s)-$(uname -m): set GASM_RUN" >&2; exit 1 ;;
  esac
  GASM_RUN="$(tools/fetch-gasm-runner.sh "$plat")/gasm-run"
fi
if [[ -d "$DATA" ]]; then src=(--asset-dir "$DATA"); else src=(--asset "cd=$DATA"); fi
mkdir -p "$OUT"

shot() {   # name ms demo params...   (the first frame at game time >= ms; frame N is at 16 * (N - 1) ms)
  local name=$1 ms=$2 demo=$3; shift 3
  local args=()
  for p in "$@"; do args+=(--param "$p=1"); done
  if [[ -n "$demo" ]]; then args+=(--param "demo=$demo"); fi
  "$GASM_RUN" "$WASM" "${src[@]}" --headless $(((ms + 15) / 16 + 1)) --screenshot "$OUT/$name.png" \
    ${args[@]+"${args[@]}"} >/dev/null 2>&1
  echo "$OUT/$name.png"
}

shot title        1500  ""      skip_intro
shot intro-logo   8000  ""
shot select       2500  ""      skip_intro play
shot lift         5000  1       skip_intro play
shot drive        9000  1       skip_intro play
shot tank-fire   11000  fire    skip_intro play
shot turret      12000  turret  skip_intro play
shot heli        12000  heli    skip_intro play
shot jeep        12000  jeep    skip_intro play
shot msv         12000  msv     skip_intro play
shot drone       14000  drone   skip_intro play
shot sub         16000  sub     skip_intro play
shot rules       14000  rules   skip_intro play
shot win          7000  win     skip_intro play
shot 2p-select    2500  ""         skip_intro play2
shot 2p-drive    12000  2p         skip_intro play2
shot 2p-heli     12000  2pheli     skip_intro play2
shot 2p-spectate 16000  2pspectate skip_intro play2
