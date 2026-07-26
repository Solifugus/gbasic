#!/usr/bin/env bash
# PLAT-OUTLINE platform suite: the general in-process source_outline(text) builtin.
#
# Tiers (all headless, GI-independent, path-free goldens):
#   1. golden cases        — 12 inline fixtures dumped canonically (kind/name/
#                            half-open byte range/line:col/flags + BYTE-exact slice
#                            of every node + containment self-check + diagnostics).
#   2. large-file / perf   — 5000-function generated source: exact node count and a
#                            generous wall-clock ceiling (catches pathological
#                            regressions, not micro-timing).
#   3. repeated calls      — 50x reparse+convert of a medium file: stable output.
#   4. memory              — valgrind on a golden fixture and the stress driver;
#                            SKIPs cleanly if valgrind is absent.
set -u

cd "$(dirname "$0")/.."

if [ ! -x ./gbasic ]; then
    if ! make gbasic >/dev/null 2>&1; then
        echo "FAIL run_outline: could not build gbasic"
        exit 1
    fi
fi

status=0
DUMP=tests/outline/outline_dump.bas
STRESS=tests/outline/outline_stress.bas
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---- Tier 1: golden fixtures -------------------------------------------------
MODES="empty one program functions modifiers nested consider multiline comments invalid unmatched unicode"
for m in $MODES; do
    gold="tests/outline/$m.out"
    got="$(./gbasic "$DUMP" "$m" 2>&1)"
    exp="$(cat "$gold" 2>/dev/null)"
    if [ "$got" = "$exp" ]; then
        echo "PASS outline:$m"
    else
        echo "FAIL outline:$m"
        diff <(printf '%s\n' "$exp") <(printf '%s\n' "$got") | head -20
        status=1
    fi
done

# Every golden must assert zero containment violations (a node's range contains
# its descendants'). Guards against a golden being rebaselined past a real break.
if grep -L "containment_violations=0" tests/outline/*.out | grep -q .; then
    echo "FAIL outline: a golden lacks containment_violations=0"
    status=1
else
    echo "PASS outline:containment (all goldens)"
fi

# ---- Tier 2: large file + performance ---------------------------------------
BIG="$TMP/big.bas"
awk 'BEGIN{
  for(i=0;i<5000;i++){
    printf "function f%d(a, b)\n  x = a + b\n  if x > 0 then\n    print x\n  end if\n  return x\nend function\n\n", i
  }
}' > "$BIG"
# 5 nodes/function (function, assign, if, print, return) * 5000 = 25000.
t0=$(date +%s.%N)
big_out="$(./gbasic "$STRESS" "$BIG" 1 2>&1)"
t1=$(date +%s.%N)
elapsed=$(awk "BEGIN{printf \"%.2f\", $t1-$t0}")
if [ "$big_out" = "ok=true nodes=25000 diagnostics=0 reps=1" ]; then
    echo "PASS outline:large-file (nodes=25000, ${elapsed}s incl process+read)"
else
    echo "FAIL outline:large-file"
    echo "  expected: ok=true nodes=25000 diagnostics=0 reps=1"
    echo "  actual:   $big_out"
    status=1
fi
# Generous ceiling: a single 40k-line parse+convert is sub-second in-process;
# 20s catches only pathological (e.g. accidental O(n^2)) regressions.
over=$(awk "BEGIN{print ($elapsed>20.0)?1:0}")
if [ "$over" = "1" ]; then
    echo "FAIL outline:large-file too slow (${elapsed}s > 20s)"
    status=1
fi

# ---- Tier 3: repeated calls (stability, no drift, no growth of result) -------
rep_out="$(./gbasic "$STRESS" "$BIG" 50 2>&1)"
if [ "$rep_out" = "ok=true nodes=25000 diagnostics=0 reps=50" ]; then
    echo "PASS outline:repeated-calls (50x stable)"
else
    echo "FAIL outline:repeated-calls"
    echo "  actual: $rep_out"
    status=1
fi

# ---- Tier 4: memory ----------------------------------------------------------
if command -v valgrind >/dev/null 2>&1; then
    MED="$TMP/med.bas"
    awk 'BEGIN{for(i=0;i<200;i++){printf "function f%d(a, b)\n  if a > b then\n    return a\n  end if\n  return b\nend function\n\n", i}}' > "$MED"
    vg_ok=1
    # A golden fixture (nested) and repeated stress over a medium file.
    for run in "$DUMP nested" "$STRESS $MED 5"; do
        log="$TMP/vg.log"
        # shellcheck disable=SC2086
        valgrind --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite \
            ./gbasic $run >/dev/null 2>"$log"
        rc=$?
        if [ $rc -eq 99 ]; then
            echo "FAIL outline:valgrind ($run)"
            grep -E "definitely lost|Invalid|ERROR SUMMARY" "$log" | head -6
            vg_ok=0
            status=1
        fi
    done
    if [ $vg_ok -eq 1 ]; then
        echo "PASS outline:valgrind (fixture + repeated stress, no definite leaks/errors)"
    fi
else
    echo "SKIP outline:valgrind (valgrind not installed)"
fi

echo "=== run_outline status=$status ==="
exit $status
