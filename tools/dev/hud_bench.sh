#!/bin/zsh
# Runs the demo with the Metal HUD for SECS seconds and screenshots the HUD region every 10 s after WARM seconds.
ROOT="${0:A:h:h:h}"; TAG="$1"; SECS="${2:-240}"; WARM="${3:-150}"
SH="$ROOT/build/demo_runs/hud_$TAG"; mkdir -p "$SH"
( sleep $WARM; for i in $(seq 1 8); do screencapture -x -t jpg "$SH/hud$i.jpg" 2>/dev/null; sleep 10; done ) &
MTL_HUD_ENABLED=1 DEMO_WINEDEBUG="-all" "$ROOT/build/run_demo.sh" "$TAG" "$SECS" >/dev/null 2>&1
wait
