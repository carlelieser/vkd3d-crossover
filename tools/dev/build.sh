#!/usr/bin/env bash
# Build everything from the patched trees in src/ and collect the binaries in out/<variant>/.
#
#   tools/dev/build.sh [--package] [moltenvk|vkd3d-proton|dxvk ...]     (default: all)
#
#   --package   also write out/vkd3d-crossover-<variant>.tar.gz (what GitHub releases ship)
#
# <variant> is what apply-patches.sh applied: "release" (shipping patches) or "dev" (+ diagnostics).
# Needs: Xcode (MoltenVK), meson + ninja + mingw-w64 (vkd3d-proton, DXVK, 64-bit Windows DLLs).
. "$(dirname "$0")/lib.sh"

[ -f "$SRC/.variant" ] || die "run tools/dev/apply-patches.sh first"
variant="$(cat "$SRC/.variant")"
dest="$OUT/$variant"
package=0 targets=()
for a in "$@"; do case "$a" in --package) package=1 ;; *) targets+=("$a") ;; esac; done
[ ${#targets[@]} -gt 0 ] || targets=(moltenvk vkd3d-proton dxvk)
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
	# grep -c, not -q: with pipefail, -q's early exit (SIGPIPE in strings) would hide a leak
	if [ "$variant" = release ] && [ "$(strings "$dest/libMoltenVK.dylib" | grep -c -e gfx-failures -e MVK_PASS_PROBE)" -gt 0 ]; then
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
# Licenses of everything in the binaries, and where their source is (vkd3d-proton is LGPL-2.1).
mkdir -p "$dest/licenses"
cp "$SRC/moltenvk/LICENSE"                          "$dest/licenses/MoltenVK-Apache-2.0.txt"
cp "$SRC/moltenvk/External/SPIRV-Cross/LICENSE"     "$dest/licenses/SPIRV-Cross-Apache-2.0.txt"
cp "$SRC/vkd3d-proton/COPYING"                      "$dest/licenses/vkd3d-proton-LGPL-2.1.txt"
cp "$SRC/vkd3d-proton/subprojects/dxil-spirv/LICENSE.MIT" "$dest/licenses/dxil-spirv-MIT.txt"
cp "$SRC/dxvk/LICENSE"                              "$dest/licenses/DXVK-zlib.txt"
cat > "$dest/licenses/SOURCE.txt" <<SRCEOF
Source code for these binaries: each upstream project at the commit listed in BUILDINFO, plus the patches in
https://github.com/carlelieser/vkd3d-crossover (patches/), at the vkd3d-crossover revision listed in BUILDINFO.
Rebuild with tools/dev/fetch.sh, tools/dev/apply-patches.sh and tools/dev/build.sh from that repository.
SRCEOF
( cd "$dest" && shasum -a 256 ./*.dylib ./*.dll 2>/dev/null > SHA256SUMS || true )
if [ "$package" = 1 ]; then
	tarball="$OUT/vkd3d-crossover-$variant.tar.gz"
	tar -C "$OUT" -czf "$tarball" "$variant"
	log "Package: $tarball"
fi
log "Done: $dest"
ls -l "$dest"
