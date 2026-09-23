#!/usr/bin/env bash
# Scientific notation as a LITERAL -- and the silent trap that made its absence
# expensive, which is the half the ledger bullet understated.
#
# `1e20` lexed as the number 1 beside the identifier `e20`, which the grammar
# reads as a DURATION. The ledger recorded that as "a misleading message". It
# was measured on 2026-09-23 and it was worse in three separate ways at once:
# the message was UNLOCATED and went straight to stderr, so it BYPASSED THE
# DIAGNOSTICS SINK and `--json-diagnostics` emitted a non-JSON line into a JSON
# stream (the defect run_parse_exit.sh exists for); the program then ANSWERED
# `0 seconds`; and it EXITED 0 and carried on. That is the bare-line /
# plausible-value / successful-exit signature run_silent_traps.sh was built
# for, in a third place it had never reached -- and it was not reachable only
# through `1e20`: `1 fortnight` was a duration of zero that nothing downstream
# could detect.
#
# So there are two changes here and they are one story. The literal removes the
# commonest way into that path; the refusal removes the path.
#
# Tiers:
#   SEMANTICS   the self-checking fixture, 17 checks. THE LOAD-BEARING TIER IS
#               THE ORACLE: each literal must equal `number("...")` on the same
#               text, which is the route the whole tree has been using instead.
#               A value comparison alone passes on a lexer consistently off by
#               a factor, because the expected value would be written as a
#               literal too. Plus the CONSERVATIVE block -- hex, durations, a
#               variable named `e2` -- without which "it is a literal" is
#               satisfied by a lexer that swallowed the identifier after every
#               number.
#   REFUSAL     an unknown duration unit, asserted the four ways
#               run_parse_exit.sh established: LOCATED (file:line:col),
#               NONZERO, NOTHING RAN, and it reaches the DIAGNOSTICS SINK as
#               JSON rather than as a bare line beside it. Beside its nearest
#               legal neighbour -- a real duration -- or "it refuses" would be
#               satisfied by refusing every duration in the language.
#   SINK        the tripwire on the class: no bare `fprintf(stderr` may return
#               to the duration path. A behavioural check cannot see a second
#               one added somewhere else in that function.
#   VALGRIND    the refusal path now frees a unit string it used to print.
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

printf 'TIER semantics: the literal, the oracle, and what did not change\n'
if ! timeout -k 5 60 ./gbasic tests/exponent_literal_test.bas >"$work/out" 2>"$work/err"; then
    cat "$work/err"; fail "the fixture did not run"
elif grep -q MISMATCH "$work/out"; then
    grep MISMATCH "$work/out"; fail "the fixture disagreed with itself"
elif ! grep -qx 'mismatches: 0' "$work/out"; then
    fail "the fixture did not finish"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    if [ -z "$n" ] || [ "$n" -lt 15 ]; then
        fail "only ${n:-0} checks ran, wanted at least 15"
    else
        pass "$n checks"
    fi
fi

printf 'TIER refusal: an unknown duration unit stops the program\n'
printf 'print 1 fortnight\nprint "ran anyway"\n' > "$work/bad.bas"
if ./gbasic "$work/bad.bas" >"$work/bad.out" 2>"$work/bad.err"; then
    fail "an unknown duration unit exited 0 -- the silent trap is back"
else
    if grep -qE "^parse error at .*:1:9: " "$work/bad.err"; then
        pass "LOCATED: reported at the unit, as a parse error"
    else
        cat "$work/bad.err"; fail "the report is not located at the unit"
    fi
    if grep -q "unknown duration unit 'fortnight'" "$work/bad.err"; then
        pass "and it names the unit"
    else
        cat "$work/bad.err"; fail "the message does not name the unit"
    fi
    if grep -q "year, month, week, day, hour, minute and second" "$work/bad.err"; then
        pass "and the units that do exist"
    else
        fail "the message does not say what the units are"
    fi
    # NOTHING RAN. This is the half that made it dangerous: the old behaviour
    # printed, produced `0 seconds`, and went on to the next statement.
    if [ ! -s "$work/bad.out" ]; then
        pass "NOTHING RAN: no output at all"
    else
        cat "$work/bad.out"; fail "the program ran past a refused duration"
    fi
    if ! grep -q "0 seconds" "$work/bad.out" "$work/bad.err"; then
        pass "and no plausible zero duration was produced"
    else
        fail "it still answers 0 seconds"
    fi
fi
# THE SINK. A bare line beside the JSON is exactly what the old code emitted,
# and only --json-diagnostics can see it.
./gbasic --json-diagnostics "$work/bad.bas" >/dev/null 2>"$work/bad.json" || true
if [ "$(wc -l < "$work/bad.json")" = "1" ] && head -1 "$work/bad.json" | grep -q '^{"severity":"error"'; then
    pass "SINK: it arrives as JSON, and nothing else is on the stream"
else
    cat "$work/bad.json"; fail "the diagnostics stream carries something that is not JSON"
fi
# AT LEAST ONE DIGIT IS REQUIRED after the `e`, and that is what keeps the
# literal conservative -- `1e` must still be the number 1 beside an identifier
# `e`, so nothing that parsed before stops parsing. IT CANNOT BE ASSERTED IN
# THE FIXTURE, because a greedy lexer makes `1e` a perfectly good number 1 and
# a fixture that could ask the question would have to contain a line that does
# not parse. Found by perturbation: with the digit requirement removed, every
# other check in this file stayed green.
printf 'print 1e\n' > "$work/greedy.bas"
if ./gbasic "$work/greedy.bas" >"$work/greedy.out" 2>"$work/greedy.err"; then
    cat "$work/greedy.out"; fail "\`1e\` was accepted as a number -- the exponent is greedy"
elif grep -q "unknown duration unit 'e'" "$work/greedy.err"; then
    pass "\`1e\` is still a number beside an identifier, not an exponent"
else
    cat "$work/greedy.err"; fail "\`1e\` failed, but not as the number-then-identifier it is"
fi

# THE CONTROL. Without it, every check above is satisfied by refusing every
# duration there is.
printf 'print 2 hours 30 minutes\n' > "$work/ok.bas"
if [ "$(./gbasic "$work/ok.bas" 2>&1)" = "2 hours 30 minutes" ]; then
    pass "CONTROL: a real duration is untouched"
else
    ./gbasic "$work/ok.bas" 2>&1; fail "a legal duration stopped working"
fi

printf 'TIER sink tripwire: the duration path may not print for itself\n'
if grep -n 'fprintf(stderr' src/parser.y | grep -qi 'duration'; then
    grep -n 'fprintf(stderr' src/parser.y | grep -i 'duration'
    fail "a bare stderr write is back on the duration path"
else
    pass "no bare stderr write names a duration"
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/exponent_literal_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access (the literal path)"
    else
        tail -20 "$work/vg.err"; fail "valgrind objected to the literal path"
    fi
    # The REFUSAL path is the one that changed ownership of a string, and the
    # fixture above never reaches it, so it needs its own run. The program
    # exits 1 BY DESIGN, so the comparison is against VG_EXIT and not against
    # success -- the reports-the-wrong-cause lesson from run_repl's own tier.
    # `|| true` here would make `$?` ALWAYS 0 -- the tier would report ok
    # whatever valgrind said, which is the class of defect this file is about.
    set +e
    vg_run ./gbasic "$work/bad.bas" >/dev/null 2>"$work/vg2.err"
    vgst=$?
    set -e
    if [ "$vgst" = "$VG_EXIT" ]; then
        grep -E "definitely lost|Invalid" "$work/vg2.err" | head -3
        fail "valgrind objected to the refusal path"
    else
        pass "no definite leak or invalid access (the refusal path)"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

exit $status
