#!/bin/zsh
# Launch the PRAGMATA demo directly into the running Steam-vkd3d Steam with tracing. Usage: run_demo.sh <tag> [seconds] [extra WINEDLLOVERRIDES]
CX="$HOME/Applications/CrossOverDev.app/Contents/SharedSupport/CrossOver"
OUT="${0:A:h}/demo_runs"; mkdir -p "$OUT"
TAG="$1"; SECS="${2:-150}"
export SteamAppId=4003800 SteamGameId=4003800
export DXVK_RELAX_CAPS=1 VKD3D_RELAX_CAPS=1 VKD3D_DEBUG=${DEMO_VKD3D_DEBUG:-warn} MVK_CONFIG_LOG_LEVEL=${DEMO_MVK_LOG:-2}
export VKD3D_LOG_FILE="C:\\vkd3d-demo-$TAG.log"
export CX_DEBUGMSG="${DEMO_WINEDEBUG:-+seh}"; export WINEDEBUG="$CX_DEBUGMSG"
[ -n "$3" ] && export WINEDLLOVERRIDES="$3"
cd "$HOME/Library/Application Support/CrossOver/Bottles/Steam-vkd3d/drive_c/Program Files (x86)/Steam/steamapps/common/PRAGMATA SKETCHBOOK"
"$CX/bin/wine" --bottle Steam-vkd3d --cx-app ${DEMO_LAUNCHER:-} 'C:\Program Files (x86)\Steam\steamapps\common\PRAGMATA SKETCHBOOK\PRAGMATA_SKETCHBOOK.exe' > "$OUT/$TAG.wine.txt" 2>&1 &
for i in $(seq 1 $SECS); do sleep 1; ps -axo command | grep -v grep | grep -q "PRAGMATA_SKETCHBOOK.exe" || { [ $i -gt 15 ] && break; }; done
pkill -f "PRAGMATA_SKETCHBOOK.exe"; pkill -f "CrashReport.exe"; sleep 2
cp "$HOME/Library/Application Support/CrossOver/Bottles/Steam-vkd3d/drive_c/vkd3d-demo-$TAG.log" "$OUT/" 2>/dev/null
echo "ran ${i}s; log $OUT/$TAG.wine.txt ($(wc -l < "$OUT/$TAG.wine.txt") lines)"
