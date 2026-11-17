#!/usr/bin/env bash
# Install the D3D12 stack: make a local clone of your CrossOver app and put our MoltenVK into it.
#
#   scripts/install.sh [--from DIR] [--crossover PATH] [--force]
#
#   --from DIR        directory with the built files (default: out/release from tools/dev/build.sh)
#   --crossover PATH  CrossOver app to clone (default: /Applications/CrossOver.app or CrossOver Preview.app)
#   --force           replace an existing clone (do this after updating CrossOver)
#
# Your CrossOver app is never modified. The clone ("CrossOver (D3D12).app" in ~/Applications) is an APFS clone:
# it shares disk blocks with the original, so it costs almost no space.
. "$(dirname "$0")/lib.sh"

from="$ROOT/out/release" src_app="" force=0
while [ $# -gt 0 ]; do
	case "$1" in
		--from) from="$2"; shift ;;
		--crossover) src_app="$2"; shift ;;
		--force) force=1 ;;
		-h|--help) sed -n '2,12p' "$0"; exit 0 ;;
		*) die "unknown option: $1" ;;
	esac
	shift
done

[ "$(uname -m)" = arm64 ] || die "Apple Silicon Mac required"
for f in libMoltenVK.dylib "${DLLS[@]/%/.dll}"; do
	[ -f "$from/$f" ] || die "$from/$f missing (build with tools/dev/build.sh, or pass --from)"
done
if [ -f "$from/SHA256SUMS" ]; then
	( cd "$from" && shasum -a 256 -c --quiet SHA256SUMS ) || die "checksum mismatch in $from"
fi
grep -q '^variant: release' "$from/BUILDINFO" 2>/dev/null || warn "$from is not a release build (diagnostics included)"

[ -n "$src_app" ] || src_app="$(find_crossover)" || die "CrossOver not found; pass --crossover PATH"
log "CrossOver: $src_app ($(defaults read "$src_app/Contents/Info" CFBundleShortVersionString 2>/dev/null || echo '?'))"

if [ -e "$APP" ]; then
	[ "$force" = 1 ] || die "$APP exists (use --force to recreate it, e.g. after a CrossOver update)"
	pgrep -f "$APP/" >/dev/null && die "$APP is running; quit it first (scripts/launch-steam.sh --stop)"
	log "Removing old $APP"
	rm -rf "$APP"
fi

mkdir -p "$(dirname "$APP")"
log "Cloning to $APP (APFS clone, near-zero disk space)"
cp -c -R "$src_app" "$APP" 2>/dev/null || { warn "APFS clone failed; making a full copy"; cp -R "$src_app" "$APP"; }

log "Installing MoltenVK (Vulkan 1.3 + our fixes)"
for arch in aarch64 x86_64; do
	d="$CX/lib/$arch"
	[ -d "$d" ] || continue
	cp "$from/libMoltenVK.dylib" "$d/libMoltenVK.dylib.new"
	xattr -c "$d/libMoltenVK.dylib.new" 2>/dev/null || true
	codesign -f -s - "$d/libMoltenVK.dylib.new" 2>/dev/null
	# Replace by rename: copying over a mapped, signed dylib breaks loading in new processes.
	mv "$d/libMoltenVK.dylib.new" "$d/libMoltenVK.dylib"
done

mkdir -p "$MARKER"
cp "$from/BUILDINFO" "$MARKER/BUILDINFO" 2>/dev/null || true
for f in "${DLLS[@]/%/.dll}"; do cp "$from/$f" "$MARKER/$f"; done   # bottles get their DLLs from here

log "Installed. Next: scripts/setup-bottle.sh <bottle name> --create --install-steam"
