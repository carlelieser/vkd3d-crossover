#!/bin/zsh
# Menu benchmark: launches the demo with MoltenVK performance logging (plus any extra MVK_*/VKD3D_* env given
# by the caller), runs SECS seconds, then prints the last performance summaries. Usage: bench_menu.sh <tag> [secs]
ROOT="${0:A:h:h:h}"; TAG="$1"; SECS="${2:-240}"
export MVK_CONFIG_PERFORMANCE_TRACKING=1 MVK_CONFIG_PERFORMANCE_LOGGING_FRAME_COUNT=300 MVK_CONFIG_ACTIVITY_PERFORMANCE_LOGGING_STYLE=0
DEMO_MVK_LOG=3 DEMO_WINEDEBUG="-all" "$ROOT/build/run_demo.sh" "$TAG" "$SECS" >/dev/null 2>&1
L="$ROOT/build/demo_runs/$TAG.wine.txt"
echo "== $TAG"
grep -a "avg FPS" "$L" | tail -3 | sed -E 's/.*avg FPS: ([0-9.]+).*/fps \1/' | tr '\n' ' '; echo
for k in "Frame interval" "Execute a MTLCommandBuffer on GPU" "Present swapchains" "Encode VkCommandBuffer" "vkQueueSubmit() encoding" "Compile MTLFunctions into a pipeline" "Convert SPIR-V to MSL" "Compile MSL into a MTLLibrary"; do
  grep -a "$k" "$L" | tail -1 | sed -E 's/\[mvk-info\] +//; s/, prev:[^,]*//; s/, min:[^,]*//' | cut -c1-150
done
