#!/usr/bin/env bash
set -uo pipefail

# stdlib/lending.bas -- loans, servicing and payoff (docs/lending_design.md).
#
# PHASE 1 ANSWERS WHAT THE PAYMENT IS. This answers what happens next, which is
# not a formula: a borrower pays late, partly or extra; a rate changes; the
# loan is paid off on the 14th. Every one is a policy decision and every one
# moves the balance, so the conventions are the subject and the arithmetic is
# small.
#
# SELF-CHECKING, and forced: every defect here produces an ORDINARY-LOOKING
# BALANCE. An accrual basis that fails to distinguish itself, a waterfall
# applied in the wrong order, interest that never accrues -- all three read as
# perfectly good money, and a golden would record them as expected.
#
# TWO OF THOSE THREE HAPPENED WHILE WRITING IT, and both were found by the
# tests the design asked for rather than by reading:
#   * the first `_accrue` PRORATED the amortized basis by days, which makes it
#     algebraically identical to daily simple interest -- the two bases agreed
#     to the cent and the declaration was decorative. The design's "the
#     difference must be the days" test is what caught it.
#   * the second counted whole periods WITHIN each gap, so two events 14 and 17
#     days apart each rounded to zero and NO INTEREST EVER ACCRUED. A
#     weekly-payment loan would have run free forever. A single-payment test
#     could not see either.
#
# THE DIFFERENCE TIERS ARE THE POINT. Basis and waterfall are asserted as
# DIFFERENCES, not values: the same loan and payments under the two bases must
# disagree, and the disagreement must be the days; the same partial payment
# under two waterfalls must land differently PER COMPONENT, since the totals
# are equal by construction.
#
# THE LEDGER TIER PROVES THE REST (design section 5). A loan's whole life
# posted to a real `accounting` ledger must balance, with receivables equal to
# the servicing balance and interest income equal to interest collected. An
# unbalanced entry or a phantom account is refused where posted, so a loan that
# posts cleanly has DEMONSTRATED its arithmetic rather than asserted it -- the
# same device `fake` uses, and the reason accounting was built first.
#
# Headless, GI-independent, never skips (bar valgrind).

cd "$(dirname "$0")/.."
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER semantics\n'
if GBASIC_PATH=stdlib ./gbasic tests/lending_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "lending_test exits 0"
else
    fail "lending_test exits 0 ($(head -1 "$scratch/err"))"
fi
[ -s "$scratch/err" ] && fail "writes nothing to stderr ($(head -1 "$scratch/err"))" \
                      || pass "writes nothing to stderr"
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "reports no mismatch"
else
    fail "reports no mismatch"; grep MISMATCH "$scratch/out" | head -5
fi
ran=$(sed -n 's/^checks: //p' "$scratch/out")
if [ "${ran:-0}" -ge 33 ]; then
    pass "ran at least 33 checks (ran ${ran:-0})"
else
    fail "ran at least 33 checks (ran ${ran:-0})"
fi

# Named individually because each is a tier the suite would otherwise pass
# without: a basis that does not distinguish itself, a waterfall that does not
# reorder, interest that never accrues, and the ledger agreement.
for label in \
    'so the two bases differ' \
    'and the difference is the extra days beyond a period' \
    'interest accrues across short gaps' \
    '  the remainder reaches principal' \
    'receivables equal the servicing balance' \
    'and interest income equals interest collected' \
    'principal parts sum to the loan exactly' \
    'a missing income gives unknown, not a ratio' \
    'an overpayment is refused, not a negative balance'
do
    command grep -Fq "ok   $label" "$scratch/out" && pass "asserted: $label" || fail "asserted: $label"
done

printf 'TIER valgrind\n'
if command -v valgrind >/dev/null 2>&1; then
    if GBASIC_PATH=stdlib valgrind --error-exitcode=9 --leak-check=full \
            --errors-for-leak-kinds=definite ./gbasic tests/lending_test.bas \
            >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_lending: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
