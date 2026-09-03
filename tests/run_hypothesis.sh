#!/usr/bin/env bash
set -uo pipefail

# insight.weigh + reasoning.hypothesis -- recipe 8
# (docs/automation_recipe_08_why_might_it_be.md), the third rung of §4's causal
# ladder. Everything before it stopped at the first: `explain_change` says WHERE
# a change is concentrated and refuses to say why, and R3 had been pointing at
# machinery that did not exist.
#
# IT DOES NOT DETERMINE A CAUSE AND IS BUILT SO THAT IT CANNOT. Every hypothesis
# carries `explains: false` for its whole life; only a recorded test can promote
# one, which is R3 and is not this. What it produces is a QUESTION: what each
# candidate predicts, how well that matches what happened, and the observation
# that would tell the survivors apart.
#
# SELF-CHECKING AND FORCED. Every defect here produces a RANKED LIST OF
# PLAUSIBLE EXPLANATIONS -- names, numbers, an order. A golden would record
# whichever story came top as expected and defend it.
#
# THE LOAD-BEARING TIER IS R11: two hypotheses predicting THE SAME CELLS are not
# separated by this data, and ordering them would invent a preference the
# evidence does not support. They are reported tied, with both discriminators as
# the next test. Its CONTROL is a set that predicts differently and IS ranked,
# or "refuses to rank" would be satisfied by refusing to rank anything.
#
# NO PROBABILITY IS CLAIMED. The charter's §11 showed "inventory availability
# confidence .91"; nothing in the data supports a probability that a hypothesis
# is TRUE. What is computed is SET AGREEMENT between predicted and affected
# cells, reported under its own name with its definition attached -- and a tier
# asserts the definition travels with it.
#
# Parsimony is not imposed anywhere: a hypothesis predicting twelve cells and
# explaining one has over-predicted eleven, and set agreement says so on its
# own. The tier asserts the ordering falls out.
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
if GBASIC_PATH=stdlib ./gbasic tests/hypothesis_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "hypothesis_test exits 0"
else
    fail "hypothesis_test exits 0 ($(head -1 "$scratch/err"))"
fi
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "no mismatches"
else
    fail "no mismatches"
    grep "^MISMATCH" "$scratch/out" | head -10
fi
n=$(sed -n 's/^checks: //p' "$scratch/out")
if [ -n "$n" ] && [ "$n" -ge 26 ]; then
    pass "check count floor ($n checks)"
else
    fail "check count floor (got '${n:-none}', want >= 26)"
fi

printf 'TIER the load-bearing tiers ran\n'
for needle in \
    "the two identical predictions are reported as indistinguishable" \
    "distinct predictions are NOT reported as indistinguishable" \
    "the narrowest consistent hypothesis leads" \
    "agreement says what it is" \
    "a hypothesis with no discriminator is refused" \
    "weighing against a finding where NOTHING cleared is refused"
do
    if grep -qF "ok   $needle" "$scratch/out"; then
        pass "ran: $needle"
    else
        fail "ran: $needle"
    fi
done

printf 'TIER the recipe still demonstrates what it claims\n'
if GBASIC_PATH=stdlib ./gbasic examples/automation_lab/06_why_might_it_be.bas \
        >"$scratch/lab" 2>/dev/null; then
    pass "06_why_might_it_be runs"
else
    fail "06_why_might_it_be runs"
fi
if diff -u examples/automation_lab/06_why_might_it_be.out "$scratch/lab" >/dev/null 2>&1; then
    pass "06 matches its committed golden"
else
    fail "06 matches its committed golden"
    diff -u examples/automation_lab/06_why_might_it_be.out "$scratch/lab" | head -12
fi
# The demonstration: the two rival explanations tie, and the page says the data
# cannot decide. If that stops being true, R11 is unmotivated.
if grep -q "predict exactly the same cells" "$scratch/lab" \
   && grep -q "separable from rivals false" "$scratch/lab"; then
    pass "the two rival explanations still tie, and the page says so"
else
    fail "the two rival explanations still tie, and the page says so"
fi
if grep -q "explains: false" "$scratch/lab"; then
    pass "and no hypothesis claims to explain anything"
else
    fail "and no hypothesis claims to explain anything"
fi

printf 'TIER valgrind\n'
if vg_available; then
    if GBASIC_PATH=stdlib vg_run ./gbasic tests/hypothesis_test.bas \
            >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_hypothesis: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
