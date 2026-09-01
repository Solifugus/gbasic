#!/usr/bin/env bash
set -euo pipefail

# Causal inference in stats.bas: difference-in-differences (`did`,
# `pre_trends`) and instrumental variables (`iv_2sls`).
#
# THIS PAIR OF ESTIMATORS HAS A FAILURE MODE A GOLDEN CANNOT SEE, and it is
# the reason the suite is shaped the way it is: both can produce the RIGHT
# COEFFICIENT and a WRONG STANDARD ERROR. Nothing about the output looks off
# -- the estimate is the estimate you would defend in a meeting -- and a
# golden would record the wrong standard error as the expected value and then
# defend that forever.
#
#   * 2SLS run as two ordinary regressions (fit x on z, then y on x-hat) gives
#     the correct point estimate and computes its residuals from X-HAT. The
#     model's residuals are y - X*beta, against the ORIGINAL x. The fixture
#     performs the naive version alongside and pins that the two coefficients
#     agree TO TEN DIGITS while the standard errors do not -- in one dataset
#     the naive one is 1.78x too large, and in the mirror dataset, differing
#     only in the SIGN of the confounding, it is 2.7x too SMALL. The error is
#     not conservative, and which direction it takes depends on something the
#     analyst cannot observe.
#
#   * A difference-in-differences on panel data with serially correlated
#     outcomes understates its own uncertainty (Bertrand, Duflo &
#     Mullainathan 2004). The clustering fixture is thirty units over twenty
#     periods where the conventional standard error is 3.2x too small: it
#     reports p < 0.001 where clustering reports p > 0.10. Same estimate, to
#     twelve digits, on both.
#
# SO ALMOST EVERY NUMERIC CLAIM IN THE FIXTURE IS DERIVED A SECOND WAY inside
# the fixture rather than recorded from a run:
#   the DiD estimate    <- the four cell means, arithmetic
#   the CR1 covariance  <- ols_robust's HC1, a different formula in different
#                          code, which CR1 must equal exactly when every
#                          cluster holds one observation
#   the 2SLS estimate   <- the Wald ratio (y1-y0)/(x1-x0): four means, no
#                          matrix algebra at all
#   the 2SLS std error  <- sigma^2 / sum (xhat - mean xhat)^2, from the
#                          structural residuals
#   the first-stage F   <- t^2 from an ordinary `ols` first stage
#   Sargan's J          <- n * R^2 from `ols` of the residuals on the
#                          instruments
#   Wu-Hausman's F      <- two residual sums of squares from `ols`
#   the pre-trend F     <- likewise
#
# The exogenous-control fixture is laid out on nested cycles that divide 60
# exactly, so the instrument, the control and the confounder are orthogonal IN
# THIS SAMPLE rather than in expectation, and 2SLS must return 1.5 and 0.8 to
# six digits. Building it that way caught the first version, which used
# coprime-ish moduli and returned 1.41 -- a number that looks like sampling
# noise and was in fact a badly chosen fixture.
#
# One claim in the fixture is deliberately the OPPOSITE of the easy lesson:
# dropping an exogenous control that is orthogonal to the instrument does NOT
# bias 2SLS. It only widens the interval. Asserting the intuitive-but-wrong
# version would have been a test defending a misunderstanding.
#
# The pre-trend test is reported as what it is. A large p-value there is the
# absence of evidence against parallel trends over however many pre-periods
# the data happens to hold -- not evidence for it -- and `note` says so in
# words, which the fixture checks, because a library that hands back a
# reassuring p-value with no caveat is how an untestable assumption gets
# treated as tested.
#
# Headless, no network, no optional dependency. Never skips (bar valgrind).

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
make >/dev/null
export GBASIC_PATH=stdlib

failures=0
checks=0
pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

printf 'TIER semantics\n'
./gbasic tests/causal_test.bas >"$work/out" 2>"$work/err" || true

if diff -u tests/causal_test.out "$work/out" >/dev/null; then
    pass 'causal_test matches its golden'
else
    fail 'causal_test matches its golden'
    diff -u tests/causal_test.out "$work/out" | head -40 || true
fi

if command grep -q MISMATCH "$work/out"; then
    fail 'causal_test reports no mismatch'
    command grep MISMATCH "$work/out" | head -10 || true
else
    pass 'causal_test reports no mismatch'
fi

reported="$(command grep '^checks: ' "$work/out" | sed 's/^checks: //')"
if [[ -n "$reported" ]] && [[ "$reported" -ge 119 ]]; then
    pass "causal_test ran at least 119 checks (ran $reported)"
else
    fail "causal_test ran at least 119 checks (ran '${reported:-none}')"
fi

if [[ -s "$work/err" ]]; then
    fail 'causal_test wrote nothing to stderr'
    head -5 "$work/err"
else
    pass 'causal_test wrote nothing to stderr'
fi

# The fixture's own summary line, not a MISMATCH count. A fixture that dies
# halfway through prints no mismatches either, and an earlier suite in this
# tree passed a red proof for exactly that reason.
if command grep -qx 'mismatches: 0' "$work/out"; then
    pass 'causal_test finished and declared zero mismatches'
else
    fail 'causal_test finished and declared zero mismatches'
    tail -5 "$work/out"
fi

printf 'TIER independent-derivation coverage\n'
# The tiers above cannot tell a fixture that derives its answers from one that
# quotes them. These are the checks that make the suite worth more than a
# golden, named individually so deleting one fails here rather than silently
# shrinking what the suite proves.
for label in \
    'the regression coefficient equals the hand arithmetic' \
    'every standard error agrees to 1e-12' \
    'the 2SLS estimate IS the Wald ratio' \
    'the reported standard error matches it' \
    'the first-stage F is the instrument.s t squared' \
    'the first-stage F excludes the control' \
    'the reported F matches the augmented regression' \
    'Wu-Hausman restricts only the first-stage residuals' \
    'J is n times that regression.s R-squared' \
    'the reported F is that F'
do
    if command grep -Eq "^ok   $label\$" "$work/out"; then
        pass "derived independently: $label"
    else
        fail "derived independently: $label"
    fi
done

printf 'TIER the standard-error trap\n'
for label in \
    'naive and correct COEFFICIENTS agree to ten digits' \
    'the standard errors do not' \
    'here the naive one is 1.78x too LARGE' \
    'but now the naive error is 2.7x too SMALL' \
    'so the naive answer is not merely conservative' \
    'conventionally this is a discovery' \
    'clustered, the data cannot tell it from zero'
do
    if command grep -Eq "^ok   $label\$" "$work/out"; then
        pass "trap demonstrated: $label"
    else
        fail "trap demonstrated: $label"
    fi
done

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/causal_test.bas >/dev/null 2>"$work/vg" </dev/null; then
        pass 'causal_test is clean under valgrind'
    else
        fail 'causal_test is clean under valgrind'
        tail -20 "$work/vg"
    fi
else
    printf '  SKIP valgrind (not installed)\n'
fi

if [[ $failures -gt 0 ]]; then
    printf 'FAIL tests/run_causal.sh (%d of %d checks failed)\n' "$failures" "$checks"
    exit 1
fi
printf 'PASS tests/run_causal.sh (%d checks, %s assertions in the fixture)\n' "$checks" "$reported"
