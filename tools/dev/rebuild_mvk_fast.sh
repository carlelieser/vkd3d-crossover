#!/bin/bash
# Rebuild only MoltenVK (no SPIRV-Cross/deps rebuild) and swap into CrossOverDev.app.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT/src/MoltenVK"
make macos MVK_USE_METAL_PRIVATE_API=1
L=Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib
for a in x86_64 aarch64; do
  T="$ROOT/build/CrossOverDev.app/Contents/SharedSupport/CrossOver/lib/$a/libMoltenVK.dylib"
  cp "$L" "$T"; codesign -f -s - "$T" 2>/dev/null
done
echo "MoltenVK rebuilt and swapped."
