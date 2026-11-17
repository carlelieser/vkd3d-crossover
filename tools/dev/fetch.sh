#!/usr/bin/env bash
# Fetch every upstream source at the commit pinned in versions.lock, into src/ (shallow, no full histories).
#
#   tools/dev/fetch.sh
#
# MoltenVK's own dependencies are fetched shallowly at MoltenVK's ExternalRevisions instead of running its
# fetchDependencies, which clones full histories and force-checks out SPIRV-Cross (that would discard our patches).
# Existing checkouts are reused; only missing commits are fetched.
. "$(dirname "$0")/lib.sh"

mkdir -p "$SRC"

log "MoltenVK"
shallow_checkout "$SRC/moltenvk" "$(lock_field moltenvk url)" "$(lock_field moltenvk sha)"

ext="$SRC/moltenvk/External"
revs="$SRC/moltenvk/ExternalRevisions"
mkdir -p "$ext"
dep() { # dep <dir under External> <url> <revision file>
	local rev; rev="$(tr -d '[:space:]' < "$revs/$3")"
	log "  $1 @ ${rev:0:12}"
	shallow_checkout "$ext/$1" "$2" "$rev"
}
dep cereal                          https://github.com/USCiLab/cereal.git            cereal_repo_revision
dep Vulkan-Headers                  https://github.com/KhronosGroup/Vulkan-Headers.git Vulkan-Headers_repo_revision
dep SPIRV-Cross                     https://github.com/KhronosGroup/SPIRV-Cross.git  SPIRV-Cross_repo_revision
dep SPIRV-Tools                     https://github.com/KhronosGroup/SPIRV-Tools.git  SPIRV-Tools_repo_revision
dep SPIRV-Tools/external/spirv-headers https://github.com/KhronosGroup/SPIRV-Headers.git SPIRV-Headers_repo_revision
dep Vulkan-Tools                    https://github.com/KhronosGroup/Vulkan-Tools.git Vulkan-Tools_repo_revision
dep Volk                            https://github.com/zeux/Volk.git                 Volk_repo_revision
# Same as fetchDependencies without --build-spirv-tools: use MoltenVK's pre-generated SPIRV-Tools headers.
unzip -o -q -d "$ext/SPIRV-Tools" "$SRC/moltenvk/Templates/spirv-tools/build.zip"
rm -rf "$ext/SPIRV-Tools/__MACOSX"

spvc_pin="$(lock_field spirv-cross sha)"
spvc_mvk="$(tr -d '[:space:]' < "$revs/SPIRV-Cross_repo_revision")"
[ "$spvc_pin" = "$spvc_mvk" ] || die "versions.lock spirv-cross ($spvc_pin) != MoltenVK's SPIRV-Cross revision ($spvc_mvk)"

log "vkd3d-proton (with submodules)"
shallow_checkout "$SRC/vkd3d-proton" "$(lock_field vkd3d-proton url)" "$(lock_field vkd3d-proton sha)"
git -C "$SRC/vkd3d-proton" submodule update -q --init --recursive --depth 1
dxil_pin="$(lock_field dxil-spirv sha)"
dxil_sub="$(git -C "$SRC/vkd3d-proton" ls-tree HEAD subprojects/dxil-spirv | awk '{print $3}')"
[ "$dxil_pin" = "$dxil_sub" ] || die "versions.lock dxil-spirv ($dxil_pin) != vkd3d-proton's submodule ($dxil_sub)"

log "DXVK (with submodules)"
shallow_checkout "$SRC/dxvk" "$(lock_field dxvk url)" "$(lock_field dxvk sha)"
git -C "$SRC/dxvk" submodule update -q --init --recursive --depth 1

log "Sources ready in src/. Next: tools/dev/apply-patches.sh"
