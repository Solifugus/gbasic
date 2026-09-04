#!/usr/bin/env bash
set -uo pipefail

# insight.explain_change + reasoning.bas -- the first increment of Business
# Automation Reasoning (docs/automation_reasoning_design.md §13, validated per
# §14).
#
# SELF-CHECKING, NOT GOLDEN, AND FORCED. Every defect this increment exists to
# prevent produces a CONFIDENT, ORDINARY-LOOKING CAUSAL STORY -- a chain of
# plausible percentages naming a place and a category. A golden would record
# one as expected and defend it, which is precisely how Recipe 1 showed the
# unguarded version failing.
#
# THE LOAD-BEARING TIER IS THE PLANTED/NULL PAIR: the same decomposition over a
# population with a real 55% collapse planted in a known cell, and over one
# with NOTHING planted, must reach OPPOSITE verdicts. Everything else checks a
# component; that checks the library can tell a cause from nothing, which is
# the only reason it exists.
#
# TWO CORRECTIONS WERE MADE BY BUILDING IT, both invisible to reading:
#
# 1. THE THRESHOLD WAS WRONG. Recipe 1 used sqrt(2 ln n) -- where the largest
#    of n draws lands ON AVERAGE -- so roughly half of all pure-noise
#    populations clear it. Measured: 6 of 13 seeds with nothing planted. That
#    is a coin flip with a formula in front of it. It is now the two-sided
#    Bonferroni quantile for a declared alpha, recorded in the Finding; the
#    same 13 seeds now clear 0 of 13.
#
# 2. THE SHARE REFUSAL BITES HARDER THAN EXPECTED, and correctly. R2 asks
#    whether the NET change is distinguishable from zero before reporting any
#    share of it. In Recipe 1's own data it is not (t = -1.03 over 60 cells) --
#    so "82.6% of the decline is Northeast" was a share of a decline that had
#    never been established, in the run where a cell really had collapsed.
#    That tier therefore carries a CONTROL, or a library that never reported a
#    share would satisfy it.
#
# Headless, GI-independent, never skips (bar valgrind): pure gBASIC over
# `frame`, `stats` and `fake`.

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER semantics\n'
if GBASIC_PATH=stdlib ./gbasic tests/insight_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "insight_test exits 0"
else
    fail "insight_test exits 0 ($(head -1 "$scratch/err"))"
fi
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "no mismatches"
else
    fail "no mismatches"
    grep "^MISMATCH" "$scratch/out" | head -10
fi

n=$(sed -n 's/^checks: //p' "$scratch/out")
if [ -n "$n" ] && [ "$n" -ge 77 ]; then
    pass "check count floor ($n checks)"
else
    fail "check count floor (got '${n:-none}', want >= 77)"
fi

# The named tiers must actually have run. Without this the floor above is
# satisfied by seventy-seven checks of anything.
printf 'TIER the load-bearing tiers ran\n'
for needle in \
    "the planted run leads with the planted cell" \
    "the quiet run's leader does NOT clear it" \
    "the threshold is well above sqrt(2 ln n), which is only the average max" \
    "a real aggregate decline DOES report shares" \
    "a Finding carrying materiality is refused, with the reason" \
    "and explicitly NOT an explanation" \
    "a shift common to every cell withholds shares" \
    "  and the SAME cell no longer clears under a common shift" \
    "a located decline is not common movement" \
    "  and the fall is monotone in how many others broke" \
    "allowing for them restores the statistic to the cell" \
    "  and saying so explicitly changes no answer"
do
    if grep -qF "ok   $needle" "$scratch/out"; then
        pass "ran: $needle"
    else
        fail "ran: $needle"
    fi
done

# --- RECIPE 3: a seasonal measure ----------------------------------------
#
# The design laboratory's answer to design §7's open question -- both nulls
# are cross-sectional and neither looks at time, so are they wrong under
# seasonality? The recipe compares December with January, which is the most
# ordinary seasonal comparison in commerce, over a population with nothing
# wrong in it and then over the same population with a real 45% collapse.
#
# The golden pins the transcript. These assertions pin what makes the
# transcript MEAN what the recipe says, each of which could stop being true
# while leaving a passing golden, because a golden records whatever came out.
printf 'TIER recipe 3 -- a seasonal measure\n'
if GBASIC_PATH=stdlib ./gbasic examples/automation_lab/09_a_seasonal_measure.bas \
        >"$scratch/r3" 2>"$scratch/r3err"; then
    pass "09_a_seasonal_measure runs"
else
    fail "09_a_seasonal_measure runs ($(head -1 "$scratch/r3err"))"
fi
if diff -u examples/automation_lab/09_a_seasonal_measure.out "$scratch/r3" >/dev/null 2>&1; then
    pass "output matches the committed golden"
else
    fail "output matches the committed golden"
    diff -u examples/automation_lab/09_a_seasonal_measure.out "$scratch/r3" | head -15
fi

# 1. THE LOAD-BEARING ONE. Runs A and B are the same population with and
#    without a real 45% collapse in it, compared December to January. If the
#    two runs stop agreeing on the leader and its z, the experiment is no
#    longer showing that seasonality BLINDS the test -- it is showing
#    something else, and the write-up would be wrong.
a_lead=$(sed -n '/RUN A/,/RUN B/p' "$scratch/r3" | grep -m1 'strength.leader')
b_lead=$(sed -n '/RUN B/,/RUN C/p' "$scratch/r3" | grep -m1 'strength.leader')
if [ -n "$a_lead" ] && [ "${a_lead%z*}" = "${b_lead%z*}" ]; then
    pass "a real 45% collapse does not change who leads"
else
    fail "a real 45% collapse does not change who leads ($a_lead / $b_lead)"
fi
if sed -n '/RUN B/,/RUN C/p' "$scratch/r3" | grep -q 'cells clearing      0'; then
    pass "and nothing clears in the run that has a real collapse in it"
else
    fail "and nothing clears in the run that has a real collapse in it"
fi

# 2. R13 must fire on the seasonal comparison and say why.
if sed -n '/RUN A/,/RUN B/p' "$scratch/r3" | grep -q 'common to the population'; then
    pass "run A withholds shares as a movement common to the population"
else
    fail "run A withholds shares as a movement common to the population"
fi

# 3. THE CONTROL, and without it the two above are satisfied by a library
#    that finds nothing anywhere. The SAME data compared January to January
#    must recover the planted cell, ranked first and clearing.
if sed -n '/RUN C/,/RUN D/p' "$scratch/r3" | grep -q 'West / Grocery      rank 1 '; then
    pass "compared like with like, the planted cell is recovered and ranked first"
else
    fail "compared like with like, the planted cell is recovered and ranked first"
fi
if sed -n '/RUN C/,/RUN D/p' "$scratch/r3" | grep -q 'strength.clears     YES'; then
    pass "and it clears"
else
    fail "and it clears"
fi

# 4. The permuted null, on a population it was never tuned against: it must
#    remove run D's false positive AND keep run C's true one. Either half
#    alone is satisfied by a threshold that never fires or never moves.
if sed -n '/RUN D/,/RUN E/p' "$scratch/r3" | grep -q 'strength.clears     YES' \
   && sed -n '/RUN F/,$p' "$scratch/r3" | grep -q 'strength.clears     no'; then
    pass "the permuted null removes the false positive the t threshold allows"
else
    fail "the permuted null removes the false positive the t threshold allows"
fi
if sed -n '/RUN E/,/RUN F/p' "$scratch/r3" | grep -q 'strength.clears     YES'; then
    pass "and keeps the true one"
else
    fail "and keeps the true one"
fi

# --- RECIPE 2: two causes at once ----------------------------------------
#
# Leave-one-out removes a cell from its own reference and nothing else. The
# recipe holds ONE cell literally constant -- the same collapse, the same
# change of -25,728 -- and varies only how many UNRELATED cells collapsed
# beside it. Anything that moves its verdict is a fact about its neighbours.
#
# The golden pins the two tables. These assertions pin what makes them mean
# what the write-up says, and the third is the one that keeps `max_causes`
# from being read as a repair rather than a trade.
printf 'TIER recipe 2 -- two causes at once\n'
if GBASIC_PATH=stdlib ./gbasic examples/automation_lab/10_two_causes_at_once.bas \
        >"$scratch/r2" 2>"$scratch/r2err"; then
    pass "10_two_causes_at_once runs"
else
    fail "10_two_causes_at_once runs ($(head -1 "$scratch/r2err"))"
fi
if diff -u examples/automation_lab/10_two_causes_at_once.out "$scratch/r2" >/dev/null 2>&1; then
    pass "output matches the committed golden"
else
    fail "output matches the committed golden"
    diff -u examples/automation_lab/10_two_causes_at_once.out "$scratch/r2" | head -15
fi

# 1. THE EXPERIMENT'S PREMISE. If the watched cell's change is not identical in
#    every run, the whole demonstration is about something else.
nchanges=$(sed -n '/others  watched change/,/^$/p' "$scratch/r2" \
           | awk '$1 ~ /^[0-9]+$/ {print $2}' | sort -u | wc -l)
if [ "$nchanges" = "1" ]; then
    pass "the watched cell's change is identical in all four runs"
else
    fail "the watched cell's change is identical in all four runs (got $nchanges values)"
fi

# 2. And its verdict is not.
if sed -n '/others  watched change/,/^$/p' "$scratch/r2" | grep -q 'FOUND' \
   && sed -n '/others  watched change/,/^$/p' "$scratch/r2" | grep -q 'nothing'; then
    pass "and its verdict flips, decided by cells it has nothing to do with"
else
    fail "and its verdict flips, decided by cells it has nothing to do with"
fi

# 3. THE LOAD-BEARING ONE. `max_causes` must be shown as a TRADE, not a fix:
#    the last row must FAIL to clear, or the recipe teaches that allowing for
#    more causes always recovers them, which is the opposite of what it found.
if sed -n '/others  max_causes/,/^$/p' "$scratch/r2" | grep -q 'FOUND' \
   && sed -n '/others  max_causes/,/^$/p' "$scratch/r2" | tail -3 | grep -q 'nothing'; then
    pass "allowing for more causes recovers some and, at the end, does not"
else
    fail "allowing for more causes recovers some and, at the end, does not"
fi

# 4. The bar must actually rise with what is allowed for. Without this, part 2
#    is satisfied by a threshold that never moved.
ths=$(sed -n '/others  max_causes/,/^$/p' "$scratch/r2" | awk 'NF==5 && $1 ~ /^[0-9]/ {print $4}')
if [ "$(printf '%s\n' "$ths" | sort -g | tr '\n' ' ')" = "$(printf '%s\n' "$ths" | tr '\n' ' ')" ] \
   && [ "$(printf '%s\n' "$ths" | sort -u | wc -l)" = "3" ]; then
    pass "and the threshold rises monotonically with max_causes"
else
    fail "and the threshold rises monotonically with max_causes ($ths)"
fi

# 5. The refusal is on the page, because it is half the design.
if grep -q 'max_causes above 1 needs' "$scratch/r2"; then
    pass "the t null refuses a trimmed reference, and says why"
else
    fail "the t null refuses a trimmed reference, and says why"
fi

printf 'TIER valgrind\n'
if vg_available; then
    cat >"$scratch/vg.bas" <<'EOF'
load insight
load reasoning
load frame
load fake
rows = []
i = 0
for each rg in ["A", "B", "C", "D"]
    for each c in ["p", "q", "r"]
        for d = 1 to 8
            for p = 0 to 1
                i = i + 1
                append(rows, { region: rg, category: c, period: p,
                               revenue: fake.lognormal(9, i, 500, 0.4),
                               stock: fake.lognormal(9, i + 90000, 100, 0.3) })
            next
        next
    next
next
f = insight.explain_change(frame.from_rows(rows),
      { measure: "revenue", period: "period", baseline: 0, current: 1,
        dimensions: ["region", "category"], comparison: "period_over_period",
        null: "siblings", associations: ["stock"] })
print string(f.search.cells) + " " + string(count(f.contributors))
print string(count(reasoning.provenance_complete(f)))
on error goto next
x = insight.explain_change(frame.from_rows(rows),
      { measure: "revenue", period: "period", baseline: 0, current: 1,
        dimensions: ["region"], comparison: "period_over_period", null: "nope" })
print error.message
error.clear()
x = reasoning.finding({ subject: "s", measure: "m", observation: { },
                        search: { cells: 3, width: 2 }, null: { kind: "siblings" },
                        strength: { }, contributors: [], provenance: { },
                        cause: "x" })
print error.message
error.clear()
on error stop
EOF
    if GBASIC_PATH=stdlib vg_run ./gbasic "$scratch/vg.bas" >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_insight: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
