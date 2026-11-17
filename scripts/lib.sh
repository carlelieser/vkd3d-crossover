# Shared helpers for the user scripts. Source it: . "$(dirname "$0")/lib.sh"
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_NAME="CrossOver (D3D12).app"
APP="${VKD3D_CROSSOVER_APP:-$HOME/Applications/$APP_NAME}"
CX="$APP/Contents/SharedSupport/CrossOver"
BOTTLES="$HOME/Library/Application Support/CrossOver/Bottles"
LOGDIR="$HOME/Library/Logs/vkd3d-crossover"
MARKER="$APP/Contents/SharedSupport/vkd3d-crossover"   # BUILDINFO of the installed build
REPO="carlelieser/vkd3d-crossover"
# DLLs installed into a bottle; d3d11 and dxgi must always come from the same implementation (DXVK).
DLLS=(d3d12 d3d12core dxgi d3d11)
# Games verified with this stack (per-game DLL overrides are set for these by default).
DEFAULT_GAMES=(re9.exe PRAGMATA_SKETCHBOOK.exe)

log()  { printf '\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33mwarning:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

require_app() {
	[ -x "$CX/bin/wine" ] || die "$APP not installed; run scripts/install.sh first"
}

bottle_dir() { echo "$BOTTLES/$1"; }

# wine_in <bottle> <args...>: run the cloned app's wine in a bottle.
wine_in() { local b="$1"; shift; "$CX/bin/wine" --bottle "$b" "$@"; }

# Find the user's CrossOver (the clone source).
find_crossover() {
	local c
	for c in "${CROSSOVER_APP:-}" "/Applications/CrossOver.app" "/Applications/CrossOver Preview.app" \
	         "$HOME/Applications/CrossOver.app" "$HOME/Applications/CrossOver Preview.app"; do
		[ -n "$c" ] && [ -x "$c/Contents/SharedSupport/CrossOver/bin/wine" ] && { echo "$c"; return 0; }
	done
	return 1
}
