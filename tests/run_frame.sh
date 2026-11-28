#!/bin/bash
# stdlib/frame.bas's summarize and join by hash (docs/columnar_analytics_design.md
# §5 A, 2026-11-28). Both scanned: summarize every row once per group and a
# `contains` per row, join every pair of rows, so 10,000 rows in 10,000 groups
# took 83 s and a join of 10,000 against 10,000 did not finish in 300 s.
#
# TIERS:
#   semantics  tests/frame/hash_test.bas: what the scans answered, worked by
#              hand -- groups in first-seen order, unknown keys one group, an
#              exact 3 and a computed 3.0 one group, a join in left order with
#              each left row's matches in right order, duplicates on both sides
#              giving every pair, `_right` on a clash, no match giving no row
#   shape      tests/frame/scale.bas: N rows in N groups, and N joined to N,
#              at 25,000 and 100,000; linear is ~4x, the scans were ~16x, the
#              gate 8x. Ratios, never absolute times.
#   valgrind   the semantics fixture
. "$(dirname "$0")/portable.sh"
. "$(dirname "$0")/valgrind_tier.sh"
set -u
cd "$(dirname "$0")/.."
export GBASIC_PATH=stdlib
status=0
out="$(mktemp)"
err="$(mktemp)"
trap 'rm -f "$out" "$err"' EXIT

printf -- '-- semantics: summarize and join answer what the scans answered\n'
if timeout -k 5 120 ./gbasic tests/frame/hash_test.bas >"$out" 2>"$err" </dev/null &&
   [ ! -s "$err" ] && tail -1 "$out" | grep -qE '^ok [0-9]+ checks, 0 mismatches$'; then
    printf 'PASS semantics: %s\n' "$(tail -1 "$out")"
else
    grep MISMATCH "$out" | head -10
    head -5 "$err"
    printf 'FAIL semantics: %s\n' "$(tail -1 "$out")"
    status=1
fi

# Echoes elapsed seconds, or -1 when the fixture did not run or answered wrong:
# a crashed or no-op run is fast, and must never read as a pass.
timed() { # rows op
    local t0 t1
    t0=$(date +%s.%N)
    if ! timeout -k 5 900 ./gbasic tests/frame/scale.bas "$1" "$2" >"$out" 2>"$err" </dev/null; then
        printf 'FAIL scale %s %s (exit, or past 900 s)\n' "$1" "$2" >&2
        echo "-1"
        return
    fi
    t1=$(date +%s.%N)
    if [ "$(cat "$out")" != "$1" ]; then
        printf 'FAIL scale %s %s answered %s rows, not %s\n' "$1" "$2" "$(head -c 40 "$out")" "$1" >&2
        echo "-1"
        return
    fi
    echo "$t1 $t0" | awk '{ printf "%.3f", $1 - $2 }'
}

shape() { # op
    local ts tl r
    ts=$(timed 25000 "$1")
    tl=$(timed 100000 "$1")
    if [ "$ts" = "-1" ] || [ "$tl" = "-1" ]; then
        status=1
        return
    fi
    r=$(echo "$ts $tl" | awk '{ f = ($1 < 0.25) ? 0.25 : $1; printf "%.2f", $2 / f }')
    if awk -v r="$r" 'BEGIN { exit !(r <= 8) }'; then
        printf 'PASS shape %-10s %ss -> %ss  ratio %sx (gate 8x, the scans ~16x)\n' "$1" "$ts" "$tl" "$r"
    else
        printf 'FAIL shape %-10s %ss -> %ss  ratio %sx exceeds 8x -- gone superlinear\n' "$1" "$ts" "$tl" "$r"
        status=1
    fi
}

printf -- '-- shape: 25,000 to 100,000 rows, as many groups and matches\n'
shape summarize
shape join

printf -- '-- valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/frame/hash_test.bas >"$out" 2>"$err" </dev/null &&
       tail -1 "$out" | grep -q ' 0 mismatches$'; then
        printf 'PASS valgrind: no definite leak or invalid access over %s\n' "$(tail -1 "$out" | sed 's/^ok //')"
    else
        grep -E 'definitely lost|Invalid' "$err" | head -5
        printf 'FAIL valgrind\n'
        status=1
    fi
else
    printf 'SKIP valgrind (not installed -- memory was NOT measured)\n'
fi

if [ "$status" = 0 ]; then
    printf 'PASS tests/run_frame.sh\n'
else
    printf 'FAIL tests/run_frame.sh\n'
fi
exit "$status"
