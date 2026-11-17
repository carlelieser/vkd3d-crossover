#!/usr/bin/env bash
# One-time reset of an RE Engine game's saved display mode (e.g. Resident Evil Requiem rendering at 882x550).
#
#   scripts/fix-game-config.sh <game folder | config.ini>
#
# RE Engine stores the display mode as an index into the mode list it saw at the time. Under Wine's old list (1,248
# odd sizes) the default index lands on a tiny mode, and that index stays in config.ini even after our DXGI reports a
# Windows-like list. This removes the saved index and list (DisplayModeCount, DisplayMode*_*, FullScreenDisplayMode)
# so the game picks again. Every other setting is kept; a backup is written next to the file.
# Run it while the game is closed.
. "$(dirname "$0")/lib.sh"

target="${1:-}"; [ -n "$target" ] || die "usage: scripts/fix-game-config.sh <game folder | config.ini>"
[ -d "$target" ] && target="$target/config.ini"
[ -f "$target" ] || die "$target not found"
grep -q '^\[RenderConfig\]' "$target" || die "$target doesn't look like an RE Engine config.ini"

n=$(grep -c -E '^(DisplayModeCount|DisplayMode[0-9]+_[A-Za-z]+|FullScreenDisplayMode)=' "$target" || true)
if [ "$n" = 0 ]; then
	log "Nothing to reset in $target"
	exit 0
fi
backup="$target.bak-$(date +%Y%m%d-%H%M%S)"
cp "$target" "$backup"
grep -v -E '^(DisplayModeCount|DisplayMode[0-9]+_[A-Za-z]+|FullScreenDisplayMode)=' "$backup" > "$target"
log "Removed $n saved display-mode lines (backup: $backup)"
