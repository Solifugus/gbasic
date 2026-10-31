#!/bin/bash
# CONVERTING BETWEEN DATES, TIMES AND DATETIMES (docs/datetime_conversion_design.md).
# `{date}`, `{time}` and `{datetime}` read only TEXT until 2026-11-01: given a
# datetime VALUE each refused, even `{date}` given a value that already was a
# date, so a datetime's time of day and a date's midnight had no spelling but
# printing to text and reading it back -- and G-SQL's CAST, built on these
# modifiers, refused all three, which is why `SERIAL OF (CAST(t AS DATE))`
# could not be written. A value now becomes that kind: its date, its time of
# day at its own precision, or to the second (a date at midnight, as the text
# form already reads one), and a value lacking what the kind needs is refused
# NAMING WHAT IT LACKS.
#
# SELF-CHECKING NOT GOLDEN: two wrong conversions can print alike, so every
# answer is checked by its text, its precision and its FIELDS against the
# value it came from, and THE TEXT FORMS ARE THE CONTROL -- each must answer
# as it did, or "values convert" is satisfied by a change that also moved
# what `{date}= "2026-01-05 10:30"` means. The G-SQL half is in
# tests/gsql/sql_cases.txt. Headless, never skips (bar valgrind).
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
if ! make >/dev/null; then
    printf 'FAIL run_datetime_convert (the build failed, so nothing below would test this tree)\n'
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER semantics\n'
timeout -k 5 60 ./gbasic tests/datetime_convert/convert_test.bas > "$work/sem.out" 2> "$work/sem.err"
rc=$?
bad=$(grep -c '^MISMATCH' "$work/sem.out" || true)
checks=$(sed -n 's/^ok  *\([0-9]*\) checks.*/\1/p' "$work/sem.out" | tail -1)
if [ "${bad:-0}" != "0" ]; then
    grep '^MISMATCH' "$work/sem.out" | head -8
    fail "$bad mismatches"
elif [ "$rc" != "0" ]; then
    tail -3 "$work/sem.out"; head -3 "$work/sem.err"
    fail "the fixture exited $rc"
elif [ "${checks:-0}" -lt 55 ]; then
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
    vg_run ./gbasic tests/datetime_convert/convert_test.bas > /dev/null 2> "$work/vg.err"
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
    printf 'run_datetime_convert: all checks passed\n'
else
    printf 'run_datetime_convert: FAILED\n'
fi
exit "$status"
