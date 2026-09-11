#!/usr/bin/env bash
# `notation` -- a TEXTUAL form that keeps every gBASIC type
# (stdlib/notation.bas, docs/text_serialization_design.md).
#
# THE GAP IT FILLS. `encode` is readable and REFUSES a date, money, a duration
# or a file; `serialize` keeps every type and is OPAQUE BINARY. There was no
# form a person could open in an editor, read and hand-edit that survived a
# typed value -- which is most of what a business record is made of.
#
# SELF-CHECKING RATHER THAN GOLDEN, and forced: every defect a serializer can
# have is a PLAUSIBLE DOCUMENT. Money rounded to the minor unit still reads
# like money, a date decoded as a string still reads like a date, an array
# whose default was dropped still reads like an array. A golden would record
# any of them as expected and defend it.
#
# THE LOAD-BEARING PROPERTY IS THE ROUND TRIP AND IT NEEDS TWO CONTROLS, or it
# is satisfied by `serialize` -- which already keeps every type and is binary.
# So the fixture also asserts the output is LEGIBLE (newlines, indentation, the
# value's own text, the tag on the key side), and that a defect which
# ROUND-TRIPS is still caught: money's sub-cent case, where `string` gives 3.46
# for 3.459 and an encoder built on it would round-trip 3.46 to 3.46,
# self-consistently wrong. Only comparing against the ORIGINAL catches that.
#
# TWO DEFECTS THE ROUND TRIP FOUND WHILE IT WAS BEING WRITTEN, neither visible
# by reading. A nested array's default was COMPUTED AND THEN DROPPED, so a
# matrix of dates came back as plain strings. And the re-raise that attaches a
# line number to a modifier's message was issued while `on error goto next` was
# still armed -- frame-scoped, so the frame caught its own raise and every
# refusal VANISHED, turning five checks green-to-red in one edit.
#
# Headless, no network, never skips (bar valgrind).
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

printf 'TIER round trip, legibility, refusals\n'
if ! timeout -k 5 120 ./gbasic tests/notation_test.bas >"$work/out" 2>"$work/err"; then
    cat "$work/err"; fail "the fixture did not run"
elif grep -q MISMATCH "$work/out"; then
    grep MISMATCH "$work/out" | head -8; fail "a value did not survive the trip"
elif ! grep -qx 'mismatches: 0' "$work/out"; then
    fail "the fixture did not finish"
elif [ -s "$work/err" ]; then
    cat "$work/err"; fail "the fixture wrote to stderr"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    # A coverage floor: a fixture that stops running its checks otherwise
    # passes by asserting nothing.
    if [ -z "$n" ] || [ "$n" -lt 50 ]; then
        fail "only ${n:-0} checks ran, wanted at least 50"
    else
        pass "$n checks (every type, strings, NUL, defaults, hand-edits, refusals)"
    fi
fi

# THE OUTPUT IS READ BY A PERSON, so one tier looks at it rather than at a
# boolean. A shape regression -- everything on one line, or the tag drifting to
# the value side -- passes every equality check in the fixture.
printf 'TIER the document a reviewer actually sees\n'
cat >"$work/shape.bas" <<'BAS'
load notation
d {date}= "2026-03-15"
u {USD}= "19.99"
s {USD}= "3.459"
j {JPY}= "500"
print notation.to_text({ id: 4471, issued: d,
                         customer: { name: "Ada Lovelace & Co." },
                         prices: [ u, s, j ],
                         dates: [ d, d ],
                         total: u })
BAS
if ! ./gbasic "$work/shape.bas" >"$work/shape.out" 2>"$work/shape.err"; then
    cat "$work/shape.err"; fail "the shape fixture did not run"
else
    want_key='issued {date}: "2026-03-15"'
    want_arr='dates {date}: \[ "2026-03-15", "2026-03-15" \]'
    want_def='prices {USD}: \[ "19.99", "3.459", {JPY}: "500" \]'
    ok=1
    grep -qF "$want_key" "$work/shape.out" || { fail "the tag is not on the key side"; ok=0; }
    grep -qE "$want_arr"  "$work/shape.out" || { fail "an all-one-type array does not hoist its tag"; ok=0; }
    grep -qE "$want_def"  "$work/shape.out" || { fail "a same-but-one array does not use a default with an override"; ok=0; }
    grep -q '^  ' "$work/shape.out" || { fail "the document is not indented"; ok=0; }
    [ "$ok" -eq 1 ] && pass "key-side tags, a hoisted array tag, a default with an override, indented"
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/notation_test.bas >/dev/null 2>"$work/vg"; then
        pass "no definite leak or invalid access"
    else
        grep -E "definitely lost|Invalid" "$work/vg" | head -3; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_notation.sh\n'
