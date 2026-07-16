#!/usr/bin/env bash
# --json-diagnostics golden test (PLAN.md Phase L, Deliverable 2).
#
# `gbasic --json-diagnostics FILE` emits collected diagnostics as JSON lines on
# stderr; the default (no-flag) path must stay byte-exact legacy format (that is
# guarded by the negative suite — spot-checked here too).
set -u

cd "$(dirname "$0")/.."

if [ ! -x ./gbasic ]; then
    if ! make gbasic >/dev/null 2>&1; then
        echo "FAIL run_json_diagnostics: could not build gbasic"
        exit 1
    fi
fi

status=0
BAS=tests/json_diagnostics_test.bas
GOLD=tests/json_diagnostics_test.jsongolden

# JSON-lines output on stderr matches the golden.
got="$(./gbasic --json-diagnostics "$BAS" 2>&1 >/dev/null)"
exp="$(cat "$GOLD")"
if [ "$got" = "$exp" ]; then
    echo "PASS json-diagnostics output"
else
    echo "FAIL json-diagnostics output"
    echo "  expected: $exp"
    echo "  actual:   $got"
    status=1
fi

# Default path stays legacy (byte-exact), NOT JSON.
default="$(./gbasic "$BAS" 2>&1 >/dev/null)"
legacy="parse error at $BAS:1:5: syntax error, unexpected RPAREN"
if [ "$default" = "$legacy" ]; then
    echo "PASS json-diagnostics default-unchanged"
else
    echo "FAIL json-diagnostics default-unchanged"
    echo "  expected: $legacy"
    echo "  actual:   $default"
    status=1
fi

echo "=== run_json_diagnostics status=$status ==="
exit $status
