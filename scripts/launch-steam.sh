#!/usr/bin/env bash
# Start Steam in a bottle through the D3D12 app, so games use this stack.
#
#   scripts/launch-steam.sh <bottle>          start Steam (games launched from it inherit the settings)
#   scripts/launch-steam.sh <bottle> --stop   shut Steam down
#
# Games must be started from this Steam (or another program started by this script), not from the regular
# CrossOver app: CrossOver's own MoltenVK is too old for vkd3d-proton.
# Log: ~/Library/Logs/vkd3d-crossover/steam-<bottle>-<time>.log
. "$(dirname "$0")/lib.sh"

bottle="${1:-}"; [ -n "$bottle" ] || die "usage: scripts/launch-steam.sh <bottle> [--stop]"
require_app
[ -d "$(bottle_dir "$bottle")" ] || die "bottle '$bottle' doesn't exist (scripts/setup-bottle.sh $bottle --create)"
steam='C:\Program Files (x86)\Steam\steam.exe'

if [ "${2:-}" = --stop ]; then
	wine_in "$bottle" "$steam" -shutdown >/dev/null 2>&1 || true
	log "Steam asked to shut down"
	exit 0
fi

# Until every capability vkd3d-proton expects is real on Metal, these let it accept MoltenVK (OVERVIEW.md §6).
export VKD3D_RELAX_CAPS=1 DXVK_RELAX_CAPS=1
mkdir -p "$LOGDIR"
logf="$LOGDIR/steam-$bottle-$(date +%Y%m%d-%H%M%S).log"
log "Starting Steam in '$bottle' (log: $logf)"
nohup "$CX/bin/wine" --bottle "$bottle" "$steam" >>"$logf" 2>&1 &
disown
