#!/bin/zsh
# Usage: run_test.sh <exe-name> [timeout-seconds] [extra args...]
# Runs a test exe from C:\vkd3d in the vkd3d-test bottle using the CrossOverDev clone.
ROOT="${0:A:h:h:h}"
CX="$ROOT/build/CrossOverDev.app/Contents/SharedSupport/CrossOver"
C="$HOME/Library/Application Support/CrossOver/Bottles/vkd3d-test/drive_c/vkd3d"
EXE="$1"; TO="${2:-45}"; shift 2 2>/dev/null
cp "$ROOT/src/vkd3d-proton/${VKD3D_BUILD:-build.64}/libs/d3d12/d3d12.dll" "$ROOT/src/vkd3d-proton/${VKD3D_BUILD:-build.64}/libs/d3d12core/d3d12core.dll" "$ROOT/src/dxvk/build.64/src/dxgi/dxgi.dll" "$C/"
rm -f "$C/${EXE%.exe}.log"; cd "$C"
DXVK_RELAX_CAPS=1 VKD3D_RELAX_CAPS=1 MVK_CONFIG_LOG_LEVEL=${MVK_LOG:-2} MVK_CONFIG_DEBUG=${MVK_DBG:-0} WINEDLLOVERRIDES="d3d12,d3d12core,dxgi=n" VKD3D_DEBUG="${VKD3D_DEBUG:-info}" \
  VKD3D_LOG_FILE="C:\\vkd3d\\${EXE%.exe}.log" timeout "$TO" "$CX/bin/wine" --bottle vkd3d-test --workdir 'C:\vkd3d' "C:\\vkd3d\\$EXE" "$@" > "wine_${EXE%.exe}.txt" 2>&1
echo "exit=$?"
"$CX/bin/wine" --bottle vkd3d-test wineboot -k >/dev/null 2>&1
echo "--- MoltenVK errors"; grep -A6 "mvk-error" "wine_${EXE%.exe}.txt" | head -30
echo "--- vkd3d log (err/warn, deduped)"; grep -E ":(err|warn|info):" "${EXE%.exe}.log" | sed 's/^[0-9a-f]*://' | sort | uniq -c | sort -rn | head -40
