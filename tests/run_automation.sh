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
# Headless, GI-independent, never skips (bar valgrind). ~10s for the recipe.

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
if [ -n "$n" ] && [ "$n" -ge 28 ]; then
    pass "check count floor ($n checks)"
else
    fail "check count floor (got '${n:-none}', want >= 28)"
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
    "a dry run that WOULD act still does not act"
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
