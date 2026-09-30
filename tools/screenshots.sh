#!/usr/bin/env bash
# Regenerate the documentation screenshots from the running game (needs the game data in ./cd).
# Usage: tools/screenshots.sh [build-dir]   → docs/public/screenshots/*.png
set -euo pipefail
cd "$(dirname "$0")/.."
BIN="${1:-build}/Return Fire.app/Contents/MacOS/Return Fire"
OUT=docs/public/screenshots
mkdir -p "$OUT"
export OPENRF_MUTE=1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

shot() {   # name ms demo args...
  local name=$1 ms=$2 demo=$3; shift 3
  if [[ -n "$demo" ]]; then export OPENRF_DEMO="$demo"; else unset OPENRF_DEMO; fi
  OPENRF_SHOT="$tmp/$name.bmp" OPENRF_SHOT_MS="$ms" "$BIN" cd "$@" >/dev/null 2>&1 || true
  sips -s format png "$tmp/$name.bmp" --out "$OUT/$name.png" >/dev/null
  echo "$OUT/$name.png"
}

shot title        1500  ""      --skip-intro
shot intro-logo   8000  ""
shot select       2500  ""      --skip-intro --play
shot lift         5000  1       --skip-intro --play
shot drive        9000  1       --skip-intro --play
shot tank-fire   11000  fire    --skip-intro --play
shot turret      12000  turret  --skip-intro --play
shot heli        12000  heli    --skip-intro --play
shot jeep        12000  jeep    --skip-intro --play
shot msv         12000  msv     --skip-intro --play
shot drone       14000  drone   --skip-intro --play
shot sub         16000  sub     --skip-intro --play
shot rules       14000  rules   --skip-intro --play
shot win          7000  win     --skip-intro --play
shot 2p-select    2500  ""         --skip-intro --play2
shot 2p-drive    12000  2p         --skip-intro --play2
shot 2p-heli     12000  2pheli     --skip-intro --play2
shot 2p-spectate 16000  2pspectate --skip-intro --play2
