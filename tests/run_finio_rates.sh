#!/usr/bin/env bash
# `finio_rates` -- reference and benchmark rates from the institutions that
# publish them (SOFR, EFFR, OBFR, US Treasury average interest rates).
#
# WHY IT IS IN THE finio FAMILY: finio's subject is that a format is one thing
# and its INTERPRETATION is another, and that provenance is an axiom because
# "where did this number come from" is a REGULATORY question here. A benchmark
# rate is that problem arriving over HTTP rather than in a file -- a bank that
# priced a loan off SOFR on a Tuesday must be able to say, years later, which
# published value it used.
#
# SELF-CHECKING NOT GOLDEN AND FORCED: every defect is a PLAUSIBLE RATE. A value
# from the wrong row, a rate rounded through a double, a date off by one -- each
# is an ordinary-looking percentage that a golden would record and defend.
#
# NO NETWORK. Responses are replayed from committed recordings. A gate that
# reached a central bank would go red on a publisher's outage for a reason that
# is not about gBASIC, and could not assert a VALUE at all, since tomorrow's
# SOFR is not today's. tests/finio_rates/record.sh re-records deliberately.
set -u
cd "$(dirname "$0")/.."

make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }
export GBASIC_PATH="$PWD/stdlib"
status=0

printf 'TIER semantics\n'
out="$(timeout 300 ./gbasic tests/finio_rates/rates_test.bas 2>&1)"
if printf '%s' "$out" | grep -q '^mismatches: 0$'; then
    n="$(printf '%s' "$out" | sed -n 's/^checks: //p')"
    printf '  ok   %s checks, 0 mismatches\n' "$n"
    [ "${n:-0}" -ge 20 ] || { printf '  FAIL too few checks ran (%s)\n' "$n"; status=1; }
else
    printf '  FAIL rates_test\n'
    printf '%s\n' "$out" | grep -E '^MISMATCH|^runtime error|^parse error' | head -6
    status=1
fi

# THE FIXTURES ARE THE ORACLE, so they must be the ones the library asks for.
printf 'TIER replay\n'
n_fix=$(ls tests/finio_rates/replay/*.json 2>/dev/null | wc -l)
if [ "$n_fix" -ge 2 ]; then
    printf '  ok   %s recorded response(s) present\n' "$n_fix"
else
    printf '  FAIL no replay fixtures; run tests/finio_rates/record.sh\n'
    status=1
fi

# KEYLESS IS A PROMISE, AND A TRIPWIRE HOLDS IT. The library's argument for
# excluding FRED -- 800,000 series, excellent, needs a key -- is that a source
# needing an account stops working when somebody's key expires, in a library
# whose whole promise is that a number can be reproduced years later. A keyed
# source added later without that being a deliberate decision would break the
# promise silently.
printf 'TIER keyless\n'
if grep -qE 'api_key|apikey|API_KEY|token:' stdlib/finio_rates.bas; then
    printf '  FAIL a source appears to need a key; that is a deliberate decision, not a default\n'
    grep -nE 'api_key|apikey|API_KEY|token:' stdlib/finio_rates.bas | head -3
    status=1
else
    printf '  ok   every source is keyless\n'
fi

# THE PUBLISHERS ARE REACHABLE, run only on request. Not part of the gate: this
# asserts a fact about the internet, and a green gate must not depend on one.
if [ "${FINIO_RATES_LIVE:-0}" = "1" ]; then
    printf 'TIER live\n'
    if timeout 60 ./gbasic tests/finio_rates/live_check.bas >/dev/null 2>&1; then
        printf '  ok   the publishers answered\n'
    else
        printf '  FAIL a publisher did not answer (this is about the network, not gBASIC)\n'
        status=1
    fi
else
    printf 'TIER live      SKIP (FINIO_RATES_LIVE=1 to reach the publishers)\n'
fi

echo
if [ "$status" = 0 ]; then echo "run_finio_rates: all cases passed"; fi
exit "$status"
