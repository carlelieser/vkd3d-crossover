#!/usr/bin/env bash
# Set up a CrossOver bottle for the D3D12 stack.
#
#   scripts/setup-bottle.sh <bottle> [--create] [--install-steam] [--game EXE ...]
#
#   --create          create the bottle (64-bit Windows 10) if it doesn't exist
#   --install-steam   download Valve's SteamSetup.exe and install Steam into the bottle
#   --game EXE        enable another game's executable (repeatable); verified games are always enabled
#   --native-ps-controllers
#                     keep Wine's default for PlayStation controllers (raw HID only). By default they are also
#                     presented as XInput gamepads, which XInput-only games (most modern ones) need
#
# Installs d3d12, d3d12core, dxgi and d3d11 into the bottle's system32 (originals kept as *.orig), and turns
# them on per game with Wine DLL overrides, so Steam and every other program keep their usual DLLs.
. "$(dirname "$0")/lib.sh"

bottle="" create=0 steam=0 xinput_ps=1 games=("${DEFAULT_GAMES[@]}")
while [ $# -gt 0 ]; do
	case "$1" in
		--create) create=1 ;;
		--install-steam) steam=1 ;;
		--game) games+=("$2"); shift ;;
		--native-ps-controllers) xinput_ps=0 ;;
		-h|--help) sed -n '2,14p' "$0"; exit 0 ;;
		-*) die "unknown option: $1" ;;
		*) [ -z "$bottle" ] || die "one bottle at a time"; bottle="$1" ;;
	esac
	shift
done
[ -n "$bottle" ] || die "usage: scripts/setup-bottle.sh <bottle> [--create] [--install-steam] [--game EXE]"
require_app
dir="$(bottle_dir "$bottle")"

if [ ! -d "$dir" ]; then
	[ "$create" = 1 ] || die "bottle '$bottle' doesn't exist (add --create)"
	log "Creating bottle '$bottle' (Windows 10, 64-bit)"
	"$CX/bin/cxbottle" --bottle "$bottle" --create --template win10_64 --description "D3D12 via vkd3d-crossover" >/dev/null
fi
sys32="$dir/drive_c/windows/system32"
[ -d "$sys32" ] || die "$sys32 missing; is '$bottle' a 64-bit bottle?"

log "Installing DLLs into $bottle"
for d in "${DLLS[@]}"; do
	[ -f "$MARKER/$d.dll" ] || die "$MARKER/$d.dll missing; re-run scripts/install.sh"
	if [ -f "$sys32/$d.dll" ] && [ ! -f "$sys32/$d.dll.orig" ]; then cp "$sys32/$d.dll" "$sys32/$d.dll.orig"; fi
	cp "$MARKER/$d.dll" "$sys32/$d.dll.new" && mv "$sys32/$d.dll.new" "$sys32/$d.dll"
done

log "Per-game DLL overrides: ${games[*]}"
for g in "${games[@]}"; do
	key="HKCU\\Software\\Wine\\AppDefaults\\$g\\DllOverrides"
	for d in "${DLLS[@]}"; do
		wine_in "$bottle" reg add "$key" /v "$d" /t REG_SZ /d native /f >/dev/null 2>&1 \
			|| die "could not set $d override for $g"
	done
done

# PlayStation controllers: Wine keeps only the raw HID device for DualShock 4 / DualSense and drops SDL's mapped
# gamepad, so XInput-only games see no controller (Steam and native-PS games still work). DisableHidraw makes the
# SDL gamepad (XInput-capable) the one games get. Takes effect the next time the bottle starts.
winebus='HKLM\System\CurrentControlSet\Services\winebus'
if [ "$xinput_ps" = 1 ]; then
	log "PlayStation controllers as XInput gamepads"
	wine_in "$bottle" reg add "$winebus" /v DisableHidraw /t REG_DWORD /d 1 /f >/dev/null 2>&1 || die "could not set winebus DisableHidraw"
else
	wine_in "$bottle" reg delete "$winebus" /v DisableHidraw /f >/dev/null 2>&1 || true
fi

if [ "$steam" = 1 ]; then
	steam_exe="$dir/drive_c/Program Files (x86)/Steam/steam.exe"
	if [ -f "$steam_exe" ]; then
		log "Steam already installed"
	else
		tmp="$(mktemp -d)"
		log "Downloading SteamSetup.exe from Valve"
		curl -fL --retry 3 -o "$tmp/SteamSetup.exe" https://cdn.cloudflare.steamstatic.com/client/installer/SteamSetup.exe
		log "Installing Steam (silent)"
		wine_in "$bottle" "$tmp/SteamSetup.exe" /S >/dev/null 2>&1 || true
		rm -rf "$tmp"
		[ -f "$steam_exe" ] || die "Steam install did not complete"
	fi
fi

log "Bottle '$bottle' ready. Start Steam with: scripts/launch-steam.sh $bottle"
