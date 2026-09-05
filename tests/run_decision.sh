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
# RECIPE 10 IS ASSERTED HERE TOO, and it is the first recipe of a DIFFERENT
# SHAPE -- every other executable recipe is "a measure moved, where, which of
# these actions". Here the answer is a CONTINUOUS QUANTITY, there is no
# decomposition, the cost of inaction dominates, and a MODEL carries the
# parameter's uncertainty into the answer. That last part is what breaks
# things: p* = cost*b/(1+b) divides by (1+b), so an elasticity interval a
# shade over 1.5x wide becomes a price interval 13.5x wide, and an interval
# that reaches b = -1 has NO answer -- while a point estimate names one (60.7)
# with a straight face. Both halves asserted.
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
if [ -n "$n" ] && [ "$n" -ge 98 ]; then
    pass "check count floor ($n checks)"
else
    fail "check count floor (got '${n:-none}', want >= 98)"
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
    "uncontrolled observations may not be calibrated from" \
    "two interventions may not be pooled into one calibration" \
    "nor may two measures" \
    "one intervention on one measure pools" \
    "the definition travels with the number" \
    "    and the two report DIFFERENT assurance from the same decision" \
    "  yet a third off the sized-off quantity reverses it" \
    "a flip INSIDE the swept range is caught" \
    "a misspelled context field is refused BY NAME" \
    "  and so is one carrying a field only the other layer reads" \
    "an interval reaching where the model breaks yields NO quantity" \
    "  so amplification separates them" \
    "  and what a point estimate would have claimed"
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


printf 'TIER recipe 10: a decision whose answer is a quantity\n'
if GBASIC_PATH=stdlib ./gbasic examples/automation_lab/08_what_price.bas \
        >"$scratch/pr" 2>/dev/null; then
    pass "08_what_price runs"
else
    fail "08_what_price runs"
fi
if diff -u examples/automation_lab/08_what_price.out "$scratch/pr" >/dev/null 2>&1; then
    pass "08 matches its committed golden"
else
    fail "08 matches its committed golden"
    diff -u examples/automation_lab/08_what_price.out "$scratch/pr" | head -12
fi
# THE DEMONSTRATION, both halves. Case B: a parameter interval a shade over
# 1.5x wide becomes a price interval 13.5x wide, because the model divides by
# (1+b). Case C: no answer at all, and a point estimate names one anyway.
if grep -q "the elasticity interval is 1.52x wide and the price interval 13.5x" "$scratch/pr"; then
    pass "a 1.52x parameter interval still becomes a 13.5x price interval"
else
    fail "a 1.52x parameter interval still becomes a 13.5x price interval"
    grep "elasticity interval is" "$scratch/pr"
fi
if grep -q "would have answered 60.7 with a straight face" "$scratch/pr"; then
    pass "and the refused case still reports the number a point estimate would have named"
else
    fail "and the refused case still reports the number a point estimate would have named"
fi

printf 'TIER recipe 11: what was this evidence about\n'
if GBASIC_PATH=stdlib ./gbasic examples/automation_lab/12_what_was_this_about.bas \
        >"$scratch/ab" 2>/dev/null; then
    pass "12_what_was_this_about runs"
else
    fail "12_what_was_this_about runs"
fi
if diff -u examples/automation_lab/12_what_was_this_about.out "$scratch/ab" >/dev/null 2>&1; then
    pass "12 matches its committed golden"
else
    fail "12 matches its committed golden"
    diff -u examples/automation_lab/12_what_was_this_about.out "$scratch/ab" | head -12
fi
# THE DEMONSTRATION IS A DIFFERENCE BETWEEN TWO RUNS OVER THE SAME EVIDENCE,
# because a number from the pooled run alone is satisfied by any library that
# returns something. Asked one campaign at a time the answers are opposite --
# ACT on the manager, DO NOT ACT on the price cut. Pooled, they are the same
# answer, and it is the wrong one for both.
sep=$(sed -n '/PART A/,/PART B/p' "$scratch/ab" | grep -c "DO NOT ACT")
pooled=$(sed -n '/PART B/,/PART C/p' "$scratch/ab" | grep -c "DO NOT ACT")
if [ "$sep" = 1 ] && [ "$pooled" = 0 ]; then
    pass "separated, one campaign is refused; pooled, neither is"
else
    fail "separated, one campaign is refused; pooled, neither is (sep=$sep pooled=$pooled)"
fi
# AND THE SHARP HALF: the pooled interval does not merely go wrong, it goes
# CONFIDENTLY wrong, and gets more so with evidence. At n=120 the estimate must
# be clear of the 0.119 break-even while the truth it is estimating is 0.05.
if sed -n '/PART C/,/PART D/p' "$scratch/ab" | grep -q "cut the price.*estimate 0.198" \
   && sed -n '/PART C/,/PART D/p' "$scratch/ab" | grep -q "cut the price   ACT   assurance 1"; then
    pass "at n=120 the pooled answer is DECISIVE and recommends a loss-making action"
else
    fail "at n=120 the pooled answer is DECISIVE and recommends a loss-making action"
    sed -n '/PART C/,/PART D/p' "$scratch/ab" | grep "cut the price"
fi
# The interval must also have stopped containing either truth -- without this
# the previous check passes on a pooled estimate that is merely imprecise.
lo=$(sed -n '/PART C/,/PART D/p' "$scratch/ab" | grep -m1 "^  120 " | awk '{print $3}')
hi=$(sed -n '/PART C/,/PART D/p' "$scratch/ab" | grep -m1 "^  120 " | awk '{print $5}')
# Both must PARSE as numbers before they are compared: awk reads a non-numeric
# field as 0, so a mis-cut column would compare "to" against 0.05 and pass.
if [[ "$lo" =~ ^-?[0-9.]+$ && "$hi" =~ ^-?[0-9.]+$ ]] \
   && awk -v lo="$lo" -v hi="$hi" 'BEGIN{exit !(lo>0.05 && hi<0.35)}'; then
    pass "and its interval [$lo, $hi] contains neither 0.05 nor 0.35"
else
    fail "and its interval [$lo, $hi] contains neither 0.05 nor 0.35"
fi
# R16 with its CONTROL, in the recipe rather than only in the unit fixture:
# a rule that refused every pool would satisfy the two refusals above.
if grep -q "calibrating one intervention from another" "$scratch/ab" \
   && grep -q "these are not observations of one quantity" "$scratch/ab"; then
    pass "R16 refuses both a mixed intervention and a mixed measure, naming each"
else
    fail "R16 refuses both a mixed intervention and a mixed measure"
fi
if grep -q "12 outcomes from two regions, estimate" "$scratch/ab"; then
    pass "and the CONTROL still pools: one intervention, one measure, two places"
else
    fail "and the CONTROL still pools: one intervention, one measure, two places"
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
