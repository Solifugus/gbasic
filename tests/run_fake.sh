#!/usr/bin/env bash
set -uo pipefail

# stdlib/fake.bas -- fabricated but realistic data (docs/fake_data_design.md).
#
# SELF-CHECKING, and here that is forced. A generator's failure mode is data
# that LOOKS FINE: a uniform amount column, an invoice naming a customer who
# does not exist, a total a cent away from its own lines. All three read as
# ordinary business data, so a golden would record them as expected and defend
# them.
#
# THE LOAD-BEARING TIER IS THE LAST ONE. Design §7 says the four kinds of
# consistency are proved together by driving `accounting` with the output: an
# unbalanced entry, a phantom account or a cross-currency line are each refused
# where they are posted, so a ledger that posts cleanly has DEMONSTRATED its
# consistency instead of claiming it. 500 generated invoices, every one posted,
# and the accounting equation asserted over the result.
#
# THE DISTRIBUTION TIER is what separates this from a name generator. Measured
# against Faker 39.0.0 on this machine: its `pyint` puts 11.0% of values in
# each leading digit, where Benford expects 30.1% for 1 -- so Faker data cannot
# exercise a Benford detector at all. fake.lognormal lands at ~30.8%, asserted
# as a band rather than a figure, since it is a distribution and not a value.
#
# ORDER INDEPENDENCE has its own tier because it is the property that keeps
# committed fixtures from rotting: row 47 must be the same row whether it was
# asked for directly or reached by walking, or adding a field to one generator
# shifts everything generated after it.
#
# Headless, GI-independent, never skips (bar valgrind): pure gBASIC over core
# money and the built-in RNG.

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER semantics\n'
if GBASIC_PATH=stdlib ./gbasic tests/fake_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "fake_test exits 0"
else
    fail "fake_test exits 0 ($(head -1 "$scratch/err"))"
fi
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "fake_test reports no mismatch"
else
    fail "fake_test reports no mismatch"; grep MISMATCH "$scratch/out" | head -5
fi
ran=$(sed -n 's/^checks: //p' "$scratch/out")
if [ "${ran:-0}" -ge 33 ]; then
    pass "fake_test ran at least 33 checks (ran ${ran:-0})"
else
    fail "fake_test ran at least 33 checks (ran ${ran:-0})"
fi

for label in \
    'row 47 is the same reached in order as asked for directly' \
    'the email derives from the name' \
    "lognormal leading digit 1 is near Benford's 30.1%" \
    'business dates never fall on a weekend' \
    'every invoice names a customer that exists' \
    'every total equals the sum of its own lines, to the cent' \
    'every postcode agrees with its region' \
    'amount in KWD carries that currency' \
    'JPY has no decimal places' \
    '2000 customers have 2000 distinct emails' \
    'the value generator underneath still repeats, by design' \
    'every generated invoice posts to a ledger' \
    'and the accounting equation holds over all of it'
do
    if command grep -Fq "ok   $label" "$scratch/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER reproducible across processes\n'
# Same seed, two SEPARATE runs, byte-identical output. In-process determinism
# is not enough: the point is a fixture generated here regenerating in CI.
cat >"$scratch/gen.bas" <<'EOF'
load fake
for i = 0 to 49
    p = fake.person(2026, i)
    c = fake.company(2026, i)
    print p.name + "|" + p.email + "|" + c.name + "|" + string(round(fake.lognormal(2026, i, 1000, 1.0), 4))
next
EOF
GBASIC_PATH=stdlib ./gbasic "$scratch/gen.bas" 2>/dev/null >"$scratch/r1"
GBASIC_PATH=stdlib ./gbasic "$scratch/gen.bas" 2>/dev/null >"$scratch/r2"
if [ -s "$scratch/r1" ] && cmp -s "$scratch/r1" "$scratch/r2"; then
    pass "two separate runs are byte-identical ($(wc -l <"$scratch/r1") rows)"
else
    fail "two separate runs are byte-identical"
fi

printf 'TIER scale\n'
# The point of a generator is populations bigger than anyone types.
cat >"$scratch/big.bas" <<'EOF'
load fake
start {date}= "2026-01-01"
finish {date}= "2026-12-31"
cust = fake.customers(5, 2000)
inv = fake.invoices(5, cust, 20000, start, finish, "USD")
print string(count(cust)) + " " + string(count(inv))
EOF
t0=$(date +%s)
out=$(GBASIC_PATH=stdlib ./gbasic "$scratch/big.bas" 2>/dev/null)
t1=$(date +%s)
if [ "$out" = "2000 20000" ]; then
    pass "22 000 rows generated in $((t1-t0))s"
else
    fail "22 000 rows generated (got '$out')"
fi
if [ $((t1-t0)) -le 60 ]; then
    pass "under the 60s ceiling"
else
    fail "under the 60s ceiling (took $((t1-t0))s)"
fi

printf 'TIER valgrind\n'
if vg_available; then
    cat >"$scratch/vg.bas" <<'EOF'
load fake
start {date}= "2026-01-01"
finish {date}= "2026-02-28"
cust = fake.customers(3, 20)
inv = fake.invoices(3, cust, 40, start, finish, "USD")
print string(count(inv))
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

printf '\nrun_fake: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
