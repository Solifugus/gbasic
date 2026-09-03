#!/usr/bin/env bash
set -uo pipefail

# decision.evaluate -- the SECOND increment of Business Automation Reasoning,
# from Recipe 5 (docs/automation_recipe_05_what_to_do.md), which is the first
# recipe to cross this boundary at all. Recipes 1-4 all stop at the finding,
# which left two thirds of the architecture unexamined.
#
# SELF-CHECKING AND FORCED, and for a sharper reason than the insight suite:
# every defect here produces a DEFENSIBLE-LOOKING RECOMMENDATION -- an option, an
# expected value, a rationale. A golden would record "restock and promote" as
# expected and defend it whether or not the number underneath it had ever been
# established.
#
# THE LOAD-BEARING TIER IS R9, the refusal Recipe 5 produced. Measured: the same
# intervention over the same data gives expected value +7,497 sized off the
# aggregate decline and -4,899 sized off the cell that actually cleared -- ACT
# against DO NOT ACT -- and the Finding had ALREADY said the aggregate was not
# established. So a decision may not be sized off a quantity its own evidence
# declined to establish, and that is refused at the boundary rather than
# reported for a reader to notice. It carries a control, or it would be
# satisfied by refusing everything: the SAME finding sized off the cell that DID
# clear is accepted, and a finding whose aggregate IS established may use it.
#
# R6 IS THE OTHER ONE: the recommendation is the best alternative, NOT the best
# AFFORDABLE one. A layer that quietly returned the affordable option would hide
# the only choice a human needs to make -- and would pass every other check
# here, which is why there is a tier asserting a cheaper affordable option
# existed and was not chosen.
#
# A BUG THIS SUITE DID NOT CATCH, recorded because the lesson is the assertion
# and not the bug: the first sensitivity sweep cancelled each alternative's own
# recovery, so a cheap intervention and an expensive one got identical benefit
# and the cheap one always won. It reported `assurance` 0 for a recommendation
# it never once picked -- and `assurance < 1` plus `sensitivities` non-empty,
# which is what the fixture asserted, are both satisfied by exactly that. It was
# found by printing the values, not by the suite. Two things fix it: the library
# now RAISES if the sweep disagrees with the point estimate at the nominal
# assumption, and the fixture asserts assurance as a DIFFERENCE between a robust
# recommendation and a marginal one (0.95 against 0.43) rather than as a number
# in range.
#
# RECIPE 9 IS ASSERTED HERE TOO. It turns the loop -- controlled evidence
# becomes the assumption -- and answers "how much evidence is enough" the only
# way the question has an answer: relative to A DECISION. Sending a manager
# costs 2000 against a 16835 loss, so it breaks even at a recovery of 0.119;
# enough evidence is when the interval stops straddling that. Case A (true
# effect 0.35) settles on TWO observations. Case B (true effect 0.15, a hair
# above break-even) does not settle on THIRTY. Both halves are asserted,
# because either verdict alone would prove nothing about sufficiency.
#
# IT ALSO RETIRES SOMETHING INVENTED. Recipe 5's `sensitivity_range: [0, 2]`
# was a span I chose; a calibration supplies the range the EVIDENCE supports,
# so assurance becomes the share of the plausible interval over which the
# recommendation survives.
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
if GBASIC_PATH=stdlib ./gbasic tests/decision_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "decision_test exits 0"
else
    fail "decision_test exits 0 ($(head -1 "$scratch/err"))"
fi
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "no mismatches"
else
    fail "no mismatches"
    grep "^MISMATCH" "$scratch/out" | head -10
fi
n=$(sed -n 's/^checks: //p' "$scratch/out")
if [ -n "$n" ] && [ "$n" -ge 50 ]; then
    pass "check count floor ($n checks)"
else
    fail "check count floor (got '${n:-none}', want >= 50)"
fi

printf 'TIER the load-bearing tiers ran\n'
for needle in \
    "sizing off an unestablished aggregate is refused" \
    "the same finding sized off the cell that cleared is accepted" \
    "a cheaper affordable option existed and was NOT chosen" \
    "with no threshold declared, materiality is unknown" \
    "so assurance distinguishes them" \
    "a Decision that decides its own permission is refused" \
    "more evidence gives a narrower interval" \
    "evidence far from the break-even is decisive" \
    "the SAME amount of evidence near it is not" \
    "uncontrolled observations may not be calibrated from"
do
    if grep -qF "ok   $needle" "$scratch/out"; then
        pass "ran: $needle"
    else
        fail "ran: $needle"
    fi
done

printf 'TIER the recipe still demonstrates what it claims\n'
# Recipe 5's whole point is that the two sizings pick OPPOSITE actions. If that
# stops being true the write-up is wrong and the refusal is unmotivated.
if GBASIC_PATH=stdlib ./gbasic examples/automation_lab/03_what_to_do.bas \
        >"$scratch/lab" 2>/dev/null; then
    pass "03_what_to_do runs"
else
    fail "03_what_to_do runs"
fi
if diff -u examples/automation_lab/03_what_to_do.out "$scratch/lab" >/dev/null 2>&1; then
    pass "03 matches its committed golden"
else
    fail "03 matches its committed golden"
    diff -u examples/automation_lab/03_what_to_do.out "$scratch/lab" | head -12
fi
if [ "$(grep -c '\-> ACT' "$scratch/lab")" = 1 ] \
   && [ "$(grep -c '\-> DO NOT ACT' "$scratch/lab")" = 1 ]; then
    pass "the two sizings still pick opposite actions"
else
    fail "the two sizings still pick opposite actions"
fi
if grep -q "best option overall:            restock and promote" "$scratch/lab" \
   && grep -q "best option within authority:   send a regional manager" "$scratch/lab"; then
    pass "and the best option is still beyond authority, so R6 has something to bite on"
else
    fail "and the best option is still beyond authority"
fi


printf 'TIER recipe 9: how much evidence is enough\n'
if GBASIC_PATH=stdlib ./gbasic examples/automation_lab/07_how_much_evidence.bas \
        >"$scratch/ev" 2>/dev/null; then
    pass "07_how_much_evidence runs"
else
    fail "07_how_much_evidence runs"
fi
if diff -u examples/automation_lab/07_how_much_evidence.out "$scratch/ev" >/dev/null 2>&1; then
    pass "07 matches its committed golden"
else
    fail "07 matches its committed golden"
    diff -u examples/automation_lab/07_how_much_evidence.out "$scratch/ev" | head -12
fi
# THE DEMONSTRATION: sufficiency is distance from the break-even, not n. Case A
# settles on TWO observations; case B does not settle on THIRTY. If either half
# stops being true the recipe no longer says what it claims.
case_a=$(sed -n '/CASE A/,/CASE B/p' "$scratch/ev" | grep -c "DECISIVE: the recommendation holds")
if [ "$case_a" = 3 ]; then
    pass "case A is decisive at every sample size, including n=2"
else
    fail "case A is decisive at every sample size (got $case_a of 3)"
fi
if sed -n '/CASE B/,$p' "$scratch/ev" | grep -q "n = 30" \
   && sed -n '/n = 30/,+3p' "$scratch/ev" | grep -q "NOT DECISIVE"; then
    pass "and case B is still undecided at n=30"
else
    fail "and case B is still undecided at n=30"
fi

printf 'TIER valgrind\n'
if vg_available; then
    cat >"$scratch/vg.bas" <<'EOF'
load decision
load reasoning
f = { subject: "revenue", measure: "revenue",
      observation: { baseline: 100, current: 80, change: 0 - 20, change_pct: 0 - 0.2 },
      search: { dimensions: ["r"], cells: 6, width: 3, alpha: 0.05 },
      null: { kind: "siblings", mean: 0 - 3, sd: 4, threshold: 3 },
      strength: { z: 0 - 4, clears: true, leader: ["r1"] },
      contributors: [{ path: ["r1"], change: 0 - 12, share: unknown, z: 0 - 4, clears: true }],
      shares_reportable: false,
      shares_withheld_because: "not distinguishable from zero",
      associations: [], hypotheses: [],
      provenance: { method: "m", rows: 1, parameters: { }, assumptions: [] } }
ctx = { objectives: [{ measure: "revenue", direction: "maximize" }],
        thresholds: { revenue: 5 }, authority: { spend_limit: 3 } }
opts = [{ name: "nothing", cost: 0, benefit: 0 },
        { name: "act", cost: 4, recovers: 0.8 }]
d = decision.evaluate(f, ctx, opts, { sizing: "leading_cell", sensitivity_range: [0, 2] })
print d.recommendation + " " + string(d.authority_required) + " " + string(round(d.assurance, 3))
on error goto next
x = decision.evaluate(f, ctx, opts, { sizing: "aggregate", sensitivity_range: [0, 2] })
print error.message
error.clear()
x = decision.evaluate(f, ctx, opts, { sizing: "leading_cell", sensitivity_range: [2, 3] })
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

printf '\nrun_decision: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
