#!/usr/bin/env bash
# Put openrf.wasm into the site's play page (docs/public/play/openrf.wasm, git-ignored).
# CI runs it after building the module; locally it copies build-gasm/openrf.wasm.
#   docs/scripts/copy-wasm.sh [path/to/openrf.wasm]
# The module is the engine only: the game data always comes from the player's own CD.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
src="${1:-$root/build-gasm/openrf.wasm}"
[ -f "$src" ] || { echo "no $src: build openrf.wasm first (docs/guide/gasm.md, Build)" >&2; exit 1; }
magic="$(head -c 4 "$src" | od -An -tx1 | tr -d ' \n')"
[ "$magic" = 0061736d ] || { echo "$src is not a WebAssembly module" >&2; exit 1; }
cp "$src" "$root/docs/public/play/openrf.wasm"
echo "copied $(wc -c < "$src" | tr -d ' ') bytes -> docs/public/play/openrf.wasm"
