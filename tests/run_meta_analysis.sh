#!/usr/bin/env bash
set -euo pipefail

# Meta-analysis in stats.bas -- combining findings ACROSS studies, medicine's
# central quantitative tool. It needed almost nothing new: the effect sizes it
# pools (cohens_d, hedges_g, odds_ratio) already existed, and what was missing
# was the pooling and the heterogeneity that decides whether pooling was
# defensible at all.
#
# THE EXPECTED VALUES ARE AN INDEPENDENT COMPUTATION, not a recorded run. The
# fixed and random estimates, Q, I-squared and tau-squared were computed in
# python from the same inputs and are stated in the fixture as literals. That
# mattered: my own hand arithmetic for the random-effects estimate was wrong in
# the 5th decimal and the code was right, which is precisely the case a golden
# cannot distinguish and a careful person cannot be trusted with.
#
# THE TRAP THIS EXISTS FOR: RATIO MEASURES POOL ON THE LOG SCALE. An odds,
# risk or hazard ratio is multiplicative -- 0.5 and 2.0 are the same size of
# effect in opposite directions, so their true pooled effect is NONE. Averaged
# as plain numbers they give 1.25, which reads as a 25% harm. The fixture
# asserts both: 1.25 raw and exactly 1.0 on the log scale. No inspection of the
# values could reveal the mistake -- a set of ratios and a set of raw
# differences are both just numbers -- so `scale: "ratio"` is explicit.
#
# One tier is an INVARIANT rather than a number: with identical studies
# tau-squared is zero, so the random and fixed models become the same estimator
# and must agree EXACTLY. That is a stronger statement than any expected value,
# and it is what catches a between-study variance that is quietly ignored.
#
# Proven red on four defects: a ratio scale that does not log-transform (3
# tiers), tau-squared forced to zero (3), random effects ignoring between-study
# variance (2), and a wrong I-squared (1).
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
./gbasic tests/meta_analysis_test.bas >"$work/out" 2>"$work/err" || true

if diff -u tests/meta_analysis_test.out "$work/out" >/dev/null; then
    pass 'meta_analysis_test matches its golden'
else
    fail 'meta_analysis_test matches its golden'
    diff -u tests/meta_analysis_test.out "$work/out" | head -30 || true
fi

if command grep -q MISMATCH "$work/out"; then
    fail 'meta_analysis_test reports no mismatch'
    command grep MISMATCH "$work/out" | head -10 || true
else
    pass 'meta_analysis_test reports no mismatch'
fi

reported="$(command grep '^checks: ' "$work/out" | sed 's/^checks: //')"
if [[ -n "$reported" ]] && [[ "$reported" -ge 36 ]]; then
    pass "meta_analysis_test ran at least 36 checks (ran $reported)"
else
    fail "meta_analysis_test ran at least 36 checks (ran '${reported:-none}')"
fi

if [[ -s "$work/err" ]]; then
    fail 'meta_analysis_test wrote nothing to stderr'
    head -5 "$work/err"
else
    pass 'meta_analysis_test wrote nothing to stderr'
fi

if [[ $failures -gt 0 ]]; then
    printf 'FAIL tests/run_meta_analysis.sh (%d of %d checks failed)\n' "$failures" "$checks"
    exit 1
fi
printf 'PASS tests/run_meta_analysis.sh (%d checks, %s assertions in the fixture)\n' "$checks" "$reported"
