#!/bin/zsh
# Runs the demo with CrashReport.exe disabled and Wine's post-mortem backtrace written to a file; restores both.
ROOT="${0:A:h:h:h}"; TAG="$1"
W="$HOME/Applications/CrossOverDev.app/Contents/SharedSupport/CrossOver/bin/wine"
K='HKLM\Software\Microsoft\Windows NT\CurrentVersion\AeDebug'
B="$HOME/Library/Application Support/CrossOver/Bottles/Steam-vkd3d/drive_c"
G="$B/Program Files (x86)/Steam/steamapps/common/PRAGMATA SKETCHBOOK"
rm -f "$B/winedbg_bt.txt"
"$W" --bottle Steam-vkd3d reg add "$K" /v Debugger /t REG_SZ /d 'C:\windows\system32\cmd.exe /c C:\windows\system32\winedbg.exe --auto %ld %ld > C:\winedbg_bt.txt 2>&1' /f >/dev/null 2>&1
mv "$G/CrashReport.exe" "$G/CrashReport.exe.disabled"
DEMO_WINEDEBUG="-all" "$ROOT/build/run_demo.sh" "$TAG" "${2:-240}"
sleep 5
mv "$G/CrashReport.exe.disabled" "$G/CrashReport.exe"
"$W" --bottle Steam-vkd3d reg add "$K" /v Debugger /t REG_SZ /d 'winedbg --auto %ld %ld' /f >/dev/null 2>&1
[ -f "$G/CrashReport.exe" ] && echo "restored reporter + AeDebug"
cp "$B/winedbg_bt.txt" "$ROOT/build/demo_runs/$TAG.bt.txt" 2>/dev/null && echo "backtrace: build/demo_runs/$TAG.bt.txt"
