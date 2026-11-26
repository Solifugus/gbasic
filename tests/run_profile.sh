#!/usr/bin/env bash
# GATE-LANE: serial -- its sampler thread being descheduled reads as the program misbehaving: the sampled hot/cold self-time ratio (2.5-3.5) was 3.55 in a parallel gate on 2026-11-26 and 2.89-3.03 in six runs alone, since load does not slow the sampler and the program alike
# `gbasic --profile` (docs/profiler_design.md): exact counts (increment 1),
# sampled time (increment 2), builtins as rows and a JSON report (increment 3).
#
# TIERS
#   counts     tests/profile/counts.bas keeps its OWN counter beside every place
#              the profiler counts and prints them (`oracle NAME N`): calls of a
#              function, a recursive one, a library's (under the name a caller
#              writes, its line under its own file), a loop body, a watch body,
#              and a one-line `if` counted as the times the line was reached.
#   shape      the whole report for tests/profile/shape.bas, byte for byte,
#              counts only (GBASIC_PROFILE_HZ=0: sampled shares would move the
#              rows); and a sampled report's headings.
#   shares     tests/profile/shares.bas: two functions with one body, called
#              30 and 10 times, take self time in a ratio of 2.5 to 3.5; every
#              function's total is at least its self; main's total is at least
#              97%; and the lines, waiting and outside rows sum to 100%. Ratios
#              of one run, so the machine's speed cancels. A sample is the
#              innermost line's (hot's body, not main's line calling it), and
#              tests/profile/recurse.bas's fib, on the stack many times at
#              once, totals 90-100%: once a sample, not once a frame.
#   waiting    tests/profile/waits.bas sleeps a second after a moment's work:
#              the waiting row holds at least 85% and the `sleep` line at most
#              2%.
#   builtins   tests/profile/builtins.bas: a builtin (sort, mod), a native
#              module's verb (process.alive) and a handle's method (h.wait(),
#              process.wait) each a row marked [builtin], its calls equal to the
#              program's own counter; sort, doing the most work, has at least
#              30% and more self than build, and the line calling it at least
#              as much; a user
#              library's function (countlib.triple) is not a builtin.
#   json       --profile=FILE.json, read by Python's json module
#              (tests/profile/check_json.py): sampled, the integers add up
#              exactly (lines + waiting + outside = samples), totals at least
#              selfs, every path absolute and existing, a builtin with no file;
#              counts only, every call and run equal to the text report's; a
#              program named with a quote, a control character and non-ASCII
#              named exactly, and one whose name is not UTF-8 still parsing.
#   signal     tests/profile/signals.py (python3 reads the wait status): SIGTERM
#              asleep and SIGINT computing each write the report and die by
#              their signal; a read blocked in C is reported after the 200 ms
#              grace; a second signal kills at once with no report; an ignored
#              SIGTERM stays ignored; counts only leaves signals alone; a
#              server's SIGTERM still drains, and the report follows.
#   unchanged  standard output and the exit status are the same with and
#              without --profile; standard error is the same, then the report.
#              Includes an uncaught raise (report after the error line) and
#              exit(3).
#   file       --profile=FILE writes every row and standard error gets none;
#              standard error alone shows 20 rows and says how many more.
#   refusals   --profile= with no file, the prompt, a GBASIC_PROFILE_HZ that is not a rate, and a --profile
#              after the file being the program's.
#   off        the same binary with every hook compiled out (make
#              gbasic-noprof) against this one with profiling off, counted in
#              INSTRUCTIONS by callgrind: the difference at most 6 a statement
#              run and 25 a call (both counted by the profiler on the same
#              program), and profiling on at most 1.10. Not wall time: this
#              binary timed against its own copy ranged 0.81 to 1.01 on a
#              loaded machine (2026-11-21), while an instruction count repeats
#              exactly. The hooks' cost IS instructions, a load and a branch
#              each. The bound was 1.01 of the whole run until 2026-11-22; the
#              hooks cost the same 7.4M instructions after names were resolved
#              once (docs/llvm_backend_design.md, increment 1), but the run
#              around them fell from 1,041M to 788M, so the ratio rose to
#              1.0093 with no change to the hooks. Measured then: 4.5 a
#              statement, about 21 a call.
#              Profiling on varies with the program's PATH (malloc's state
#              after copying it: 1.003-1.047 measured), hence the looser bound.
#   valgrind   the counts fixture, profiled.
#   helgrind   the shares fixture, profiled: no race between the sampler and
#              the interpreter, over at least 1000 samples (fair scheduling:
#              without it the sampler starved, 3 samples, and a planted race
#              went unseen). tests/profile_helgrind.supp names the functions
#              whose __atomic accesses helgrind cannot model, by first frame
#              only; an unprofiled run's own errors (libp11-kit at exit) are
#              the allowance.
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
# A line's share column is there when the run was sampled.
runs_of()  { awk -v t="$2" '/^lines/ { f = 1; next } f { line = $0; sub(/^ +[^ ]+ +[0-9]+( +[0-9.]+%)?   /, "", line); if (line == t) print $2 }' "$1"; }
place_of() { awk -v t="$2" '/^lines/ { f = 1; next } f { line = $0; sub(/^ +[^ ]+ +[0-9]+( +[0-9.]+%)?   /, "", line); if (line == t) print $1 }' "$1"; }
self_of()  { awk -v n="$2" '/^functions/ { f = 1; next } /^lines/ { f = 0 } f && $1 == n { sub(/%/, "", $4); print $4 }' "$1"; }
total_of() { awk -v n="$2" '/^functions/ { f = 1; next } /^lines/ { f = 0 } f && $1 == n { sub(/%/, "", $3); print $3 }' "$1"; }
header_re='^profile: [0-9]+\.[0-9]{2} s, [0-9]+ samples, [0-9]+\.[0-9]% of them waiting'
in_range() { awk -v x="$1" -v lo="$2" -v hi="$3" 'BEGIN { exit !(x != "" && x + 0 >= lo && x + 0 <= hi) }'; }
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
same "a function called back from a library" "$(calls_of "$tmp/counts.txt" called_back)" "$(oracle "$tmp/counts.out" called_back)"
same "the program block, once"          "$(calls_of "$tmp/counts.txt" main)"            1
same "the library's line, in its file"  "$(place_of "$tmp/counts.txt" 'return x * 3')"  tests/profile/countlib.bas:5

echo "--- TIER shape ---"
GBASIC_PROFILE_HZ=0 ./gbasic --profile tests/profile/shape.bas 2>"$tmp/shape.err" >/dev/null </dev/null || true
if cmp -s "$tmp/shape.err" tests/profile/shape.report; then
    pass "the report, byte for byte (counts only)"
else
    diff tests/profile/shape.report "$tmp/shape.err" | sed 's/^/  /'; fail "shape"
fi
./gbasic --profile tests/profile/shape.bas 2>"$tmp/sampled.err" >/dev/null </dev/null || true
if head -1 "$tmp/sampled.err" | grep -Eq "$header_re" &&
   [ "$(sed -n 2p "$tmp/sampled.err")" = "functions                          calls     total      self" ] &&
   [ "$(sed -n 5p "$tmp/sampled.err")" = "lines                               runs      self" ]; then
    pass "sampled: the header and the headings"
else
    sed 's/^/  /' "$tmp/sampled.err"; fail "sampled: the header or the headings"
fi

# shares.bas calls each function with 150000, not the 40000 it had: once
# increment 1 of docs/llvm_backend_design.md made its loop cheaper the run took
# 0.14 s, and a loaded gate's scheduling moved the ratio (1.72 in gate 25; with
# three-quarters of the cores busy, one run in 20 outside 2.5-3.5 at 40000 and
# none at 150000). The sampler counts wall time, so a short run is exposed.
# And so is one whose halves see different loads: with every function run
# back to back (hot's 30 calls, then cold's 10) gate 30 read 4.29, and with
# every core busy 4 runs in 60 were outside 2.5-3.5 for the binary before
# increment 2 of the LLVM work as for the one after. Interleaved (three hot
# to each cold, 2026-11-22): 0 in 40, 2.86-3.33.
echo "--- TIER shares ---"
./gbasic --profile="$tmp/shares.txt" tests/profile/shares.bas >/dev/null 2>"$tmp/shares.err" </dev/null || true
hot="$(self_of "$tmp/shares.txt" hot)"; cold="$(self_of "$tmp/shares.txt" cold)"
same "hot's calls" "$(calls_of "$tmp/shares.txt" hot)" 30
same "cold's calls" "$(calls_of "$tmp/shares.txt" cold)" 10
ratio="$(awk -v a="$hot" -v b="$cold" 'BEGIN { if (b > 0) printf "%.2f", a / b }')"
if in_range "$ratio" 2.5 3.5; then
    pass "self time follows the work: hot/cold = $ratio (2.5-3.5; $hot% and $cold%)"
else
    sed 's/^/  /' "$tmp/shares.txt"; fail "self time: hot/cold = '$ratio' ($hot% and $cold%), not 2.5-3.5"
fi
bad="$(awk '/^functions/ { f = 1; next } /^lines/ { f = 0 } f { t = $3; s = $4; sub(/%/, "", t); sub(/%/, "", s); if (t + 0 < s + 0) print $1 }' "$tmp/shares.txt")"
if [ -z "$bad" ]; then pass "every function's total is at least its self"; else fail "total under self: $bad"; fi
# A sample is the innermost statement's: the loop body inside hot, not the
# line in main that called hot.
body="$(awk '/^lines/ { f = 1; next } f && $1 == "tests/profile/shares.bas:6" { sub(/%/, "", $3); print $3 }' "$tmp/shares.txt")"
caller="$(awk '/^lines/ { f = 1; next } f && $1 == "tests/profile/shares.bas:22" { sub(/%/, "", $3); print $3 }' "$tmp/shares.txt")"
if in_range "$body" 30 100 && in_range "$caller" 0 2; then
    pass "a sample is the innermost line's: hot's body $body%, its caller's line $caller%"
else
    fail "innermost line: hot's body '$body'%, its caller's line '$caller'%"
fi
./gbasic --profile="$tmp/recurse.txt" tests/profile/recurse.bas >/dev/null 2>&1 </dev/null || true
fib_total="$(total_of "$tmp/recurse.txt" fib)"
if in_range "$fib_total" 90 100; then
    pass "recursion counts once a sample: fib's total $fib_total%"
else
    sed 's/^/  /' "$tmp/recurse.txt"; fail "recursion: fib's total '$fib_total'%"
fi
main_total="$(total_of "$tmp/shares.txt" main)"
if in_range "$main_total" 97 100; then pass "main's total: $main_total%"; else fail "main's total: '$main_total'%"; fi
# Every sample is one line's, the waiting row's or outside any line's.
sum="$(awk '/^lines/ { f = 1; next } f { for (i = 2; i <= NF; i++) if ($i ~ /%$/) { v = $i; sub(/%/, "", v); s += v; break } }
            /^profile:/ { if (match($0, /, [0-9.]+% outside/)) { v = substr($0, RSTART + 2, RLENGTH - 10); s += v } }
            END { printf "%.1f", s }' "$tmp/shares.txt")"
if in_range "$sum" 98.5 101.5; then pass "the rows account for every sample ($sum%)"; else sed 's/^/  /' "$tmp/shares.txt"; fail "the rows sum to $sum%"; fi

echo "--- TIER waiting ---"
./gbasic --profile="$tmp/waits.txt" tests/profile/waits.bas >/dev/null 2>&1 </dev/null || true
waiting="$(awk '$1 == "(waiting)" { sub(/%/, "", $2); print $2 }' "$tmp/waits.txt")"
sleeping="$(awk '/^lines/ { f = 1; next } f && /sleep\(1\)$/ { sub(/%/, "", $3); print $3 }' "$tmp/waits.txt")"
if in_range "$waiting" 85 100; then pass "a second asleep is the waiting row's: $waiting%"; else sed 's/^/  /' "$tmp/waits.txt"; fail "waiting: '$waiting'%"; fi
if in_range "$sleeping" 0 2; then pass "and not the sleep line's: $sleeping%"; else fail "the sleep line: '$sleeping'%"; fi

echo "--- TIER builtins ---"
./gbasic --profile="$tmp/builtins.txt" tests/profile/builtins.bas >"$tmp/builtins.out" 2>"$tmp/builtins.err" </dev/null || true
if [ -s "$tmp/builtins.err" ]; then sed 's/^/  /' "$tmp/builtins.err"; fail "builtins: the fixture wrote to standard error"; fi
builtin_calls() { awk -v n="$2" '/^functions/ { f = 1; next } /^lines/ { f = 0 } f && $1 == n && $2 == "[builtin]" { print $3 }' "$1"; }
builtin_self()  { awk -v n="$2" '/^functions/ { f = 1; next } /^lines/ { f = 0 } f && $1 == n && $2 == "[builtin]" { sub(/%/, "", $5); print $5 }' "$1"; }
for b in mod sort process.alive process.wait; do
    same "$b, a builtin row" "$(builtin_calls "$tmp/builtins.txt" "$b")" "$(oracle "$tmp/builtins.out" "$b")"
done
sort_self="$(builtin_self "$tmp/builtins.txt" sort)"
build_self="$(self_of "$tmp/builtins.txt" build)"
sort_line="$(awk '/^lines/ { f = 1; next } f && / ys = sort\(xs\)$/ { sub(/%/, "", $3); print $3 }' "$tmp/builtins.txt")"
if in_range "$sort_self" 30 100 && awk -v a="$sort_self" -v b="$build_self" 'BEGIN { exit !(a > b) }'; then
    pass "sort's own row holds its time: $sort_self% (build's self $build_self%)"
else
    sed 's/^/  /' "$tmp/builtins.txt"; fail "sort's self '$sort_self'%, build's '$build_self'%"
fi
if awk -v l="$sort_line" -v b="$sort_self" 'BEGIN { exit !(l != "" && l + 0 >= b - 1) }'; then
    pass "and the line calling it holds it too: $sort_line%"
else
    fail "the sort line '$sort_line'% under sort's '$sort_self'%"
fi
if grep -q 'countlib\.triple \[builtin\]' "$tmp/counts.txt"; then
    fail "a user library's function was shown as a builtin"
else
    pass "a user library's function is not a builtin"
fi

echo "--- TIER json ---"
if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP json (needs python3 to read it; NOT measured)\n'
else
    ./gbasic --profile="$tmp/b.json" tests/profile/builtins.bas >/dev/null 2>"$tmp/b.json.err" </dev/null || true
    if [ -s "$tmp/b.json.err" ]; then sed 's/^/  /' "$tmp/b.json.err"; fail "json: something on standard error"; fi
    python3 tests/profile/check_json.py sampled "$tmp/b.json" >"$tmp/j1.txt" 2>&1 && pass "sampled ($(grep -c '^ok' "$tmp/j1.txt") checks)" || { sed 's/^/  /' "$tmp/j1.txt"; fail "json sampled"; }
    GBASIC_PROFILE_HZ=0 ./gbasic --profile="$tmp/c.json" tests/profile/counts.bas >/dev/null 2>&1 </dev/null || true
    GBASIC_PROFILE_HZ=0 ./gbasic --profile="$tmp/c.txt" tests/profile/counts.bas >/dev/null 2>&1 </dev/null || true
    python3 tests/profile/check_json.py counts "$tmp/c.json" "$tmp/c.txt" >"$tmp/j2.txt" 2>&1 && pass "counts only, as the text report ($(grep -c '^ok' "$tmp/j2.txt") checks)" || { sed 's/^/  /' "$tmp/j2.txt"; fail "json counts"; }
    odd="$tmp/a \"q\" $(printf '\001')ü.bas"
    cp tests/profile/shape.bas "$odd"
    ./gbasic --profile="$tmp/n.json" "$odd" >/dev/null 2>&1 </dev/null || true
    python3 tests/profile/check_json.py names "$tmp/n.json" "$odd" >"$tmp/j3.txt" 2>&1 && pass "a quote, a control character and ü in the name" || { sed 's/^/  /' "$tmp/j3.txt"; fail "json names"; }
    bad="$tmp/$(printf 'b\377')ü.bas"
    cp tests/profile/shape.bas "$bad"
    ./gbasic --profile="$tmp/x.json" "$bad" >/dev/null 2>&1 </dev/null || true
    python3 tests/profile/check_json.py names "$tmp/x.json" "$bad" >"$tmp/j4.txt" 2>&1 && pass "a name that is not UTF-8 still parses" || { sed 's/^/  /' "$tmp/j4.txt"; fail "json, a name that is not UTF-8"; }
fi

echo "--- TIER signal ---"
if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP signal (needs python3 to read the wait status; NOT measured)\n'
elif python3 tests/profile/signals.py ./gbasic >"$tmp/signals.txt" 2>&1; then
    sed 's/^/  /' "$tmp/signals.txt"; pass "signal ($(grep -c '^ok' "$tmp/signals.txt") checks)"
else
    sed 's/^/  /' "$tmp/signals.txt"; fail "signal"
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
    elif ! tail -c +"$((n + 1))" "$tmp/u1.err" | head -1 | grep -Eq "$header_re"; then
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
if [ "$(printf '%s\n' "$both" | head -1)" = "12" ] && printf '%s\n' "$both" | sed -n 2p | grep -Eq "$header_re"; then
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
refused "--profile= names no file" '--profile= needs a file to write, as --profile=run.txt' --profile= tests/profile/shape.bas
refused "the prompt" '--profile runs a program file, not the prompt: gbasic --profile FILE' --profile --repl
for hz in fast -1 100001 12x; do
    got="$(GBASIC_PROFILE_HZ="$hz" ./gbasic --profile tests/profile/shape.bas 2>&1 </dev/null)" && rc=0 || rc=$?
    if [ "$rc" = 2 ] && [ "$got" = "--profile: GBASIC_PROFILE_HZ must be a whole number of samples a second, 0 (counts only) to 100000; the default is 10000" ]; then
        pass "GBASIC_PROFILE_HZ=$hz refused, nothing run"
    else
        fail "GBASIC_PROFILE_HZ=$hz: exit $rc, '$got'"
    fi
done
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
    . "$(dirname "$0")/valgrind_tier.sh"
    # interpreted even in the compiled lane: there LLVM's heap moves
    # _int_malloc's path by ~10 instructions a call with the program's PATH
    # (noprof 1016.66M from /tmp, 1018.68M from the scratchpad, the hooks
    # ~7.0M both times), which is layout, not what this tier prices
    ir() { GBASIC_COMPILE=0 vg_count_instructions "$@"; }
    if ! vg_available; then
        printf 'SKIP off (needs valgrind'"'"'s callgrind to count instructions; NOT measured)\n'
    elif [ -x "$tmp/gbasic-noprof" ]; then
        with="$(ir ./gbasic "$tmp/bench.bas")"
        without="$(ir "$tmp/gbasic-noprof" "$tmp/bench.bas")"
        profiled="$(ir ./gbasic --profile="$tmp/bench.txt" "$tmp/bench.bas")"
        GBASIC_PROFILE_HZ=0 ./gbasic --profile="$tmp/bench.json" "$tmp/bench.bas" >/dev/null 2>&1 </dev/null
        runs="$(grep -o '"runs": *[0-9]*' "$tmp/bench.json" | awk -F: '{ s += $2 } END { print s + 0 }')"
        calls="$(grep -o '"calls": *[0-9]*' "$tmp/bench.json" | awk -F: '{ s += $2 } END { print s + 0 }')"
        if [ -z "$with" ] || [ -z "$without" ] || [ -z "$profiled" ]; then
            fail "off: callgrind gave no count ('$with' '$without' '$profiled')"
        elif [ "$runs" -lt 600000 ] || [ "$calls" -lt 200000 ]; then
            fail "off: the profiler counted $runs statement runs and $calls calls, fewer than the program makes"
        else
            ratio="$(awk -v a="$with" -v b="$without" 'BEGIN { printf "%.4f", a / b }')"
            on="$(awk -v a="$profiled" -v b="$without" 'BEGIN { printf "%.4f", a / b }')"
            bound=$((6 * runs + 25 * calls))
            extra=$((with - without))
            if [ "$extra" -le "$bound" ]; then
                pass "off: the hooks cost $extra instructions, at most $bound (6 a statement run x $runs, 25 a call x $calls; ${ratio}x the whole run)"
            else
                fail "off: the hooks cost $extra instructions, over $bound (6 a statement run x $runs, 25 a call x $calls; $with against $without)"
            fi
            if awk -v r="$on" 'BEGIN { exit !(r <= 1.10) }'; then
                pass "profiling on: ${on}x (at most 1.10; $profiled against $without instructions)"
            else
                fail "profiling on: ${on}x the instructions, over 1.10"
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

echo "--- TIER helgrind ---"
if vg_available; then
    races() { grep -cE 'Possible data race|lock order' "$1" || true; }
    errors() { awk '/ERROR SUMMARY:/ { print $4 }' "$1"; }
    timeout -k 5 600 bash -c '. tests/valgrind_tier.sh; vg_run_helgrind ./gbasic tests/profile/shares.bas' \
        >/dev/null 2>"$tmp/hg0.err" </dev/null || true
    timeout -k 5 600 bash -c '. tests/valgrind_tier.sh; vg_run_helgrind --suppressions=tests/profile_helgrind.supp ./gbasic --profile="$1" tests/profile/shares.bas' _ "$tmp/hg.txt" \
        >/dev/null 2>"$tmp/hg1.err" </dev/null || true
    # The sampler must actually have run beside the interpreter, or there was
    # nothing for helgrind to see (it took 3 samples before --fair-sched).
    hg_samples="$(awk 'NR == 1 { for (i = 1; i < NF; i++) if ($(i + 1) ~ /^samples,?$/) print $i }' "$tmp/hg.txt" 2>/dev/null)"
    if [ -z "$(errors "$tmp/hg1.err")" ]; then
        tail -20 "$tmp/hg1.err"; fail "helgrind gave no summary"
    elif ! in_range "$hg_samples" 1000 1000000000; then
        fail "helgrind: the sampler took '$hg_samples' samples under it, so no race could be seen"
    elif [ "$(races "$tmp/hg1.err")" -eq 0 ] && [ "$(errors "$tmp/hg1.err")" -le "$(errors "$tmp/hg0.err")" ]; then
        pass "helgrind: no race over $hg_samples samples ($(errors "$tmp/hg1.err") errors, as unprofiled: $(errors "$tmp/hg0.err"))"
    else
        grep -A12 -E 'Possible data race|lock order' "$tmp/hg1.err" | head -60; fail "helgrind"
    fi
else
    printf 'SKIP helgrind (unavailable)\n'
fi

exit $status
