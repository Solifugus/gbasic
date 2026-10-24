#!/bin/bash
# THREE-VALUED LOGIC (docs/unknown_nan_design.md, increment B). `unknown` is
# gBASIC's SQL NULL, so `not`/`and`/`or` follow Kleene's tables and `unknown`
# propagates through comparison and arithmetic -- `unknown = unknown` is
# `unknown`. THE ORACLE IS SQL'S TABLES WRITTEN OUT BY HAND in the fixture, not
# read back from the binary, since a table copied from the implementation would
# defend whatever it said.
#
# THE CONTROLS OUTNUMBER NOTHING AND CARRY THE DESIGN: `if`, `while` and
# `until` still REFUSE an unknown (gBASIC's anti-silence rule, where SQL's WHERE
# would drop the row), the "same value" routes still call unknown the same value
# as unknown, string `+` still renders it (D2) and `nothing` stays two-valued
# (D3). Short-circuiting is asserted with a right operand that RAISES, so only
# an evaluator that never looks at it passes.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
if ! make >/dev/null; then
    printf 'FAIL run_three_valued (the build failed, so nothing below would test this tree)\n'
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER semantics\n'
timeout -k 5 60 ./gbasic tests/three_valued/three_valued_test.bas > "$work/sem.out" 2> "$work/sem.err"
rc=$?
bad=$(grep -c '^MISMATCH' "$work/sem.out" || true)
checks=$(sed -n 's/^ok  *\([0-9]*\) checks.*/\1/p' "$work/sem.out" | tail -1)
if [ "${bad:-0}" != "0" ]; then
    grep '^MISMATCH' "$work/sem.out" | head -8
    fail "$bad mismatches"
elif [ "$rc" != "0" ]; then
    tail -3 "$work/sem.out"; head -3 "$work/sem.err"
    fail "the fixture exited $rc"
elif [ "${checks:-0}" -lt 80 ]; then
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
    vg_run ./gbasic tests/three_valued/three_valued_test.bas > /dev/null 2> "$work/vg.err"
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
    printf 'run_three_valued: all checks passed\n'
else
    printf 'run_three_valued: FAILED\n'
fi
exit "$status"
