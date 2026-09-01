#!/usr/bin/env bash
set -uo pipefail

# credit analytics over a portfolio (docs/credit_analytics_design.md) --
# Phase 3's second increment, and the item docs/lending_design.md §8 deferred
# until `fake.plant` existed so it could be tested "against a portfolio with
# known-bad accounts rather than six hand-written loans".
#
# `lending` answers questions about ONE loan. This answers questions about a
# BOOK -- vintage curves, roll rates, migration, charge-off and recovery --
# and none of them is derivable from a single loan or is arithmetic about
# balances. They are questions about STATES OVER TIME.
#
# SELF-CHECKING, not golden, and forced: EVERY defect this library exists to
# prevent produces an ordinary-looking PERCENTAGE. A roll rate that drops
# attrition, a vintage curve on the wrong index, a matrix that double-counts --
# each reads as a book doing slightly better or worse than expected, and a
# golden would record the damaged figure as expected and defend it.
#
# THE LOAD-BEARING TIER IS RECONCILIATION, the counterpart of the accounting
# equation: every loan observed at t is accounted for at t+1, bucket by bucket.
# It is never ENFORCED anywhere in the library -- it falls out of correct
# bucketing -- which is exactly what makes it a good test.
#
# AND THE TIER THAT KEEPS IT FROM BEING VACUOUS is the one where four accounts
# GO SILENT. `observe` emits a row for every date once a loan is written, so
# nothing ever disappears from a table it produced, and a library that never
# counted attrition at all would reconcile perfectly. Proven: with the
# `unobserved` count removed, the main book reports zero mismatches and only
# the silent-account tier goes red.
#
# EVERY TIER PROVEN RED against its own deliberately broken copy of the
# library: dropped attrition (both in counts and in rates), a vintage indexed
# by calendar month instead of age, an outstanding denominator that
# double-counts a delinquent loan, MBA and OTS collapsed into one convention,
# loans observed before they were written, a cumulative curve that forgets,
# every cohort carried out to the same age, and an absorbing state allowed to
# roll out. Each was caught by the tier written for it.
#
# Headless, GI-independent, never skips (bar valgrind): pure gBASIC over core
# money, `lending`, `finance` and `fake`.

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER semantics\n'
if GBASIC_PATH=stdlib ./gbasic tests/credit_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "credit_test exits 0"
else
    fail "credit_test exits 0 ($(head -1 "$scratch/err"))"
fi
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "credit_test reports no mismatch"
else
    fail "credit_test reports no mismatch"; grep MISMATCH "$scratch/out" | head -5
fi
ran=$(sed -n 's/^checks: //p' "$scratch/out")
if [ "${ran:-0}" -ge 50 ]; then
    pass "credit_test ran at least 50 checks (ran ${ran:-0})"
else
    fail "credit_test ran at least 50 checks (ran ${ran:-0})"
fi

# Named individually so a regression says WHICH claim broke.
for label in \
    'every observed balance equals lending.apply at that date' \
    'no loan is observed before it was written' \
    'every migration reconciles, bucket by bucket' \
    'loans that paid off are in the matrix, not dropped' \
    'a loan that stops being observed is reported, not dropped' \
    'and the matrix still reconciles with them in it' \
    'every roll-rate row plus its attrition comes to one' \
    "an empty bucket's roll rate is unknown, not zero" \
    'the planted cohort'"'"'s bad rate at age 9 is exactly four in twenty' \
    'the cohort before it is clean' \
    'the cohort after it is clean' \
    'a cumulative curve never falls' \
    'the older cohort'"'"'s curve is longer' \
    'and the younger one stops rather than reporting zeroes' \
    'MBA and OTS disagree on the same loans' \
    'it is harsher on every row where they differ' \
    'the outstanding basis gives a different rate' \
    'a delinquent loan is in that denominator ONCE, not twice' \
    'gross and net are reported separately, never netted silently' \
    'charged-off loans stay charged off' \
    'an undeclared delinquency method is refused' \
    'a charged-off loan that cures is refused as a data defect' \
    'a migration between two populated dates is accepted'
do
    if command grep -Fq "ok   $label" "$scratch/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER reproducible across processes\n'
# A book generated here has to regenerate in CI, or the planted-portfolio
# tier proves nothing about anyone else's run. In-process determinism is not
# the claim; two separate processes agreeing is.
cat >"$scratch/gen.bas" <<'EOF'
load credit
load lending
load fake
a {date}= "2026-01-05"
b {date}= "2026-03-25"
specs = fake.portfolio(101, 12, { from: a, to: b, currency: "USD",
                                  median: 20000, sigma: 0.4, terms: [36] })
book = []
for each s in specs
    l = lending.loan({ principal: s.principal, rate: s.rate, term: s.term,
                       opened: s.opened, basis: "amortized",
                       waterfall: "fees_interest_principal", day_count: "30/360" })
    evs = []
    for k = 1 to 6
        append(evs, { on: l.opened + (1 month) * k, kind: "payment",
                      amount: lending.payment(l) })
    next
    append(book, { id: s.id, loan: l, events: evs })
next
obs = []
anchor {date}= "2026-01-01"
for k = 0 to 15
    append(obs, anchor + (1 month) * k)
next
for each r in credit.observe(book, obs, "mba")
    print r.id + "|" + string(r.as_of) + "|" + r.status + "|" + money.text(r.balance, 2)
next
print string(fake.sample(4242, 20, 4))
EOF
GBASIC_PATH=stdlib ./gbasic "$scratch/gen.bas" 2>/dev/null >"$scratch/r1"
GBASIC_PATH=stdlib ./gbasic "$scratch/gen.bas" 2>/dev/null >"$scratch/r2"
if [ -s "$scratch/r1" ] && cmp -s "$scratch/r1" "$scratch/r2"; then
    pass "two separate runs observe identically ($(wc -l <"$scratch/r1") rows)"
else
    fail "two separate runs observe identically"
fi

printf 'TIER valgrind\n'
# Small on purpose: the semantics fixture is a 12s program and the point here
# is the allocation paths, not the population size.
if vg_available; then
    cat >"$scratch/vg.bas" <<'EOF'
load credit
o {date}= "2026-01-01"
function row(id, as_of, status, bal)
    return { id: id, opened: o, as_of: as_of, status: status,
             balance: money.of("USD", bal) }
end function
t1 {date}= "2026-03-01"
t2 {date}= "2026-04-01"
tbl = [ row("A", t1, "current", "1000"), row("A", t2, "dpd_30", "1000"),
        row("B", t1, "dpd_30", "900"),   row("B", t2, "charged_off", "900"),
        row("C", t1, "current", "500"),  row("C", t2, "paid_off", "0"),
        row("D", t1, "dpd_60", "700") ]
m = credit.migration(tbl, t1, t2)
r = credit.roll_rates(tbl, t1, t2)
v = credit.vintage(tbl, { basis: "outstanding", cohort_by: "quarter" })
l = credit.losses(tbl, { cohort_by: "quarter" })
print string(m.total) + " " + string(count(v.cohorts)) + " " + string(l.count)
on error goto next
x = credit.bucket([], t1, "nope")
print error.message
error.clear()
on error stop
EOF
    if GBASIC_PATH=stdlib vg_run ./gbasic "$scratch/vg.bas" \
            >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_credit: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
