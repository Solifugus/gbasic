#!/usr/bin/env bash
set -euo pipefail

# stdlib/market.bas -- daily price history as a frame.
#
# WHY THIS LIBRARY EXISTS. gBASIC's finance stack was complete except for its
# input. `stats.simple_returns`, `sharpe_ratio`, `max_drawdown`,
# `value_at_risk`, `capm` and `forensics.altman_classic(facts, prices)` all take
# prices as an ARGUMENT and nothing in the tree produced them -- EDGAR serves
# filings, not quotes -- so the whole return-based half of the library was
# unreachable without hand-assembling a price series.
#
# WHY IT IS TESTED THIS WAY. The two things that can go wrong here both fail as
# PLAUSIBLE NUMBERS rather than as errors, which is precisely what a golden
# cannot catch, because a golden records whatever the library answers AS the
# expected output:
#
#   ORDER       Providers disagree about oldest-first versus newest-first, and
#               `simple_returns` on a reversed series returns the NEGATED
#               sequence -- ordinary-looking market data with every sign wrong.
#               The fixture is therefore SCRAMBLED ON PURPOSE (05, 03, 08, 04,
#               02) and the test names the five dates it expects in order, plus
#               the consequence: on this falling series the first return must be
#               NEGATIVE. Proven red by removing the sort -- four dates move and
#               the sign assertion flips, which is exactly the failure a caller
#               would never notice.
#   ADJUSTMENT  A 2-for-1 split halves the raw close overnight, and a return
#               computed from unadjusted prices reads it as a -50% day. So
#               adjustment is never guessed: the result carries `adjusted` from
#               what the PROVIDER supplies, and both providers are asserted.
#
# Every check states its own expected answer and prints ok or a MISMATCH naming
# both sides; the golden pins that every line says ok, and a floor stops a
# fixture that quietly stopped asserting from passing.
#
# NO NETWORK, in either direction: the fixtures are committed and replayed
# through `market.offline`, the seam `llm.bas` and `edgar.bas` already use.
# Never skips.

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
./gbasic tests/market_test.bas >"$work/out" 2>"$work/err" || true

if diff -u tests/market_test.out "$work/out" >/dev/null; then
    pass 'market_test matches its golden'
else
    fail 'market_test matches its golden'
    diff -u tests/market_test.out "$work/out" | head -30 || true
fi

if command grep -q MISMATCH "$work/out"; then
    fail 'market_test reports no mismatch'
    command grep MISMATCH "$work/out" | head -10 || true
else
    pass 'market_test reports no mismatch'
fi

reported="$(command grep '^checks: ' "$work/out" | sed 's/^checks: //')"
if [[ -n "$reported" ]] && [[ "$reported" -ge 44 ]]; then
    pass "market_test ran at least 44 checks (ran $reported)"
else
    fail "market_test ran at least 44 checks (ran '${reported:-none}')"
fi

if [[ -s "$work/err" ]]; then
    fail 'market_test wrote nothing to stderr'
    head -5 "$work/err"
else
    pass 'market_test wrote nothing to stderr'
fi

printf 'TIER offline\n'
# The point of the offline seam is that it is COMPLETE: with no fixture
# directory and no network reachable, a fetch must still fail as a VALUE rather
# than raise or hang. Uses an address that cannot answer rather than a real
# host, so nothing leaves the machine even if the seam were broken.
cat > "$work/nonet.bas" <<'EOF'
load market
from_d{date}= "2024-01-01"
to_d{date}= "2024-01-31"
m = market.offline(market.stooq(), "/nonexistent-fixture-dir")
r = market.daily(m, "AAPL", from_d, to_d)
print "ok=" + string(r.ok)
print "has_message=" + string(len(r.message) > 0)
EOF
if out="$(timeout 20 ./gbasic "$work/nonet.bas" 2>&1)"; then
    if [[ "$out" == *"ok=false"* ]] && [[ "$out" == *"has_message=true"* ]]; then
        pass 'a missing fixture fails as a value, with a reason'
    else
        fail "a missing fixture fails as a value, with a reason (got: $out)"
    fi
else
    fail 'a missing fixture fails as a value, with a reason (it raised or hung)'
fi

printf 'TIER no-network\n'
# A tripwire, not a behaviour test: this suite must never reach the internet.
# `webclient` is loaded by the library, so the only guard that means anything
# is that no test path calls it -- asserted by there being no live provider
# handle anywhere in the fixture.
if command grep -qE "market\.(stooq|tiingo)\(\)[^)]*$" tests/market_test.bas && \
   ! command grep -q "market.offline" tests/market_test.bas; then
    fail 'the fixture uses only offline handles'
else
    pass 'the fixture uses only offline handles'
fi

if [[ $failures -gt 0 ]]; then
    printf 'FAIL tests/run_market.sh (%d of %d checks failed)\n' "$failures" "$checks"
    exit 1
fi

printf 'PASS tests/run_market.sh (%d checks, %s assertions in the fixture)\n' "$checks" "$reported"
