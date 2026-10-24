#!/bin/bash
# THE SECOND EQUALITY AND THE NaN ORDER (docs/unknown_nan_design.md, increment
# A). SQL has two equalities and gBASIC needs both: the `=` operator, and the
# "is this the same value" question its routes ask -- `contains`, `find`,
# `remove_value`, `unique`, a compound's deep comparison, a watcher's change
# test -- which SQL calls "not distinct". The orderings (`sort`, `min`, `max`,
# `median`, the percentile family) share one order with NaN in it.
#
# THE ONE DIFFERENCE IS NaN, asserted BOTH ways: the routes treat NaN as the same
# value as NaN and place it after +inf, while the `=` and `<` operators keep
# IEEE's answer (decision D1) -- either half alone passes on a build that changed
# the wrong one. MEASURED BEFORE BUILDING, over the whole gate on an instrumented
# build: no sort in the tree had ever met a NaN, so this changed no existing
# answer, and it fixed `sort([nan, 1, nan, 0])` leaving the 1 and the 0
# unsorted (a comparator answering "tie" for every pair touching NaN is not an
# order). AND A DEFECT FOUND ON THE WAY: the deep comparison compared DOUBLES,
# so `[9007199254740993] = [9007199254740992]` was true while the scalar
# comparison said false -- an exactness hole in compound `=`, `contains` on
# nested values and the watchers.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
if ! make >/dev/null; then
    printf 'FAIL run_same_value (the build failed, so nothing below would test this tree)\n'
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER semantics\n'
timeout -k 5 60 ./gbasic tests/same_value/same_value_test.bas > "$work/sem.out" 2> "$work/sem.err"
rc=$?
bad=$(grep -c '^MISMATCH' "$work/sem.out" || true)
checks=$(sed -n 's/^ok  *\([0-9]*\) checks.*/\1/p' "$work/sem.out" | tail -1)
if [ "${bad:-0}" != "0" ]; then
    grep '^MISMATCH' "$work/sem.out" | head -8
    fail "$bad mismatches"
elif [ "$rc" != "0" ]; then
    tail -3 "$work/sem.out"; head -3 "$work/sem.err"
    fail "the fixture exited $rc"
elif [ "${checks:-0}" -lt 30 ]; then
    fail "only ${checks:-0} checks ran -- the fixture stopped early"
else
    pass "$checks checks, 0 mismatches"
fi
if [ -s "$work/sem.err" ]; then
    head -3 "$work/sem.err"; fail "the fixture wrote to stderr"
fi

printf 'TIER valgrind\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    vg_run ./gbasic tests/same_value/same_value_test.bas > /dev/null 2> "$work/vg.err"
    vrc=$?
    if [ "$vrc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised|definitely" "$work/vg.err" | head -4
        fail "valgrind objected"
    elif [ "$vrc" != "0" ]; then
        fail "the fixture exited $vrc under valgrind"
    else
        pass "no definite leak or invalid access"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_same_value: all checks passed\n'
else
    printf 'run_same_value: FAILED\n'
fi
exit "$status"
