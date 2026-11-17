#!/usr/bin/env bash
# Undo the installation.
#
#   scripts/uninstall.sh [--bottle NAME ...] [--keep-app]
#
#   --bottle NAME   restore this bottle: original DLLs back, our per-game overrides and controller setting removed
#                   (repeatable). The bottle itself, Steam and your games are kept.
#   --keep-app      keep "CrossOver (D3D12).app" (default: remove it)
#
# Your CrossOver app is never touched.
. "$(dirname "$0")/lib.sh"

bottles=() keep_app=0
while [ $# -gt 0 ]; do
	case "$1" in
		--bottle) bottles+=("$2"); shift ;;
		--keep-app) keep_app=1 ;;
		-h|--help) sed -n '2,11p' "$0"; exit 0 ;;
		*) die "unknown option: $1" ;;
	esac
	shift
done

for b in ${bottles[@]+"${bottles[@]}"}; do
	sys32="$(bottle_dir "$b")/drive_c/windows/system32"
	[ -d "$sys32" ] || { warn "bottle '$b' not found, skipped"; continue; }
	log "Restoring bottle '$b' (quit its programs first, e.g. scripts/launch-steam.sh $b --stop)"
	for d in "${DLLS[@]}"; do
		if [ -f "$sys32/$d.dll.orig" ]; then mv "$sys32/$d.dll.orig" "$sys32/$d.dll"
		else warn "$d.dll: no original saved; left as is"; fi
	done
	if [ -x "$CX/bin/wine" ]; then
		for g in "${DEFAULT_GAMES[@]}"; do
			for d in "${DLLS[@]}"; do
				wine_in "$b" reg delete "HKCU\\Software\\Wine\\AppDefaults\\$g\\DllOverrides" /v "$d" /f >/dev/null 2>&1 || true
			done
		done
		wine_in "$b" reg delete 'HKLM\System\CurrentControlSet\Services\winebus' /v DisableHidraw /f >/dev/null 2>&1 || true
		# Stop the bottle's Wine server (started by the edits above) so the registry is written and the app is free.
		for ws in "$CX"/bin/wineserver-*; do WINEPREFIX="$(bottle_dir "$b")" "$ws" -k >/dev/null 2>&1 || true; done
	else
		warn "app already removed: per-game overrides left in '$b' (harmless without our DLLs)"
	fi
done

if [ "$keep_app" = 0 ] && [ -e "$APP" ]; then
	for i in 1 2 3 4 5 6 7 8 9 10; do pgrep -f "$APP/" >/dev/null || break; sleep 1; done
	pgrep -f "$APP/" >/dev/null && die "$APP is running; quit it first"
	log "Removing $APP"
	rm -rf "$APP"
fi
log "Done"
