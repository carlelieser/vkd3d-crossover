#!/bin/bash
# Rebuild MoltenVK's external deps (SPIRV-Cross etc.) WITHOUT fetchDependencies' force-checkout,
# so local SPIRV-Cross patches survive. Then rebuild MoltenVK and swap it into CrossOverDev.app.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT/src/MoltenVK"
# Xcode's build.db fails with disk I/O errors in this folder; keep intermediates in the scratchpad.
DD="${TMPDIR:-/tmp}/vkd3d-crossover-mvk-ext-dd"
rm -rf External/build/Intermediates/macOS "$DD"; mkdir -p "$DD" External/build/Intermediates
ln -s "$DD" External/build/Intermediates/macOS
xcodebuild -project ExternalDependencies.xcodeproj -scheme ExternalDependencies-macOS \
  -destination "generic/platform=macOS" -configuration Release \
  -derivedDataPath External/build/Intermediates/macOS -quiet build
PROJECT_DIR="." CONFIGURATION=Release SKIP_PACKAGING="" EXT_DIR=External
export PROJECT_DIR CONFIGURATION SKIP_PACKAGING
. ./Scripts/create_ext_lib_xcframeworks.sh
. ./Scripts/package_ext_libs_finish.sh
make macos MVK_USE_METAL_PRIVATE_API=1
L=Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib
for a in x86_64 aarch64; do
  T="$ROOT/build/CrossOverDev.app/Contents/SharedSupport/CrossOver/lib/$a/libMoltenVK.dylib"
  cp "$L" "$T"; codesign -f -s - "$T" 2>/dev/null
done
echo "MoltenVK rebuilt and swapped."
