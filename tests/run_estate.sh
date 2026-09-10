#!/usr/bin/env bash
# `estate` -- a fabricated business estate whose TRUTH IS WRITTEN DOWN
# (stdlib/estate.bas, docs/estate_design.md), and `discovery` traced across it.
#
# WHY IT EXISTS. "Did the tool find the right relationship?" has no answer
# unless the right answer is known, and the sharper question -- "did it INVENT
# one where there is none?" -- has no answer at all without a region whose
# truth is nothing. A real customer database cannot supply either.
#
# R1 IS THE LOAD-BEARING PROPERTY: ONE DECLARATION PRODUCES BOTH THE DATABASE
# AND THE TRUTH. A relationship declared `enforced` becomes a constraint; one
# declared otherwise leaves no trace while still appearing in the truth. A
# hand-written answer key drifts the first time either side changes, and A
# FIXTURE WHOSE ANSWER KEY IS WRONG TEACHES THE TOOL TO BE WRONG AND THEN
# CERTIFIES IT.
#
# THE OFFLINE TIER NEEDS NO DATABASE, which is what keeps this gate from going
# quiet: `spec`, `ddl` and `truth` are pure, and `discovery.references` is a
# pure function of a string -- so the estate DECLARES what each module touches
# and the scanner DERIVES it from the SQL without seeing the declaration.
# Agreement between two independently written statements is evidence; a golden
# of either alone is a transcript.
#
# THE LIVE TIER materialises the whole estate and traces it. It found four
# defects in `discovery` that no hermetic test could:
#   - a view reading another VIEW could not be resolved at all, because only
#     base tables were kept as resolution targets (estate R11/R29)
#   - keeping views then swept in every sys.* and INFORMATION_SCHEMA.* object:
#     23 -> 656, of which 17 were the business estate
#   - FreeTDS refuses odbc.columns with no table pattern
#   - psqlODBC with no SCHEMA pattern SILENTLY returns only the search_path,
#     so an estate in `trading`/`finance`/`warehouse` came back as 17 `public`
#     tables WITH NO ERROR -- a complete-looking answer missing most of the
#     database, which is exactly the failure this library exists not to produce
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null
export GBASIC_PATH=stdlib

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER the declaration, and what it must and must not emit\n'
if ! timeout -k 5 60 ./gbasic tests/estate_test.bas >"$work/out" 2>"$work/err"; then
    cat "$work/err"; fail "the fixture did not run"
elif grep -q MISMATCH "$work/out"; then
    grep MISMATCH "$work/out"; fail "the estate disagreed with its own truth"
elif ! grep -qx 'mismatches: 0' "$work/out"; then
    fail "the fixture did not finish"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    if [ -z "$n" ] || [ "$n" -lt 35 ]; then
        fail "only ${n:-0} checks ran, wanted at least 35"
    else
        pass "$n checks (R1, the named decoys, the null region, the declared flows)"
    fi
fi

printf 'TIER live: materialise the estate and trace it\n'
if [ -z "${GBASIC_ESTATE_CONNECTION:-}" ]; then
    pass "SKIP (set GBASIC_ESTATE_CONNECTION and GBASIC_ESTATE_DIALECT to run it)"
else
    export GBASIC_ODBC_CONNECTION="$GBASIC_ESTATE_CONNECTION"
    export DIALECT="${GBASIC_ESTATE_DIALECT:-sqlserver}"
    if ! timeout -k 10 600 ./gbasic tests/estate_live_test.bas >"$work/live" 2>"$work/liveerr"; then
        cat "$work/liveerr"; fail "the live fixture did not run"
    elif grep -q MISMATCH "$work/live"; then
        grep MISMATCH "$work/live"; fail "discovery did not recover the declared estate"
    else
        n=$(sed -n 's/^checks: //p' "$work/live")
        pass "$n checks against a real $DIALECT"
    fi
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/estate_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_estate.sh\n'
