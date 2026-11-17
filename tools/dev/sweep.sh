#!/bin/zsh
# Runs each vkd3d-proton d3d12 test in its own process and records results (resumable).
# Usage: sweep.sh [max_tests_this_run]
ROOT="${0:A:h:h:h}"
C="$HOME/Library/Application Support/CrossOver/Bottles/vkd3d-test/drive_c/vkd3d"
OUT="${SWEEP_OUT:-$ROOT/build/sweep_results.tsv}"
ERRDIR="${SWEEP_ERRDIR:-$ROOT/build/sweep_errors}"
mkdir -p "$ERRDIR"
touch "$OUT"
MAX="${1:-100000}"
n=0
for t in $(grep -oE "decl_test\(([a-z0-9_]+)\)" "$ROOT/src/vkd3d-proton/tests/d3d12_tests.h" | sed -E 's/decl_test\(([a-z0-9_]+)\)/\1/'); do
  grep -q "^$t	" "$OUT" && continue
  (( n >= MAX )) && break
  n=$((n+1))
  VKD3D_TEST_MATCH=$t "$ROOT/build/run_test.sh" d3d12.exe 300 >/dev/null 2>&1
  W="$C/wine_d3d12.txt"
  summary=$(grep -aoE "[0-9]+ tests executed \([0-9]+ failures" "$W" | tail -1)
  [[ -z "$summary" ]] && summary="NO-SUMMARY"
  mvk=$(grep -ac "mvk-error" "$W")
  lost=$(grep -ac "Lost VkDevice" "$W")
  crash=$(grep -acE "Unhandled (page fault|exception)|Assertion failed" "$W")
  first=$(grep -a -m1 -A2 "mvk-error" "$W" | tr '\n\t' '  ' | cut -c1-300)
  print -r -- "$t	$summary	mvk=$mvk	lost=$lost	crash=$crash	$first" >> "$OUT"
  if (( mvk > 0 || lost > 0 || crash > 0 )); then cp "$W" "$ERRDIR/$t.txt"; fi
done
echo "ran $n tests; total recorded: $(wc -l < "$OUT")"
