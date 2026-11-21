#!/usr/bin/env bash
# `gbasic --profile` (docs/profiler_design.md), increment 1: exact counts.
#
# TIERS
#   counts     tests/profile/counts.bas keeps its OWN counter beside every place
#              the profiler counts and prints them (`oracle NAME N`): calls of a
#              function, a recursive one, a library's (under the name a caller
#              writes, its line under its own file), a loop body, a watch body,
#              and a one-line `if` counted as the times the line was reached.
#   shape      the whole report for tests/profile/shape.bas, byte for byte.
#   unchanged  standard output and the exit status are the same with and
#              without --profile; standard error is the same, then the report.
#              Includes an uncaught raise (report after the error line) and
#              exit(3).
#   file       --profile=FILE writes every row and standard error gets none;
#              standard error alone shows 20 rows and says how many more.
#   refusals   --profile=x.json (increment 3), --profile= with no file, the
#              prompt, and a --profile after the file being the program's.
#   off        the same binary with every hook compiled out (make
#              gbasic-noprof) against this one with profiling off, counted in
#              INSTRUCTIONS by callgrind: at most 1.01, and counting on at most
#              1.10. Not wall time: this binary timed against its own copy
#              ranged 0.81 to 1.01 on a loaded machine (2026-11-21), while an
#              instruction count repeats exactly. The hooks' cost IS
#              instructions, a load and a branch each.
#              Counting on varies 1.003-1.047 with the program's PATH (malloc's
#              state after copying it), hence the looser bound there.
#   valgrind   the counts fixture, profiled.
. "$(dirname "$0")/portable.sh"
. "$(dirname "$0")/premise.sh"   # premise_time_claims
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null
export GBASIC_PATH=stdlib
status=0
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; status=1; }

# A row of the report: the count beside a function's name, or beside the line
# whose source text is given.
calls_of() { awk -v n="$2" '/^functions/ { f = 1; next } /^lines/ { f = 0 } f && $1 == n { print $2 }' "$1"; }
runs_of()  { awk -v t="$2" '/^lines/ { f = 1; next } f { line = $0; sub(/^ +[^ ]+ +[0-9]+   /, "", line); if (line == t) print $2 }' "$1"; }
place_of() { awk -v t="$2" '/^lines/ { f = 1; next } f { line = $0; sub(/^ +[^ ]+ +[0-9]+   /, "", line); if (line == t) print $1 }' "$1"; }
oracle()   { awk -v n="$2" '$1 == "oracle" && $2 == n { print $3 }' "$1"; }

echo "--- TIER counts ---"
./gbasic --profile="$tmp/counts.txt" tests/profile/counts.bas >"$tmp/counts.out" 2>"$tmp/counts.err" </dev/null || true
if [ -s "$tmp/counts.err" ]; then
    sed 's/^/  /' "$tmp/counts.err"; fail "counts: the fixture wrote to standard error"
fi
same() {   # label, profile's figure, the program's own
    if [ -n "$2" ] && [ "$2" = "$3" ]; then pass "$1: $2"; else fail "$1: profile says '${2}', the program counted '${3}'"; fi
}
same "a function's calls"               "$(calls_of "$tmp/counts.txt" seven)"           "$(oracle "$tmp/counts.out" seven)"
same "a recursive function's calls"     "$(calls_of "$tmp/counts.txt" fib)"             "$(oracle "$tmp/counts.out" fib)"
same "a library function, as written"   "$(calls_of "$tmp/counts.txt" countlib.triple)" "$(oracle "$tmp/counts.out" countlib.triple)"
same "the library's line"               "$(runs_of "$tmp/counts.txt" 'return x * 3')"   "$(oracle "$tmp/counts.out" countlib.triple)"
same "a loop body"                      "$(runs_of "$tmp/counts.txt" 'tally.body += 1')" "$(oracle "$tmp/counts.out" body)"
same "a watch body"                     "$(runs_of "$tmp/counts.txt" 'tally.watched += 1')" "$(oracle "$tmp/counts.out" watched)"
same "a one-line if, as reached"        "$(runs_of "$tmp/counts.txt" 'if i > 5 then tally.oneline += 1')" "$(oracle "$tmp/counts.out" oneline_reached)"
same "the program block, once"          "$(calls_of "$tmp/counts.txt" main)"            1
same "the library's line, in its file"  "$(place_of "$tmp/counts.txt" 'return x * 3')"  tests/profile/countlib.bas:5

echo "--- TIER shape ---"
./gbasic --profile tests/profile/shape.bas 2>"$tmp/shape.err" >/dev/null </dev/null || true
if cmp -s "$tmp/shape.err" tests/profile/shape.report; then
    pass "the report, byte for byte"
else
    diff tests/profile/shape.report "$tmp/shape.err" | sed 's/^/  /'; fail "shape"
fi

echo "--- TIER unchanged ---"
unchanged() {   # label, then the program and its arguments
    local label="$1"; shift
    local rc0=0 rc1=0
    ./gbasic "$@" >"$tmp/u0.out" 2>"$tmp/u0.err" </dev/null || rc0=$?
    ./gbasic --profile "$@" >"$tmp/u1.out" 2>"$tmp/u1.err" </dev/null || rc1=$?
    local n; n="$(wc -c <"$tmp/u0.err")"
    if ! cmp -s "$tmp/u0.out" "$tmp/u1.out"; then
        fail "$label: standard output differs under --profile"
    elif [ "$rc0" != "$rc1" ]; then
        fail "$label: exit status $rc0 became $rc1"
    elif ! cmp -s "$tmp/u0.err" <(head -c "$n" "$tmp/u1.err"); then
        fail "$label: standard error does not begin with the unprofiled run's"
    elif [ "$(tail -c +"$((n + 1))" "$tmp/u1.err" | head -1)" != "profile: exact counts (times are not measured yet)" ]; then
        fail "$label: the report does not follow it"
    else
        pass "$label (exit $rc0)"
    fi
}
unchanged "the counts fixture"             tests/profile/counts.bas
unchanged "an uncaught raise"              tests/profile/raises.bas
unchanged "exit(3)"                        tests/profile/raises.bas now
unchanged "the conditional's checks"       tests/conditional/conditional_test.bas
for ex in examples/money_cookbook/*.bas; do
    unchanged "$ex" "$ex"
done
# One stream, piped: the program's output is still in its buffer when the run
# ends, and the report must come after it, not above it.
both="$(./gbasic --profile tests/profile/shape.bas 2>&1 </dev/null | cat)"
if [ "$(printf '%s\n' "$both" | head -1)" = "6" ] && [ "$(printf '%s\n' "$both" | sed -n 2p)" = "profile: exact counts (times are not measured yet)" ]; then
    pass "one piped stream: the program's output, then the report"
else
    printf '%s\n' "$both" | head -3 | sed 's/^/  /'; fail "one piped stream: the report is not after the program's output"
fi

echo "--- TIER file ---"
./gbasic --profile tests/profile/counts.bas >/dev/null 2>"$tmp/stderr.txt" </dev/null || true
rows_file="$(awk '/^lines/ { f = 1; next } f' "$tmp/counts.txt" | wc -l)"
rows_err="$(awk '/^lines/ { f = 1; next } f && !/^  \.\.\./' "$tmp/stderr.txt" | wc -l)"
more="$(grep -c '^  \.\.\. and [0-9]* more (--profile=FILE lists every one)$' "$tmp/stderr.txt" || true)"
if ! grep -q '^profile:' "$tmp/counts.err"; then pass "--profile=FILE: nothing on standard error"; else fail "--profile=FILE also wrote to standard error"; fi
if [ "$rows_file" -gt 20 ]; then pass "the file has every line ($rows_file)"; else fail "the file has $rows_file lines, expected over 20"; fi
if [ "$rows_err" -eq 20 ] && [ "$more" -ge 1 ] && grep -q "and $((rows_file - 20)) more" "$tmp/stderr.txt"; then
    pass "standard error shows 20 and names the $((rows_file - 20)) more"
else
    fail "standard error showed $rows_err rows and $more 'more' lines"
fi

echo "--- TIER refusals ---"
refused() {   # label, expected first line, then gbasic's arguments
    local label="$1" want="$2"; shift 2
    local got rc=0
    got="$(./gbasic "$@" 2>&1 </dev/null)" || rc=$?
    if [ "$rc" = 2 ] && [ "$(printf '%s\n' "$got" | head -1)" = "$want" ]; then pass "$label"; else fail "$label: exit $rc, '$got'"; fi
}
refused "a JSON report is not built yet" \
    '--profile=x.json: a JSON report is not built yet (it is increment 3 of docs/profiler_design.md); name a text file' \
    --profile=x.json tests/profile/shape.bas
refused "--profile= names no file" '--profile= needs a file to write, as --profile=run.txt' --profile= tests/profile/shape.bas
refused "the prompt" '--profile runs a program file, not the prompt: gbasic --profile FILE' --profile --repl
got="$(./gbasic tests/profile/args.bas --profile 2>&1 </dev/null)"
if [ "$got" = '["--profile"]' ]; then pass "after the file, --profile is the program's"; else fail "after the file: '$got'"; fi

echo "--- TIER off ---"
if ! premise_time_claims; then
    printf 'SKIP off (GATE_TIME_SCALE=%s: the hooks'"'"' cost when off was NOT measured, and building a second copy of eval.c would take most of an hour)\n' "$GATE_TIME_SCALE"
else
    make gbasic-noprof NOPROF_DIR="$tmp" >"$tmp/noprof.log" 2>&1 || { tail -20 "$tmp/noprof.log"; fail "make gbasic-noprof"; }
    cat >"$tmp/bench.bas" <<'BAS'
function add(a, b)
    return a + b
end function
s = 0
for i = 1 to 200000
    s = add(s, i)
    if s < 0 then s = 0
next
print s
BAS
    ir() { valgrind --tool=callgrind --callgrind-out-file=/dev/null "$@" 2>&1 >/dev/null </dev/null | awk '/Collected :/ { print $4 }'; }
    if ! command -v valgrind >/dev/null 2>&1; then
        printf 'SKIP off (needs valgrind'"'"'s callgrind to count instructions; NOT measured)\n'
    elif [ -x "$tmp/gbasic-noprof" ]; then
        with="$(ir ./gbasic "$tmp/bench.bas")"
        without="$(ir "$tmp/gbasic-noprof" "$tmp/bench.bas")"
        profiled="$(ir ./gbasic --profile="$tmp/bench.txt" "$tmp/bench.bas")"
        if [ -z "$with" ] || [ -z "$without" ] || [ -z "$profiled" ]; then
            fail "off: callgrind gave no count ('$with' '$without' '$profiled')"
        else
            ratio="$(awk -v a="$with" -v b="$without" 'BEGIN { printf "%.4f", a / b }')"
            on="$(awk -v a="$profiled" -v b="$without" 'BEGIN { printf "%.4f", a / b }')"
            if awk -v r="$ratio" 'BEGIN { exit !(r <= 1.01) }'; then
                pass "off: ${ratio}x the instructions of the hooks compiled out (at most 1.01)"
            else
                fail "off: ${ratio}x the instructions of the hooks compiled out, over 1.01 ($with against $without)"
            fi
            if awk -v r="$on" 'BEGIN { exit !(r <= 1.10) }'; then
                pass "counting on: ${on}x (at most 1.10; $profiled against $without instructions)"
            else
                fail "counting on: ${on}x the instructions, over 1.10"
            fi
        fi
    fi
fi

echo "--- TIER valgrind ---"
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    rc=0
    timeout -k 5 600 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic --profile tests/profile/counts.bas' \
        >/dev/null 2>"$tmp/vg.err" </dev/null || rc=$?
    if [ "$rc" = "$VG_EXIT" ] || [ "$rc" = 124 ]; then
        tail -30 "$tmp/vg.err"; fail "valgrind (rc $rc)"
    else
        pass "valgrind (no definite leak or invalid access)"
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

exit $status
