#!/usr/bin/env bash
set -euo pipefail

# Exploratory factor analysis in stats.bas -- principal-axis factoring with
# iterated communalities and a varimax rotation.
#
# FACTOR ANALYSIS IS NOT PCA, and the suite is built around being able to tell.
# PCA summarises the observed variables and explains TOTAL variance; factor
# analysis posits latent causes and explains COMMON variance only. In the
# arithmetic that is one diagonal: 1s for PCA, communalities for FA.
#
# THE FIXTURE HAS A KNOWN STRUCTURE, built rather than sampled: six variables,
# the first three driven by one latent factor and the last three by another,
# independent. A correct fit must recover exactly that block pattern, asserted
# as a PROPERTY over all six variables rather than as recorded digits.
#
# That paid for itself immediately -- it caught two bugs that both produced
# converged, entirely plausible output:
#   * eigenvectors indexed TRANSPOSED (`vectors` is eigenvector-first, as `pca`
#     reads it), which gave Heywood cases on every other variable;
#   * a varimax criterion using the wrong two of its four sums, which left
#     every loading at 0.7 -- the LEAST simple structure there is -- while
#     still converging and reporting sane communalities.
#
# AND ONE TIER EXISTS BECAUSE A RED PROOF CAME BACK GREEN. Replacing the
# communalities with 1s -- turning the method into PCA, the single most
# important thing this code must not do -- passed every check. The clean
# fixture could not see it: its communalities are already 0.98, so the two
# methods nearly coincide there. Telling them apart needs data with real
# unique variance, so a second fixture is half noise, where factor analysis
# reports communalities near 0.40 and leaving 1s on the diagonal reports 0.60
# -- a 50% overstatement of what the latent factors explain. That tier fails
# on the substitution; nothing else did.
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
./gbasic tests/factor_analysis_test.bas >"$work/out" 2>"$work/err" || true

if diff -u tests/factor_analysis_test.out "$work/out" >/dev/null; then
    pass 'factor_analysis_test matches its golden'
else
    fail 'factor_analysis_test matches its golden'
    diff -u tests/factor_analysis_test.out "$work/out" | head -30
fi

if command grep -q MISMATCH "$work/out"; then
    fail 'factor_analysis_test reports no mismatch'
    command grep MISMATCH "$work/out" | head -10
else
    pass 'factor_analysis_test reports no mismatch'
fi

reported="$(command grep '^checks: ' "$work/out" | sed 's/^checks: //')"
if [[ -n "$reported" ]] && [[ "$reported" -ge 26 ]]; then
    pass "factor_analysis_test ran at least 26 checks (ran $reported)"
else
    fail "factor_analysis_test ran at least 26 checks (ran '${reported:-none}')"
fi

if [[ -s "$work/err" ]]; then
    fail 'factor_analysis_test wrote nothing to stderr'
    head -5 "$work/err"
else
    pass 'factor_analysis_test wrote nothing to stderr'
fi

if [[ $failures -gt 0 ]]; then
    printf 'FAIL tests/run_factor_analysis.sh (%d of %d checks failed)\n' "$failures" "$checks"
    exit 1
fi
printf 'PASS tests/run_factor_analysis.sh (%d checks, %s assertions in the fixture)\n' "$checks" "$reported"
