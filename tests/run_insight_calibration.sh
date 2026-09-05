#!/usr/bin/env bash
set -uo pipefail

# Does the threshold mean what the Finding says it means?
#
# A MANUAL TIER, deliberately, and named as one rather than quietly dropped:
# it runs ~250 searches over freshly generated null data and takes about five
# minutes. That is the honest cost of CHECKING a calibration rather than
# trusting its formula, and it is why nobody had done it.
#
# WHAT IT EXISTS FOR. Every Finding used to assert `alpha: 0.05`, and R1, R11,
# R12 and every `clears` verdict rest on that number. When it was finally
# measured the answer was NO -- 0.090 at 12 cells, 0.110 at 20 and 0.143 at 40
# against a requested 0.05, WORSENING as the search widened, which is the
# opposite of what a family-wise correction is for. The field is
# `alpha_requested` now, and this tier is what keeps the replacement claims
# honest.
#
# THE MEASUREMENT IS THE TEST. Run the search over data where NOTHING is
# planted and count how often ANY cell clears; that is the family-wise
# false-positive rate by definition. Deterministic -- every trial's data comes
# from an explicit seed -- so this is a slow computation, not a flaky one.
#
# THREE TIERS AND EACH NEEDS THE OTHERS.
#   * LIGHT-TAILED data through the t threshold lands at alpha (0.040
#     measured). Without it the next tier reads as "the correction is broken"
#     rather than "its assumption is unmet".
#   * HEAVY-TAILED data through the same threshold does not (0.093). This is
#     the documented limitation asserted as a FACT: `null.calibration` tells a
#     reader the t threshold delivers 0.10-0.13 on revenue-like data, so the
#     label itself is under test.
#   * The PERMUTED null on the same heavy-tailed data fires less often (0.080),
#     asserted as a DIFFERENCE against the t null rather than as a small
#     number -- because "the rate is small" also passes on a null that never
#     fires, which is exactly the bug the first attempt shipped. Sign-flipping
#     each cell's deviation leaves every |deviation| intact, so the threshold
#     landed ABOVE the statistic it was judging (3.724 against an observed
#     3.309) and the test fired 0 times in 200 trials. Hence the final tier:
#     it still fires. It is a threshold, not a mute button.
#
# AND A FOURTH TIER, ADDED 2026-09-04, MEASURING A DIFFERENT AXIS ENTIRELY:
# every tier above measures ONE search, and a monitoring process asks the same
# question every month. The correction has always been family-wise over the
# CELLS of one search -- twelve families, paid for once -- and nothing said so.
# MEASURED over a population with NOTHING wrong in it: 0.725 that some month
# raises a finding within the year. This is NOT the tail-weight problem above;
# even a perfectly calibrated 0.05 per run is 1 - 0.95^12 = 0.46 over a year,
# so it is arithmetic about repetition. Declaring `repetitions: 12` takes it to
# 0.175, and the tier asserts BOTH halves plus the honest third -- that it does
# NOT reach the requested 0.05, for the same tail-weight reason one level
# deeper into the tail.
#
# INSIGHT_CALIBRATION_FULL=1 raises the trial and draw counts for a real
# measurement rather than a regression check.

cd "$(dirname "$0")/.."
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER family-wise calibration (slow by nature)\n'
if GBASIC_PATH=stdlib ./gbasic tests/insight_calibration.bas \
        >"$scratch/out" 2>"$scratch/err"; then
    pass "insight_calibration exits 0"
else
    fail "insight_calibration exits 0 ($(head -1 "$scratch/err"))"
fi
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "no mismatches"
else
    fail "no mismatches"
    grep "^MISMATCH" "$scratch/out" | head -10
fi
sed -n 's/^ok   /  measured: /p' "$scratch/out" | grep "=" | head -8

n=$(sed -n 's/^checks: //p' "$scratch/out")
if [ -n "$n" ] && [ "$n" -ge 11 ]; then
    pass "check count floor ($n checks)"
else
    fail "check count floor (got '${n:-none}', want >= 11)"
fi

printf '\nrun_insight_calibration: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
