#!/usr/bin/env bash
# Check an installation and (optionally) a bottle, and say how to fix what's wrong.
#
#   scripts/doctor.sh [bottle]
. "$(dirname "$0")/lib.sh"
set +e   # report every problem, don't stop at the first

bottle="${1:-}"
fails=0 warns=0
ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
bad()  { printf '  \033[31m✗\033[0m %s\n' "$*"; fails=$((fails + 1)); }
meh()  { printf '  \033[33m!\033[0m %s\n' "$*"; warns=$((warns + 1)); }

echo "System"
[ "$(uname -m)" = arm64 ] && ok "Apple Silicon ($(sysctl -n machdep.cpu.brand_string 2>/dev/null))" || bad "not an Apple Silicon Mac"
ok "macOS $(sw_vers -productVersion)"

echo "D3D12 app ($APP)"
if [ ! -x "$CX/bin/wine" ]; then
	bad "not installed — run scripts/install.sh"
else
	ok "installed"
	if [ -f "$MARKER/BUILDINFO" ]; then
		ok "build: $(grep -m1 '^vkd3d-crossover:' "$MARKER/BUILDINFO" | cut -d' ' -f2) ($(grep -m1 '^variant:' "$MARKER/BUILDINFO" | cut -d' ' -f2))"
	else
		meh "no BUILDINFO (installed by an older script?)"
	fi
	for arch in aarch64 x86_64; do
		d="$CX/lib/$arch/libMoltenVK.dylib"
		[ -f "$d" ] || { bad "MoltenVK missing for $arch"; continue; }
		if codesign -v "$d" 2>/dev/null; then ok "MoltenVK ($arch) signed"; else bad "MoltenVK ($arch) signature invalid — run scripts/install.sh --force"; fi
	done
	# grep -c, not -q: -q exits early, strings gets SIGPIPE and pipefail turns a match into a failure
	if [ "$(strings "$CX/lib/aarch64/libMoltenVK.dylib" 2>/dev/null | grep -c 'uint lol = 0u')" -gt 0 ]; then
		ok "MoltenVK is the vkd3d-crossover build"
	else
		bad "MoltenVK is not ours (CrossOver's original?) — run scripts/install.sh --force"
	fi
	src="$(find_crossover)"
	if [ -n "$src" ]; then
		v_src="$(defaults read "$src/Contents/Info" CFBundleShortVersionString 2>/dev/null)"
		v_app="$(defaults read "$APP/Contents/Info" CFBundleShortVersionString 2>/dev/null)"
		if [ "$v_src" = "$v_app" ]; then ok "matches your CrossOver ($v_src)"
		else meh "your CrossOver is $v_src, the clone is $v_app — run scripts/install.sh --force"; fi
	fi
fi

if [ -n "$bottle" ]; then
	dir="$(bottle_dir "$bottle")"
	sys32="$dir/drive_c/windows/system32"
	echo "Bottle '$bottle'"
	if [ ! -d "$sys32" ]; then
		bad "not found (or not 64-bit) — scripts/setup-bottle.sh $bottle --create"
	else
		ok "exists"
		for d in "${DLLS[@]}"; do
			if [ -f "$MARKER/$d.dll" ] && cmp -s "$sys32/$d.dll" "$MARKER/$d.dll"; then ok "$d.dll is ours"
			else bad "$d.dll is not the installed build — scripts/setup-bottle.sh $bottle"; fi
		done
		if [ -x "$CX/bin/wine" ]; then
			for g in "${DEFAULT_GAMES[@]}"; do
				n=$(wine_in "$bottle" reg query "HKCU\\Software\\Wine\\AppDefaults\\$g\\DllOverrides" 2>/dev/null | tr -d '\r' | grep -c 'REG_SZ *native')
				if [ "$n" -ge 4 ]; then ok "overrides for $g"; else bad "overrides for $g incomplete ($n/4) — scripts/setup-bottle.sh $bottle"; fi
			done
			if [ "$(wine_in "$bottle" reg query 'HKLM\System\CurrentControlSet\Services\winebus' /v DisableHidraw 2>/dev/null | grep -c 0x1)" -gt 0 ]; then
				ok "PlayStation controllers as XInput"
			else
				meh "PlayStation controllers raw HID only (XInput-only games won't see them; setup-bottle.sh enables it)"
			fi
		fi
		if [ -f "$dir/drive_c/Program Files (x86)/Steam/steam.exe" ]; then ok "Steam installed"
		else meh "Steam not installed — scripts/setup-bottle.sh $bottle --install-steam"; fi
	fi
fi

echo
if [ "$fails" -gt 0 ]; then echo "$fails problem(s), $warns warning(s)"; exit 1; fi
if [ "$warns" -gt 0 ]; then echo "All good ($warns warning(s))"; else echo "All good"; fi
