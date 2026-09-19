#!/usr/bin/env bash
# Operator precedence, and in particular where `not` sits (DOGFOOD 15).
#
# SELF-CHECKING RATHER THAN A GOLDEN, and forced: every defect in a precedence
# chain produces a PERFECTLY ORDINARY VALUE. `not a = b` answered `false`, which
# is a plausible answer to a reasonable question, so a golden would have
# recorded it as expected and defended it. The fixture states the answer it
# wants and prints a MISMATCH naming both sides.
#
# THE CONTROLS ARE MOST OF THE FILE, deliberately. Moving one level in a
# precedence chain is only safe if every other level stayed where it was, and
# "nothing else moved" is a claim about all of them -- so the fixture asserts
# arithmetic before comparison, comparison before `and`, `and` before `or`,
# unary minus untouched, and `not a and b` still grouping as `(not a) and b`.
#
# THE GRAMMAR TIER is not decoration. This project rejected `IDENT expression`
# as a statement form over 4 MEASURED shift/reduce conflicts, and adding a
# precedence level is exactly the kind of change that introduces them. bison
# must still report ZERO.
set -u
cd "$(dirname "$0")/.."

make >/dev/null || { echo "FAIL build"; exit 1; }
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0

# --- semantics ---------------------------------------------------------------
if timeout 60 ./gbasic tests/precedence_test.bas >"$work/out" 2>"$work/err" </dev/null; then
    if grep -q '^mismatches: 0$' "$work/out" \
       && [ "$(sed -n 's/^checks: //p' "$work/out")" -ge 22 ]; then
        printf 'PASS tests/precedence_test.bas (%s checks)\n' \
            "$(sed -n 's/^checks: //p' "$work/out")"
    else
        printf 'FAIL tests/precedence_test.bas -- precedence moved\n'
        grep -E '^MISMATCH|^checks|^mismatches' "$work/out"
        status=1
    fi
else
    printf 'FAIL tests/precedence_test.bas (exit)\n'; cat "$work/err"; status=1
fi

# --- the grammar stayed unambiguous ------------------------------------------
touch src/parser.y
conflicts="$(make 2>&1 | grep -ci 'conflict' || true)"
if [ "$conflicts" = "0" ]; then
    printf 'PASS grammar (bison reports zero conflicts)\n'
else
    printf 'FAIL grammar -- bison now reports conflicts:\n'
    make 2>&1 | grep -i conflict | head -5
    status=1
fi

# --- the documented table matches the grammar --------------------------------
# A precedence table is a claim like any other, and this one was ACCURATE while
# the behaviour it described was the surprise -- so it has to move WITH the
# grammar or it becomes wrong in the other direction. Checked by reading both.
table="$(grep -A9 '^Precedence, high to low:' docs/reference.md | grep -nE '^[0-9]+\.')"
cmp_at="$(printf '%s' "$table" | grep -n 'comparisons' | cut -d: -f1)"
not_at="$(printf '%s' "$table" | grep -n '`not`' | cut -d: -f1)"
if [ -n "$cmp_at" ] && [ -n "$not_at" ] && [ "$not_at" -gt "$cmp_at" ]; then
    printf 'PASS reference (the table puts `not` after comparisons, as the grammar does)\n'
else
    printf 'FAIL reference -- docs/reference.md disagrees with the grammar about `not`\n'
    printf '%s\n' "$table"
    status=1
fi

# --- `-not x` is gone, and that is a documented consequence -------------------
# It was legal and is now a parse error. Nothing in the tree writes it (every
# `-not` is prose in a comment), and asserting it keeps the removal deliberate
# rather than something noticed later by a puzzled reader.
printf 'x = true\nprint(- not x)\n' >"$work/m.bas"
if ./gbasic "$work/m.bas" >/dev/null 2>"$work/err"; then
    printf 'FAIL `-not x` should be a parse error now\n'; status=1
else
    grep -q 'parse error' "$work/err" \
        && printf 'PASS `-not x` is a parse error (documented consequence)\n' \
        || { printf 'FAIL `-not x` failed for the wrong reason: %s\n' "$(cat "$work/err")"; status=1; }
fi

exit "$status"
