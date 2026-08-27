#!/usr/bin/env bash
set -euo pipefail

# Event studies in stats.bas -- the standard tool of empirical finance, and the
# thing that turns an EDGAR filing date into a testable claim: estimate a
# normal-return model before an event, measure the residual across it, and ask
# whether the average across many events differs from zero.
#
# WHY THE FIXTURE IS CONSTRUCTED RATHER THAN SAMPLED. The series are built so
# the right answer is known EXACTLY: the asset is precisely 1.5x the market
# with zero alpha, and a single +4% shock is injected on the event day. A
# correct market model must therefore recover beta = 1.5, alpha = 0, and an
# abnormal return of exactly 0.04. Those are arithmetic, not observations, so
# the test can state them -- where a golden over real prices could only record
# that the numbers stopped changing.
#
# THE FOUR TRAPS, each with its own tier, and each producing a plausible NUMBER
# rather than an error if unguarded:
#
#   CALENDAR vs TRADING DAYS  A "2-day window" counted in calendar days lands
#       on a Saturday the market never traded. Windows index into the dates the
#       series actually has, so a window is always the requested number of
#       OBSERVATIONS.
#   AN EVENT WHEN THE MARKET WAS SHUT  News breaks at weekends. The event moves
#       to the next trading day and the result says it moved, rather than
#       silently taking the day before or dropping the event.
#   LOOK-AHEAD  An estimation window overlapping the event window fits the
#       "normal" return partly on the event being measured, biasing the
#       abnormal return toward zero. Refused.
#   UNEQUAL WINDOWS  Averaging a 3-day CAR with a 9-day CAR yields a number
#       with no interpretation. Refused.
#
# And one thing REPORTED rather than refused: contaminated estimation windows.
# When events cluster, one event's baseline can contain another event. On the
# constructed pair whose true CAAR is exactly 0.025, contamination produces
# 0.02455 -- close enough to read as ordinary noise, which is why it is
# surfaced by name. Not refused, because clustering is sometimes unavoidable
# and the literature's answer is to disclose it, not to discard the study.
#
# Proven red on three distinct defects: an estimation window overlapping the
# event (7 tiers fail), an off-by-one in event-day alignment (3), and unequal
# windows allowed to aggregate (1).
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
./gbasic tests/event_study_test.bas >"$work/out" 2>"$work/err" || true

if diff -u tests/event_study_test.out "$work/out" >/dev/null; then
    pass 'event_study_test matches its golden'
else
    fail 'event_study_test matches its golden'
    diff -u tests/event_study_test.out "$work/out" | head -30
fi

if command grep -q MISMATCH "$work/out"; then
    fail 'event_study_test reports no mismatch'
    command grep MISMATCH "$work/out" | head -10
else
    pass 'event_study_test reports no mismatch'
fi

reported="$(command grep '^checks: ' "$work/out" | sed 's/^checks: //')"
if [[ -n "$reported" ]] && [[ "$reported" -ge 36 ]]; then
    pass "event_study_test ran at least 36 checks (ran $reported)"
else
    fail "event_study_test ran at least 36 checks (ran '${reported:-none}')"
fi

if [[ -s "$work/err" ]]; then
    fail 'event_study_test wrote nothing to stderr'
    head -5 "$work/err"
else
    pass 'event_study_test wrote nothing to stderr'
fi

printf 'TIER composition\n'
# The point of the whole exercise: a price frame from `market` feeds this
# without an adapter. Offline fixture, so no network.
cat > "$work/compose.bas" <<'EOF'
load market
load stats
from_d{date}= "2024-01-01"
to_d{date}= "2024-01-31"
m = market.offline(market.stooq(), "tests/market_fixtures")
r = market.daily(m, "AAPL", from_d, to_d)
if not r.ok then
    print "FETCH FAILED"
else
    rets = stats.simple_returns(r.frame["close"])
    w = stats.event_window(r.frame["date"], r.frame["date"][2], 1, 1)
    print "returns=" + string(count(rets)) + " window_ok=" + string(w.ok) + " index=" + string(w.index)
end if
EOF
if out="$(./gbasic "$work/compose.bas" 2>&1)"; then
    if [[ "$out" == *"window_ok=true"* ]] && [[ "$out" == *"index=2"* ]]; then
        pass "a market frame feeds event_window with no adapter ($out)"
    else
        fail "a market frame feeds event_window with no adapter (got: $out)"
    fi
else
    fail "a market frame feeds event_window with no adapter (it raised: $out)"
fi

if [[ $failures -gt 0 ]]; then
    printf 'FAIL tests/run_event_study.sh (%d of %d checks failed)\n' "$failures" "$checks"
    exit 1
fi
printf 'PASS tests/run_event_study.sh (%d checks, %s assertions in the fixture)\n' "$checks" "$reported"
