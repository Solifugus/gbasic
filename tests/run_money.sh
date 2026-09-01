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
. "$(dirname "$0")/valgrind_tier.sh"
make >/dev/null || exit 1

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

failures=0
checks=0
pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

export GBASIC_MONEY_V1=tests/money/v1_payload.hex
export GBASIC_PATH=stdlib

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
run_fixture tests/money_refusal_test.bas 24 'money_refusal'

# The threshold labels are named individually because the refusal tier and the
# acceptance tier below it are only meaningful as a PAIR: the same fixture has
# to refuse past the storage scale AND accept everything up to it, or a
# threshold that drifted in either direction would still look green. The
# retention label is the one display cannot show -- see the fixture.
for label in \
    'authored text past the STORAGE scale is refused' \
    'the threshold follows the CURRENCY, not a constant' \
    "past USD's ceiling is refused" \
    'a non-finite number is refused' \
    'the SAME excess precision, COMPUTED, is accepted' \
    'just inside the ceiling is accepted' \
    'a half-cent price is accepted' \
    'an authored sub-cent value is RETAINED, not rounded at the door' \
    'trailing zeros are accepted' \
    'exactly AT the storage scale is accepted'
do
    if command grep -Fq "ok   $label" "$work/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER the lossless exit (money.text)\n'
run_fixture tests/money_text_test.bas 20 'money_text'

# The control label is named explicitly: every other check in that fixture is
# also satisfied by a money.text that simply called string(), except the round
# trips -- and those pass for any value that happens to land on whole cents.
for label in \
    'USD renders at its storage scale' \
    'USD round-trips through text exactly' \
    'a computed third round-trips' \
    'money.text does not' \
    'half-even rounds 0.125 to 0.12'
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

printf 'TIER exchange rates (phase 3)\n'
run_fixture tests/money_fx_test.bas 30 'money_fx'

# A rate is a DATED fact: converting without an as-of date gives a number
# nobody can reproduce, which is an audit problem rather than an arithmetic
# one. Expected values are computed in exact decimal arithmetic OUTSIDE gBASIC
# rather than recorded from a run -- a conversion wrong in its last digit is
# exactly what a golden would enshrine.
for label in \
    "March uses January's rate" \
    "July uses June's rate" \
    'an eight-figure rate is applied exactly' \
    'USD to JPY, no minor unit' \
    'USD to KWD, three places' \
    'and is in the target currency' \
    'rate_on reports the date it came from' \
    'the inverse is refused, and says so' \
    'a date-shaped STRING is not a date' \
    'converting to the same currency is identity'
do
    if command grep -Eq "^ok   $label\$" "$work/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER allocation (phase 4)\n'
run_fixture tests/money_allocate_test.bas 25 'money_allocate'

# Division and allocation are DIFFERENT problems and guard digits only solve
# the first. Three payments cannot each be 33.3333 -- an invoice or a payroll
# line has to be a whole number of minor units -- so allocation works at the
# minor unit and distributes the remainder one unit at a time. Never three of
# 33.33 (loses a cent) and never three of 33.34 (invents one); both would look
# perfectly reasonable.
for label in \
    'THE POINT: they sum back exactly' \
    'weights split proportionally' \
    'a zero weight gets nothing' \
    'JPY splits into whole yen' \
    'KWD splits at three places' \
    'a negative amount allocates'
do
    if command grep -Eq "^ok   $label\$" "$work/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER the time value of money (phase 4)\n'
GBASIC_PATH=stdlib run_fixture tests/finance_test.bas 48 'finance'

# Expected values are EXTERNAL: a spreadsheet's answer for the same inputs
# (PMT 250,000 at 0.5%/month over 360 is -1498.88 in Excel and LibreOffice) or
# computed in Python. A TVM function wrong in the second decimal returns a
# number a finance person would act on.
for label in \
    "xirr: Excel's documented example" \
    'xnpv at the xirr is zero' \
    '30/360 differs from the actual counts' \
    'actual/actual: a leap year is exactly 1' \
    'multiple roots are warned about' \
    'a single sign change warns about nothing' \
    'rate: recovers the rate pmt used' \
    'rate: and round-trips back to the payment' \
    'timing: by exactly one period of interest' \
    'omitting the tail equals supplying the defaults' \
    'pmt: a 250k mortgage at 6%/yr over 30 years' \
    'pv: 1000/month for 30 years at 6%/yr' \
    'fv: 10000 at 5% for 10 years' \
    'npv: three years of 1000 at 10%' \
    'irr: recovers the rate npv used' \
    'the final balance is EXACTLY zero' \
    'the principal parts sum to the loan EXACTLY' \
    'interest falls over the term' \
    'a single flow breaks even at a negative rate'
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
if vg_available; then
    for fixture in tests/money_construct_test.bas tests/money_refusal_test.bas \
                   tests/money_text_test.bas \
                   tests/money_arithmetic_test.bas tests/money_overflow_test.bas \
                   tests/money_currency_test.bas tests/money_registry_test.bas \
                   tests/money_fx_test.bas tests/money_allocate_test.bas; do
        if vg_run ./gbasic "$fixture" >/dev/null 2>"$work/vg"; then
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
