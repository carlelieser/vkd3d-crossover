#!/bin/zsh
# Launch Pragmata via the running Steam; report crash/alive after up to 40 s.
CX=~/Applications/CrossOverDev.app/Contents/SharedSupport/CrossOver
L=$(ls -t ~/Library/Logs/vkd3d-mesh/steam-*.log | head -1)
n0=$(grep -a -c 'Unhandled exception' $L)
"$CX/bin/wine" --bottle Steam-vkd3d 'C:\Program Files (x86)\Steam\steam.exe' -applaunch 4003800 >/dev/null 2>&1 &
for i in $(seq 1 20); do sleep 2; n=$(grep -a -c 'Unhandled exception' $L); [ $n -gt $n0 ] && { echo "CRASH after $((i*2))s"; exit 1; }; done
pgrep -f PRAGMATA_SKETCHBOOK.exe >/dev/null && echo "ALIVE after 40s" || echo "EXITED (no exception)"
