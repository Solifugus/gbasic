#!/usr/bin/env bash
set -uo pipefail

# automation.execute / would / rehearsal / observe -- the THIRD increment of
# Business Automation Reasoning, from Recipe 6
# (docs/automation_recipe_06_should_we_act.md). It closes the architecture:
# until something could act, R5 (simulation as a precondition) and the
# ENFORCEMENT half of R6 had never been tested at all.
#
# THIS IS THE LAYER WHERE A DEFECT DOES NOT PRODUCE A WRONG NUMBER. It produces
# something HAPPENING that should not have. So the refusal tiers assert an
# ABSENCE, and they prove it with a side effect on disk -- the executor writes a
# marker file, and "nothing happened" means the file is not there. Trusting a
# return value would prove nothing about whether the executor ran.
#
# THE LOAD-BEARING TIER IS THE SHARED GATE. `would` and `execute` both call one
# `_gate` and neither holds a copy of the rules, because R5's entire argument is
# that a rehearsal tells you what the LIVE run will do. If the dry run and the
# live path could disagree, the rehearsal would be a statement about a different
# program. The fixture runs both over a six-case matrix and requires them to
# agree -- with a control that the matrix lands BOTH ways, since unanimous
# agreement on a matrix that is all refusals proves nothing.
#
# R5 IS CHECKED BEFORE AUTHORITY, deliberately, and there is a tier for it:
# approval does NOT substitute for a rehearsal, because a human asked to approve
# an unrehearsed process has nothing to approve on -- nobody knows how often it
# fires or how often it is wrong.
#
# AND THE RECIPE ITSELF IS ASSERTED, because its demonstration is the argument
# for the whole layer: the same process at two granularities, replayed over a
# year, where the coarse one MISSES the collapse it exists to find and every
# alarm it raises is false, while the fine one catches it and raises nothing
# else. Same code, same data. If that stops being true the recipe is wrong and
# R5 is unmotivated.
#
# RECIPE 7 IS ASSERTED HERE TOO, because it is the argument for R10 and it is
# the sharpest measurement in the whole lab: an intervention whose true effect
# is EXACTLY ZERO -- nothing in the fixture alters a number after the decision
# is taken -- shows a ~49% apparent "recovery", which is the number a learning
# loop would have stored, and is entirely regression to the mean. The cells
# nobody touched recovered just as well. Both halves are asserted, since the
# apparent recovery alone is only half the argument.
#
# Headless, GI-independent, never skips (bar valgrind). ~20s for the recipes.

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch" tests/.automation_marker' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER semantics\n'
if GBASIC_PATH=stdlib ./gbasic tests/automation_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "automation_test exits 0"
else
    fail "automation_test exits 0 ($(head -1 "$scratch/err"))"
fi
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "no mismatches"
else
    fail "no mismatches"
    grep "^MISMATCH" "$scratch/out" | head -10
fi
n=$(sed -n 's/^checks: //p' "$scratch/out")
if [ -n "$n" ] && [ "$n" -ge 56 ]; then
    pass "check count floor ($n checks)"
else
    fail "check count floor (got '${n:-none}', want >= 56)"
fi

# The executor leaves a file behind. If the fixture ever stops cleaning up, or
# stops writing at all, the absence tiers would be meaningless -- so check the
# marker is gone AND that the fixture proved it can create one.
if [ ! -e tests/.automation_marker ]; then
    pass "the fixture left no marker behind"
else
    fail "the fixture left no marker behind"
fi

printf 'TIER the load-bearing tiers ran\n'
for needle in \
    "the dry run and the live path agree on every case" \
    "  with the matrix landing both ways" \
    "approval does not substitute for a rehearsal" \
    "a rehearsed, within-authority decision DOES act" \
    "a decision beyond delegated authority may not act unattended" \
    "an action with no measured outcome is not evidence" \
    "a dry run that WOULD act still does not act" \
    "a measured but UNCONTROLLED outcome is still not evidence of an effect" \
    "  so the EFFECT is the difference, not the observation" \
    "  and the POLICY that permitted it" \
    "evidence read off it says what it was about" \
    "an action built on a decision that names no finding is refused" \
    "a well-formed action is accepted" \
    "automation refuses a misspelled context field too" \
    "and the correctly spelled one still passes the gate" \
    "short sequential keys land near the declared rate"
do
    if grep -qF "ok   $needle" "$scratch/out"; then
        pass "ran: $needle"
    else
        fail "ran: $needle"
    fi
done

printf 'TIER the recipe still demonstrates what it claims\n'
if GBASIC_PATH=stdlib ./gbasic examples/automation_lab/04_should_we_act.bas \
        >"$scratch/lab" 2>/dev/null; then
    pass "04_should_we_act runs"
else
    fail "04_should_we_act runs"
fi
if diff -u examples/automation_lab/04_should_we_act.out "$scratch/lab" >/dev/null 2>&1; then
    pass "04 matches its committed golden"
else
    fail "04 matches its committed golden"
    diff -u examples/automation_lab/04_should_we_act.out "$scratch/lab" | head -12
fi
# The demonstration itself: coarse misses and is all-false-alarm, fine catches
# and is clean. A golden alone would not say which line meant what.
if grep -qE "by region x category  12 +1 +false +1" "$scratch/lab"; then
    pass "the coarse configuration still misses the real event and fires falsely"
else
    fail "the coarse configuration still misses the real event and fires falsely"
    grep "region x category" "$scratch/lab"
fi
if grep -qE "plus store +60 +1 +true +0" "$scratch/lab"; then
    pass "the fine configuration still catches it with no false alarms"
else
    fail "the fine configuration still catches it with no false alarms"
    grep "plus store" "$scratch/lab"
fi
if [ "$(grep -c 'REFUSED' "$scratch/lab")" = 2 ] \
   && [ "$(grep -cE '^  .*acts ' "$scratch/lab")" = 2 ]; then
    pass "and the four gate cases still land two each way"
else
    fail "and the four gate cases still land two each way"
fi


printf 'TIER recipe 7: learning, and what it teaches when uncontrolled\n'
if GBASIC_PATH=stdlib ./gbasic examples/automation_lab/05_did_it_work.bas \
        >"$scratch/learn" 2>/dev/null; then
    pass "05_did_it_work runs"
else
    fail "05_did_it_work runs"
fi
if diff -u examples/automation_lab/05_did_it_work.out "$scratch/learn" >/dev/null 2>&1; then
    pass "05 matches its committed golden"
else
    fail "05 matches its committed golden"
    diff -u examples/automation_lab/05_did_it_work.out "$scratch/learn" | head -12
fi
# THE DEMONSTRATION. The intervention's true effect is exactly zero -- nothing
# in the fixture alters a number after the decision. If the uncontrolled
# measurement ever stops reporting a large apparent recovery, the recipe no
# longer demonstrates the trap and R10 is unmotivated.
apparent=$(sed -n 's/^    the intervention recovers \([0-9.]*\)%.*/\1/p' "$scratch/learn")
if [ -n "$apparent" ] && awk -v a="$apparent" 'BEGIN{exit !(a>25)}'; then
    pass "an intervention with ZERO effect still shows ${apparent}% apparent recovery"
else
    fail "an intervention with zero effect still shows a large apparent recovery (got '${apparent:-none}')"
fi
# And the holdout must recover too, or the comparison proves nothing.
holdout=$(sed -n 's/^    holdout recovered  \([0-9.-]*\)%.*/\1/p' "$scratch/learn")
if [ -n "$holdout" ] && awk -v h="$holdout" 'BEGIN{exit !(h>25)}'; then
    pass "and the cells nobody touched recovered ${holdout}% -- the comparison is what measures the effect"
else
    fail "the holdout also recovered (got '${holdout:-none}')"
fi

printf 'TIER recipe 12: the whole loop, in one program\n'
if GBASIC_PATH=stdlib ./gbasic examples/automation_lab/13_the_whole_loop.bas \
        >"$scratch/loop" 2>/dev/null; then
    pass "13_the_whole_loop runs"
else
    fail "13_the_whole_loop runs"
fi
if diff -u examples/automation_lab/13_the_whole_loop.out "$scratch/loop" >/dev/null 2>&1; then
    pass "13 matches its committed golden"
else
    fail "13 matches its committed golden"
    diff -u examples/automation_lab/13_the_whole_loop.out "$scratch/loop" | head -12
fi

# THE REASON THIS RECIPE EXISTS. Every other recipe executes a HAND-BUILT
# Decision -- recipes 9 and 11 print what `decision.evaluate` returned and then
# execute a hand-written one, so the decision->automation seam had never been
# driven by the real producer. `assurance_is` is the proof it was here: nothing
# but `evaluate` puts one on a Decision, and this line reads it back off an
# executed Action.
if grep -q "assured over               " "$scratch/loop"; then
    pass "the executed Action carries a Decision that decision.evaluate produced"
else
    fail "the executed Action carries a Decision that decision.evaluate produced"
fi
# And the rest of §9's chain, read off the same Action rather than asserted.
for needle in "the finding it came from" "which established" \
              "the policy that permitted it" "the rehearsal it rested on" \
              "what the executor reported"; do
    if grep -q "$needle" "$scratch/loop"; then
        pass "  and $needle"
    else
        fail "  and $needle"
    fi
done

# R5 bites before anything else can. A finding this strong with a sound
# decision behind it still may not run, and the reason names the rehearsal.
if grep -q "would it run? false -- needs rehearsal" "$scratch/loop"; then
    pass "R5 refuses the first month, with a real finding and a real decision"
else
    fail "R5 refuses the first month"
fi

# BOTH ARMS, or automation.assign proves nothing: a run that only ever acts has
# no counterfactual and a run that only ever holds back never tests anything.
acted=$(sed -n 's/^  acted on \([0-9]*\) cells.*/\1/p' "$scratch/loop")
held=$(sed -n 's/^.*deliberately held back \([0-9]*\).*/\1/p' "$scratch/loop")
if [ "${acted:-0}" -ge 1 ] && [ "${held:-0}" -ge 1 ]; then
    pass "the run both acted ($acted) and deliberately held back ($held)"
else
    fail "the run both acted and deliberately held back (acted=$acted held=$held)"
fi

# THE LOAD-BEARING ASSERTION, and it is falsifiable rather than a transcript.
# A treated cell recovers 0.62 of its gap and an untreated one 0.24, so the
# true incremental effect of acting is 0.38. The loop is required to RECOVER
# that from its own controlled outcomes -- which is the only check here that
# would fail if the chain were wired up correctly and measuring the wrong
# thing.
est=$(sed -n 's/^  calibrated from [0-9]* controlled outcomes: \([0-9.]*\).*/\1/p' "$scratch/loop")
if [ -n "$est" ] && awk -v e="$est" 'BEGIN{exit !(e>0.30 && e<0.46)}'; then
    pass "the loop recovers the true incremental effect (0.38): measured $est"
else
    fail "the loop recovers the true incremental effect 0.38 (got '${est:-none}')"
fi

# R9 still bites inside the loop: a month where nothing cleared cannot be
# decided at all, rather than being sized off the leader anyway.
if grep -q "Month 13, where nothing cleared, cannot be decided at all" "$scratch/loop" \
   && grep -q "did not establish" "$scratch/loop"; then
    pass "R9 refuses a month where nothing cleared"
else
    fail "R9 refuses a month where nothing cleared"
fi

# R17 as a DIFFERENCE between two runs over the SAME finding: the invented span
# and the measured interval must not report the same assurance, and the value
# must say which is which. A single assurance number proves neither.
inv=$(grep -A2 "sensitivity_range \[0, 2\]" "$scratch/loop" | grep -o "assurance [0-9.]*" | head -1)
mea=$(grep -A2 "the calibrated interval  " "$scratch/loop" | grep -o "assurance [0-9.]*" | head -1)
if [ -n "$inv" ] && [ -n "$mea" ] && [ "$inv" != "$mea" ]; then
    pass "the same finding reports different assurance under the two ranges ($inv vs $mea)"
else
    fail "the same finding reports different assurance under the two ranges (inv='$inv' mea='$mea')"
fi
if grep -q "from a range the caller declared" "$scratch/loop" \
   && grep -q "from the calibrated interval, from 3 controlled outcomes" "$scratch/loop"; then
    pass "  and each says where its range came from"
else
    fail "  and each says where its range came from"
fi


printf 'TIER valgrind\n'
if vg_available; then
    if GBASIC_PATH=stdlib vg_run ./gbasic tests/automation_test.bas \
            >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_automation: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
