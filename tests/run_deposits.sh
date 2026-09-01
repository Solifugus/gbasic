#!/usr/bin/env bash
set -uo pipefail

# stdlib/deposits.bas -- deposit interest, crediting and certificates
# (docs/lending_design.md section 7).
#
# A SEPARATE LIBRARY FROM LENDING ON PURPOSE. `waterfall`, `delinquency` and
# per-diem mean nothing to a savings account; `tier`, crediting schedule and
# early-withdrawal penalty mean nothing to a mortgage. What they genuinely
# share is simple interest over a day count, and that was pushed DOWN into
# `finance.accrue` once lending had proved what it is -- a deposit does not
# borrow, so lending must not own it.
#
# SELF-CHECKING, because every defect here produces an ordinary-looking amount
# of interest.
#
# THE BALANCE-METHOD TIER IS A DIFFERENCE, like lending's accrual basis: a
# large late withdrawal earns 28.77 under `daily` and 4.11 under `minimum`,
# which is why banks name the method in the account terms.
#
# AND ONE PAIR THAT DOES *NOT* DIFFER, ASSERTED AS EQUAL. Simple interest is
# linear in the balance, so the average balance earns exactly what each day's
# balance earns: `daily` and `average_daily` agree at a constant rate and part
# company only when the rate is not (under tiers). Asserting they differ would
# be asserting something false, and this is the tier that says so out loud.
#
# COMPOUNDING IS NOT CREDITING, which is the common error: interest is computed
# per crediting period and added at the end of it, so twelve 30-day periods
# come to more than flat simple interest, and interest since the last crediting
# date is reported SEPARATELY because the holder has earned it and not been
# paid it.
#
# THE PENALTY MAY EXCEED THE INTEREST, and then it reduces PRINCIPAL. A 90-day
# penalty on a CD redeemed after 31 days takes 123.29 against 42.47 earned, and
# clamping that at zero would report proceeds the holder will not receive. The
# maturity case is the control: without it a library that always penalised
# would pass every case above.
#
# Headless, GI-independent, never skips (bar valgrind).

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER semantics\n'
if GBASIC_PATH=stdlib ./gbasic tests/deposits_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "deposits_test exits 0"
else
    fail "deposits_test exits 0 ($(head -1 "$scratch/err"))"
fi
[ -s "$scratch/err" ] && fail "writes nothing to stderr ($(head -1 "$scratch/err"))" \
                      || pass "writes nothing to stderr"
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "reports no mismatch"
else
    fail "reports no mismatch"; grep MISMATCH "$scratch/out" | head -5
fi
ran=$(sed -n 's/^checks: //p' "$scratch/out")
if [ "${ran:-0}" -ge 24 ]; then
    pass "ran at least 24 checks (ran ${ran:-0})"
else
    fail "ran at least 24 checks (ran ${ran:-0})"
fi

# Named individually: each is a tier the suite would otherwise pass without.
for label in \
    'so the methods differ, and by a lot' \
    'average_daily equals daily at a constant rate' \
    'a year of 30-day crediting compounds past simple interest' \
    'interest since the last crediting date is separate' \
    'portion and whole disagree at a boundary' \
    'a penalty larger than the interest reduces principal' \
    'no penalty at maturity' \
    'a withdrawal beyond the balance is refused'
do
    command grep -Fq "ok   $label" "$scratch/out" && pass "asserted: $label" || fail "asserted: $label"
done

printf 'TIER valgrind\n'
if vg_available; then
    if GBASIC_PATH=stdlib vg_run ./gbasic tests/deposits_test.bas \
            >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_deposits: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
