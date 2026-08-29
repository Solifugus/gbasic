#!/usr/bin/env bash
set -uo pipefail

# PLAT-MONEY phase 0: exact construction (docs/money_design.md §7).
#
# WHY THESE FIXTURES ARE SELF-CHECKING RATHER THAN GOLDEN. Every defect this
# phase fixes produced a PLAUSIBLE NUMBER, never an error: a cent lost at the
# top of the range, and a rounding rule that flipped depending on the binary
# representation of the literal. A golden records whatever the binary said AS
# the expectation and then defends it -- which is precisely how these survived
# from the type's introduction until gdash became its first real consumer.
#
# THE ROUNDING TIER IS THE ONE TO UNDERSTAND. Before this change `0.125`
# rounded to 0.13 while `0.145` rounded to 0.14, which looks like banker's
# rounding and was not: `round_to_cents` was half-away-from-zero applied to a
# double, and 0.145 as a double is 0.14499999999999999001. So the rule
# depended on something the author could not see. Construction now renders a
# number to its SHORTEST round-trip decimal (PLAT-NUMFMT) and parses that text
# as an integer, so ties resolve half-even every time and the answer is
# predictable from what was written.
#
# THE REFUSAL FIXTURE CARRIES ITS OWN CONTROL, and it has to: a refusal tier
# alone is satisfied by a modifier that rejects everything. Each refusal sits
# beside its nearest legal neighbour -- authored "1.234" is refused while a
# COMPUTED 1.234 is accepted and rounded, because excess precision the author
# wrote is a bug in their input while excess precision from `price * 1.08` is
# ordinary arithmetic.
#
# Headless, no optional dependency, never skips (bar valgrind).

cd "$(dirname "$0")/.."
make >/dev/null || exit 1

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

failures=0
checks=0
pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

run_fixture() {
    local fixture="$1" floor="$2" label="$3"
    ./gbasic "$fixture" >"$work/out" 2>"$work/err"
    local status=$?

    [[ $status -eq 0 ]] && pass "$label exits 0" || {
        fail "$label exits 0 (exit $status)"; head -5 "$work/err"; }

    [[ -s "$work/err" ]] && { fail "$label writes nothing to stderr"; head -5 "$work/err"; } \
                         || pass "$label writes nothing to stderr"

    if command grep -q MISMATCH "$work/out"; then
        fail "$label reports no mismatch"
        command grep MISMATCH "$work/out" | head -10
    else
        pass "$label reports no mismatch"
    fi

    # The fixture's own summary, not the absence of MISMATCH: a fixture that
    # dies on line 20 also prints no mismatches.
    command grep -qx 'mismatches: 0' "$work/out" \
        && pass "$label finished and declared zero mismatches" \
        || { fail "$label finished and declared zero mismatches"; tail -5 "$work/out"; }

    local ran
    ran="$(command grep '^checks: ' "$work/out" | sed 's/^checks: //')"
    if [[ -n "$ran" ]] && [[ "$ran" -ge "$floor" ]]; then
        pass "$label ran at least $floor checks (ran $ran)"
    else
        fail "$label ran at least $floor checks (ran '${ran:-none}')"
    fi
}

printf 'TIER construction\n'
run_fixture tests/money_construct_test.bas 30 'money_construct'

# Named individually so deleting one shrinks the suite loudly rather than
# quietly. These are the claims that make this more than a smoke test.
for label in \
    "gdash's value survives from text" \
    'the same value survives from a literal' \
    'int64 max, exactly' \
    'int64 min, exactly -- one greater in magnitude' \
    '0.125 ties to even (0.12)' \
    '0.145 ties to even (0.14)' \
    'a negative tie goes to even too' \
    'the modifier is idempotent' \
    'a computed value rounds rather than raising'
do
    if command grep -Fq "ok   $label" "$work/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER refusals\n'
run_fixture tests/money_refusal_test.bas 17 'money_refusal'

for label in \
    'sub-cent authored text is refused' \
    'one cent past int64 max is refused' \
    'a non-finite number is refused' \
    'the SAME excess precision, COMPUTED, is accepted' \
    'int64 max itself is accepted'
do
    if command grep -Fq "ok   $label" "$work/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER valgrind\n'
if command -v valgrind >/dev/null 2>&1; then
    for fixture in tests/money_construct_test.bas tests/money_refusal_test.bas; do
        if valgrind --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite -q \
                    ./gbasic "$fixture" >/dev/null 2>"$work/vg"; then
            pass "valgrind clean: $fixture"
        else
            fail "valgrind clean: $fixture"
            head -20 "$work/vg"
        fi
    done
else
    printf '  SKIP valgrind (not installed)\n'
fi

printf '\n%d checks, %d failed\n' "$checks" "$failures"
[[ $failures -eq 0 ]]
