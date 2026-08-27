#!/usr/bin/env bash
set -euo pipefail

# Survival analysis in stats.bas -- Kaplan-Meier, Greenwood errors, and the
# log-rank test. Time until an event, when for some subjects it has not
# happened YET, which is the situation medicine, reliability engineering,
# churn and credit default all share and none of the ordinary tools handle.
#
# THE ORACLE IS EXTERNAL AND PUBLISHED. The fixture is Freireich et al. 1963,
# the 6-mercaptopurine leukaemia remission trial that every survival textbook
# works through, and the expected values are its PUBLISHED results rather than
# numbers recorded from this implementation: median remission 23 weeks on 6-MP
# against 8 on placebo, S(10) = 0.7529, S(23) = 0.4482, log-rank chi-square
# 16.79 on 1 df. Agreeing with an outside source is a much stronger claim than
# agreeing with yesterday's run of the same code.
#
# CENSORING IS THE SUBJECT, and the fixture proves it by doing the two obvious
# shortcuts on purpose. The same data gives a median of:
#     23  handled correctly
#     10  if censored subjects are DROPPED      (only failures remain)
#     16  if they are counted as EVENTS         (failures that never happened)
# Every one of those is an ordinary-looking number, and nothing about the two
# wrong ones announces itself.
#
# A NOTE ON HOW THIS SUITE WAS BUILT, because it is the useful part. The first
# version passed, and then red-proofing found that TWO of its tiers asserted
# nothing: breaking the median boundary (`<` for `<=`) and breaking the
# Greenwood variance both left every check green. The median tier could not see
# it because the Freireich curve never touches 0.5 exactly, and the Greenwood
# tier only asserted the error was POSITIVE -- which a wrong formula also is.
# Both are now pinned to independently computed VALUES, plus a four-subject
# fixture whose survival lands exactly on one half. All four defects fail now:
# risk set (5 tiers), Greenwood (3), median boundary (1), log-rank variance (1).
#
# COX is here too: `cox_ph` fits the partial likelihood through the
# derivative-free optimiser with numerically differentiated standard errors,
# and reproduces the PUBLISHED fit of the same trial to four decimals --
# beta 1.5092, hazard ratio 4.523, se 0.4096, z 3.685, p 0.00023.
#
# A note on the red proofs, because one of them lied. Removing Breslow's tie
# correction first reported ZERO mismatches, which read as a tier that asserts
# nothing. It is not: the change makes the likelihood degenerate and the
# program DIES on a division by zero, so no MISMATCH line is ever printed.
# Counting mismatches alone cannot tell a pass from a crash. The proof now
# requires the fixture's own summary line to exist, and the runner catches that
# defect three ways -- the golden diff, the check-count floor, and the tier
# asserting stderr is empty.
#
# Headless, no network, no optional dependency. Never skips.

cd "$(dirname "$0")/.."
make >/dev/null
export GBASIC_PATH=stdlib

failures=0
checks=0
pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

printf 'TIER semantics\n'
./gbasic tests/survival_test.bas >"$work/out" 2>"$work/err" || true

if diff -u tests/survival_test.out "$work/out" >/dev/null; then
    pass 'survival_test matches its golden'
else
    fail 'survival_test matches its golden'
    diff -u tests/survival_test.out "$work/out" | head -30 || true
fi

if command grep -q MISMATCH "$work/out"; then
    fail 'survival_test reports no mismatch'
    command grep MISMATCH "$work/out" | head -10 || true
else
    pass 'survival_test reports no mismatch'
fi

reported="$(command grep '^checks: ' "$work/out" | sed 's/^checks: //')"
if [[ -n "$reported" ]] && [[ "$reported" -ge 65 ]]; then
    pass "survival_test ran at least 65 checks (ran $reported)"
else
    fail "survival_test ran at least 65 checks (ran '${reported:-none}')"
fi

if [[ -s "$work/err" ]]; then
    fail 'survival_test wrote nothing to stderr'
    head -5 "$work/err"
else
    pass 'survival_test wrote nothing to stderr'
fi

if [[ $failures -gt 0 ]]; then
    printf 'FAIL tests/run_survival.sh (%d of %d checks failed)\n' "$failures" "$checks"
    exit 1
fi
printf 'PASS tests/run_survival.sh (%d checks, %s assertions in the fixture)\n' "$checks" "$reported"
