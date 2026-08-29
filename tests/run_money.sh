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

export GBASIC_MONEY_V1=tests/money/v1_payload.hex

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
    'a large value survives from text' \
    'the same value survives from a literal' \
    'the largest authorable USD value' \
    'and the smallest' \
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
    "past USD's ceiling is refused" \
    'a non-finite number is refused' \
    'the SAME excess precision, COMPUTED, is accepted' \
    'just inside the ceiling is accepted'
do
    if command grep -Fq "ok   $label" "$work/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER arithmetic (phase 1)\n'
run_fixture tests/money_arithmetic_test.bas 24 'money_arithmetic'

# THE MULTIPLY TIER IS THE ONE THAT NEEDED PHASE 0 TO EXIST. `money * n` went
# through a double, and the double has precision to spare below 2^53 units --
# so the defect does NOT reproduce at ordinary magnitudes, and gdash warned
# that an implementer whose first probe passes may conclude the finding is
# wrong. Every expected value here is above 2^53 units and was computed by
# integer arithmetic OUTSIDE gBASIC; against the phase-0 binary the first two
# come back 184467440737095.52 and 276701161105643.28.
for label in \
    'x2 above 2\^53 is exact' \
    'x3 above 2\^53 is exact' \
    'a magnitude where the defect does NOT show' \
    '0.05 \* 0.5 = 0.025 ties to even' \
    '0.15 \* 0.5 = 0.075 ties to even' \
    'number \* money works too'
do
    if command grep -Eq "^ok   $label\$" "$work/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER overflow (phase 1)\n'
run_fixture tests/money_overflow_test.bas 12 'money_overflow'

# Overflow was UNDEFINED BEHAVIOUR, not a wrap: max + 0.01 returned the most
# NEGATIVE money value. The control half is what keeps this tier from being
# satisfiable by an implementation that raises on everything.
for label in \
    'max \+ 0.01 raises' \
    'min - 0.01 raises' \
    'division by zero raises' \
    'max - 0.01 succeeds' \
    'min \+ 0.01 succeeds' \
    'max / 2 succeeds .*' \
    'max \* 0.5 succeeds .*'
do
    if command grep -Eq "^ok   $label\$" "$work/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER currency identity and guard digits (phase 2)\n'
run_fixture tests/money_currency_test.bas 26 'money_currency'

# The guard-digit tier is the one phase 2 exists for: at cents scale every one
# of these lost money. And the equality/ordering split is the PLAT-EQ idiom --
# "is USD 19.95 equal to EUR 19.95" is a real question answered no, while
# ordering them is not a question at all without a rate.
for label in \
    '100 / 3 \* 3 comes back whole' \
    'but is retained: x3 gives the cent back' \
    'JPY 100 / 3 \* 3 comes back whole' \
    'JPY has no decimal places' \
    'KWD has three' \
    'adding different currencies raises' \
    'ordering different currencies raises' \
    'equality across currencies answers false' \
    'relabelling refuses rather than inventing a rate'
do
    if command grep -Eq "^ok   $label\$" "$work/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER the currency registry (phase 2)\n'
run_fixture tests/money_registry_test.bas 20 'money_registry'

for label in \
    'a withdrawn currency is not built in' \
    'registering it makes historical data expressible' \
    'an existing value still computes after retirement' \
    'but a new value is refused' \
    're-registering revives a retired currency'
do
    if command grep -Eq "^ok   $label\$" "$work/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER serialization and the v1 migration (phase 2)\n'
# THE V1 PAYLOAD IS REAL: tests/money/v1_payload.hex was written by the
# phase-1 binary, before money carried a currency. Testing migration against
# bytes this version generated to look old would prove nothing -- a migration
# nobody tested is a migration that does not work. It is hex because gBASIC's
# write/read truncate at the first NUL and cannot carry binary intact.
GBASIC_MONEY_V1=tests/money/v1_payload.hex \
    run_fixture tests/money_serialize_test.bas 13 'money_serialize'

for label in \
    'a v1 payload still deserializes' \
    'its money is rescaled into guard digits' \
    'a deserialized JPY still refuses USD' \
    'guard digits survive serialization'
do
    if command grep -Eq "^ok   $label\$" "$work/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER the defect is gone from the source\n'
# `round_to_cents` WAS the defect -- a divide and a multiply by 100 in floating
# point on a value that was already exact. Dead code that still compiles is how
# a retired construct comes back (the PLAT-BRACE lesson), so its absence is
# asserted rather than assumed.
# Match CODE, not mentions: the comments above the replacement name the old
# function to explain what it did wrong, and a check that greps for the bare
# word reddens on its own documentation. (The first version of this tier did
# exactly that -- the same false positive the docs-gate tripwire hit when its
# own note named a library that no longer exists.)
if command grep -nE "round_to_cents *\(" src/eval.c | command grep -vE "^[0-9]+: *\*" >/dev/null; then
    fail 'round_to_cents is gone from src/eval.c'
    command grep -nE "round_to_cents *\(" src/eval.c | command grep -vE "^[0-9]+: *\*" | head -3
else
    pass 'round_to_cents is gone from src/eval.c (mentions in comments are fine)'
fi

printf 'TIER valgrind\n'
if command -v valgrind >/dev/null 2>&1; then
    for fixture in tests/money_construct_test.bas tests/money_refusal_test.bas \
                   tests/money_arithmetic_test.bas tests/money_overflow_test.bas \
                   tests/money_currency_test.bas tests/money_registry_test.bas; do
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
