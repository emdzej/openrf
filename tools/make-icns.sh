#!/usr/bin/env bash
# Build the macOS app icon (.icns) from the site's favicon (the skull sprite), nearest-neighbour scaled.
# Used by CMake (Return Fire.app) and tools/package-gasm.sh (Return Fire (gasm).app). Needs macOS iconutil.
#   tools/make-icns.sh <out.icns>
set -euo pipefail
OUT=${1:?usage: make-icns.sh <out.icns>}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
SET="$TMP/openrf.iconset"; mkdir -p "$SET"
for s in 16 32 128 256 512; do
  python3 "$ROOT/tools/icon.py" "$s" "$SET/icon_${s}x${s}.png" "$ROOT/docs/public/favicon.png"
  python3 "$ROOT/tools/icon.py" $((s * 2)) "$SET/icon_${s}x${s}@2x.png" "$ROOT/docs/public/favicon.png"
done
iconutil -c icns "$SET" -o "$OUT"
