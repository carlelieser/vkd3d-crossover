#!/usr/bin/env bash
# Build everything from the patched trees in src/ and collect the binaries in out/<variant>/.
#
#   tools/dev/build.sh [moltenvk|vkd3d-proton|dxvk ...]     (default: all)
#
# <variant> is what apply-patches.sh applied: "release" (shipping patches) or "dev" (+ diagnostics).
# Needs: Xcode (MoltenVK), meson + ninja + mingw-w64 (vkd3d-proton, DXVK, 64-bit Windows DLLs).
. "$(dirname "$0")/lib.sh"

[ -f "$SRC/.variant" ] || die "run tools/dev/apply-patches.sh first"
variant="$(cat "$SRC/.variant")"
dest="$OUT/$variant"
targets=("$@"); [ ${#targets[@]} -gt 0 ] || targets=(moltenvk vkd3d-proton dxvk)
mkdir -p "$dest"

build_moltenvk() {
	local m="$SRC/moltenvk"
	log "MoltenVK: external dependencies (SPIRV-Cross with our patches, SPIRV-Tools, ...)"
	# Xcode's build database (SQLite) fails with "disk I/O error" in some folders (seen under ~/Documents, likely
	# iCloud-synced): keep the dependency intermediates in a temp folder and link them where MoltenVK's scripts look.
	local dd="${TMPDIR:-/tmp}/vkd3d-crossover/mvk-ext-dd"
	mkdir -p "$dd" "$m/External/build/Intermediates"
	if [ ! -L "$m/External/build/Intermediates/macOS" ]; then
		rm -rf "$m/External/build/Intermediates/macOS"
		ln -s "$dd" "$m/External/build/Intermediates/macOS"
	fi
	( cd "$m"
	  xcodebuild -project ExternalDependencies.xcodeproj -scheme ExternalDependencies-macOS \
		-destination "generic/platform=macOS" -configuration Release \
		-derivedDataPath External/build/Intermediates/macOS -quiet build
	  PROJECT_DIR="." CONFIGURATION=Release SKIP_PACKAGING="" EXT_DIR=External
	  export PROJECT_DIR CONFIGURATION SKIP_PACKAGING EXT_DIR
	  set +u   # MoltenVK's packaging scripts read unset variables
	  . ./Scripts/create_ext_lib_xcframeworks.sh
	  . ./Scripts/package_ext_libs_finish.sh
	  log "MoltenVK: library (private Metal API on: needed for logic ops / feature level 11_1+)"
	  make macos MVK_USE_METAL_PRIVATE_API=1 )
	cp "$m/Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" "$dest/"
	if [ "$variant" = release ] && strings "$dest/libMoltenVK.dylib" | grep -q -e gfx-failures -e MVK_PASS_PROBE; then
		die "release libMoltenVK.dylib contains dev diagnostics strings"
	fi
}

meson_win64() { # meson_win64 <src dir> <build dir name> [ninja targets...]
	local s="$1" b="$1/$2"; shift 2
	if [ ! -f "$b/build.ninja" ]; then
		meson setup --cross-file "$s/build-win64.txt" --buildtype release "$b" "$s" >/dev/null
	fi
	ninja -C "$b" "$@"
}

build_vkd3d() {
	log "vkd3d-proton (win64)"
	meson_win64 "$SRC/vkd3d-proton" "build.$variant"
	cp "$SRC/vkd3d-proton/build.$variant/libs/d3d12/d3d12.dll" "$SRC/vkd3d-proton/build.$variant/libs/d3d12core/d3d12core.dll" "$dest/"
}

build_dxvk() {
	log "DXVK dxgi + d3d11 (win64)"
	meson_win64 "$SRC/dxvk" "build.$variant" src/dxgi/dxgi.dll src/d3d11/d3d11.dll
	cp "$SRC/dxvk/build.$variant/src/dxgi/dxgi.dll" "$SRC/dxvk/build.$variant/src/d3d11/d3d11.dll" "$dest/"
}

for t in "${targets[@]}"; do
	case "$t" in
		moltenvk) build_moltenvk ;;
		vkd3d-proton) build_vkd3d ;;
		dxvk) build_dxvk ;;
		*) die "unknown target: $t" ;;
	esac
done

{
	echo "variant: $variant"
	echo "vkd3d-crossover: $(git -C "$ROOT" rev-parse --short HEAD)$(git -C "$ROOT" diff --quiet || echo '-dirty')"
	echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	grep -v '^#' "$ROOT/versions.lock"
} > "$dest/BUILDINFO"
( cd "$dest" && shasum -a 256 ./*.dylib ./*.dll 2>/dev/null > SHA256SUMS || true )
log "Done: $dest"
ls -l "$dest"
