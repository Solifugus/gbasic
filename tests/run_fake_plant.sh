#!/usr/bin/env bash
set -uo pipefail

# fake.plant -- a clean population with a KNOWN defect in a KNOWN place
# (docs/fake_data_design.md §6). Separate from run_fake.sh because it tests a
# different KIND of claim: the generators are judged on whether their output is
# realistic, planting on whether it is exactly and only what was asked for.
#
# SELF-CHECKING, not golden, and here that is forced. Every planting defect
# leaves a population that still looks like a population -- an anomaly on the
# wrong row, one anomaly too many, a clean row altered in passing -- so a
# golden would record the damaged data as expected and defend it.
#
# THE LOAD-BEARING PROPERTY IS "NO MARKER". The rows come back carrying nothing
# that says they were planted; the report is a SEPARATE value. A marker field
# would be a back door a detector could read, and a detector tested against
# data that labels its own anomalies has not been tested at all. That is also
# why a planted duplicate gets an id CONTINUING the population's sequence, and
# why a population whose ids cannot be continued is refused rather than given
# one that stands out.
#
# WHAT THIS IS FOR IS STILL AHEAD OF IT, and the design says so: there is no
# transaction-level detector in the tree today (`forensics` is
# financial-statement forensics -- accruals, M-score, Altman -- and has no
# Benford test). The nearer consumer is Phase 3 credit analytics. So the bar
# planting has to clear NOW is the one below: n anomalies land, on the rows the
# report names, and nothing else moves.
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
if GBASIC_PATH=stdlib ./gbasic tests/fake_plant_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "fake_plant_test exits 0"
else
    fail "fake_plant_test exits 0 ($(head -1 "$scratch/err"))"
fi
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "fake_plant_test reports no mismatch"
else
    fail "fake_plant_test reports no mismatch"; grep MISMATCH "$scratch/out" | head -5
fi
ran=$(sed -n 's/^checks: //p' "$scratch/out")
if [ "${ran:-0}" -ge 46 ]; then
    pass "fake_plant_test ran at least 46 checks (ran ${ran:-0})"
else
    fail "fake_plant_test ran at least 46 checks (ran ${ran:-0})"
fi

# The three claims §6 makes, plus the one the whole design rests on. Named
# individually so a regression says WHICH claim broke, not merely that one did.
for label in \
    'exactly six rows changed' \
    'and they are the rows the report names' \
    'only the amount moved on a planted row' \
    "a planted row has exactly its neighbours' fields" \
    'every planted amount is a round figure' \
    'and none of them was round before' \
    'every planted amount is strictly under the limit' \
    'every planted date falls on a weekend' \
    'no unplanted row drifted onto a weekend' \
    'every original row is untouched' \
    'a duplicate matches its source on party, amount and lines' \
    "a duplicate's id is shaped like every other id" \
    'every named row is actually absent' \
    'and every row NOT named is still there' \
    'planting five times left the original population alone' \
    'the same seed plants the same rows' \
    "\`avoid\` keeps a second plant off the first's rows" \
    'ids that cannot be continued are refused rather than given a marker' \
    'planting EVERY row is legal'
do
    if command grep -Fq "ok   $label" "$scratch/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER reproducible across processes\n'
# A planted fixture is only useful if it regenerates in CI. In-process
# determinism is not the claim; two separate processes agreeing is.
cat >"$scratch/gen.bas" <<'EOF'
load fake
start {date}= "2026-01-01"
finish {date}= "2026-06-30"
cust = fake.customers(11, 40)
inv = fake.invoices(11, cust, 300, start, finish, "USD")
for each kind in ["round_dollar", "weekend", "duplicate", "sequence_gap"]
    r = fake.plant(inv, { anomaly: kind, count: 9, at: 77 })
    for each p in r.planted
        print kind + "|" + string(p.id) + "|" + string(p.index) + "|" + string(p.now)
    next
next
EOF
GBASIC_PATH=stdlib ./gbasic "$scratch/gen.bas" 2>/dev/null >"$scratch/r1"
GBASIC_PATH=stdlib ./gbasic "$scratch/gen.bas" 2>/dev/null >"$scratch/r2"
if [ -s "$scratch/r1" ] && cmp -s "$scratch/r1" "$scratch/r2"; then
    pass "two separate runs plant identically ($(wc -l <"$scratch/r1") anomalies)"
else
    fail "two separate runs plant identically"
fi

printf 'TIER scale\n'
# 200 anomalies in 20 000 rows is the shape the feature exists for -- a
# population no one types, with a defect no one can find by reading.
cat >"$scratch/big.bas" <<'EOF'
load fake
start {date}= "2026-01-01"
finish {date}= "2026-12-31"
cust = fake.customers(5, 500)
inv = fake.invoices(5, cust, 20000, start, finish, "USD")
r = fake.plant(inv, { anomaly: "round_dollar", count: 200, at: 3 })
seen = { }
for each p in r.planted
    seen[string(p.index)] = true
next
print string(count(r.rows)) + " " + string(count(r.planted)) + " " + string(count(keys(seen)))
EOF
t0=$(date +%s)
out=$(GBASIC_PATH=stdlib ./gbasic "$scratch/big.bas" 2>/dev/null)
t1=$(date +%s)
# The third figure is the one that matters: 200 DISTINCT rows. A sampler that
# collided would report 200 anomalies on fewer rows and look perfectly fine.
if [ "$out" = "20000 200 200" ]; then
    pass "200 anomalies on 200 distinct rows of 20 000, in $((t1-t0))s"
else
    fail "200 anomalies on 200 distinct rows of 20 000 (got '$out')"
fi
if [ $((t1-t0)) -le 90 ]; then
    pass "under the 90s ceiling"
else
    fail "under the 90s ceiling (took $((t1-t0))s)"
fi

printf 'TIER valgrind\n'
if command -v valgrind >/dev/null 2>&1; then
    cat >"$scratch/vg.bas" <<'EOF'
load fake
start {date}= "2026-01-01"
finish {date}= "2026-02-28"
cust = fake.customers(3, 10)
inv = fake.invoices(3, cust, 60, start, finish, "USD")
total = 0
for each kind in ["round_dollar", "weekend", "duplicate", "sequence_gap"]
    r = fake.plant(inv, { anomaly: kind, count: 4, at: 9 })
    total = total + count(r.planted)
next
u = fake.plant(inv, { anomaly: "just_under", count: 4, at: 9, threshold: money.of("USD", "5000") })
print string(total + count(u.planted))
EOF
    if GBASIC_PATH=stdlib valgrind --error-exitcode=9 --leak-check=full \
            --errors-for-leak-kinds=definite ./gbasic "$scratch/vg.bas" \
            >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_fake_plant: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
