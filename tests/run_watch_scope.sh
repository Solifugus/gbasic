#!/usr/bin/env bash
# A WATCH BELONGS TO THE FRAME THAT DECLARES IT (docs/watch_scope_design.md §3,
# Matthew's ruling 2026-11-07). In a function it watches that function's own
# variables, its body runs in that frame and sees its locals, and it ends when
# the frame does, by every exit. Before, a watch in a function registered
# nothing and said so with one unlocated line on stderr.
#
# TIER scope: the fixture (tests/watch_scope/scope_test.bas), self-checking,
# every answer by hand: firing per assignment with the body writing a local,
# each call its own watch, recursion, a callee's same-named local not firing
# the caller's watch, a raise ending the watch it unwound, the named form,
# a handle that outlived its frame, each refusal beside an accepted neighbour,
# and the top level unchanged.
#
# TIER wait (increment 2, §4): `wait until <condition> [for <limit>]` runs the
# loop from inside a function: a timer counted into a local, an actor's reply,
# a limit that passes, nothing left to wait for (raised, not hung), limits a
# duration or seconds and nothing else, a wait inside a watch body inside a
# wait, waits nested past the cap (raised), a top-level wait, and `wait` still
# a plain name where `until` does not follow it (gdash has a variable `wait`).
#
# TIER serve: a server in a function's LOCAL, answered by that function's
# watch while it waits, driven by curl; the requests used to be found only in
# a global.
#
# TIER shape: an ended watch must LEAVE the registry, not stay as a tombstone,
# or a function called N times leaves N entries every later change scans. A
# ratio across a 4x step: linear is about 3x here, a registry that keeps them
# is about 14x.
#
# TIER valgrind: frames are on the C stack and their addresses are reused, so
# an entry that outlived its frame reads a dead stack slot.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."

make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }
status=0
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

echo "--- TIER scope ---"
out="$(GBASIC_PATH=stdlib timeout -k 5 60 ./gbasic tests/watch_scope/scope_test.bas 2>"$tmp/scope.err" </dev/null)"
last="$(printf '%s\n' "$out" | tail -1)"
printf '%s\n' "$out" | grep '^MISMATCH' | sed 's/^/  /'
n="$(printf '%s\n' "$last" | sed -n 's/^ok \([0-9]*\) checks, 0 mismatches$/\1/p')"
if [ -z "$n" ] || [ "$n" -lt 16 ]; then
    printf 'FAIL scope (%s)\n' "${last:-no verdict}"; status=1
elif [ -s "$tmp/scope.err" ]; then
    sed 's/^/  /' "$tmp/scope.err" | head -5
    printf 'FAIL scope (the fixture wrote to stderr)\n'; status=1
else
    printf 'PASS scope (%s checks)\n' "$n"
fi

echo "--- TIER wait ---"
out="$(GBASIC_PATH=stdlib timeout -k 5 90 ./gbasic tests/watch_scope/wait_test.bas 2>"$tmp/wait.err" </dev/null)"
last="$(printf '%s\n' "$out" | tail -1)"
printf '%s\n' "$out" | grep '^MISMATCH' | sed 's/^/  /'
n="$(printf '%s\n' "$last" | sed -n 's/^ok \([0-9]*\) checks, 0 mismatches$/\1/p')"
if [ -z "$n" ] || [ "$n" -lt 19 ]; then
    printf 'FAIL wait (%s)\n' "${last:-no verdict}"; status=1
elif [ -s "$tmp/wait.err" ]; then
    sed 's/^/  /' "$tmp/wait.err" | head -5
    printf 'FAIL wait (the fixture wrote to stderr)\n'; status=1
else
    printf 'PASS wait (%s checks)\n' "$n"
fi

echo "--- TIER serve ---"
if ! command -v curl >/dev/null 2>&1; then
    printf 'SKIP serve (no curl): a server in a function was NOT measured\n'
else
    GBASIC_PATH=stdlib timeout -k 5 60 ./gbasic --line-buffered tests/watch_scope/serve_in_function.bas 3 \
        >"$tmp/serve.log" 2>&1 </dev/null &
    srv=$!
    port=""
    for _ in $(seq 1 200); do
        port="$(sed -n 's/^PORT //p' "$tmp/serve.log" | head -1)"; [ -n "$port" ] && break; sleep 0.05
    done
    bodies=""
    for _ in 1 2 3; do
        bodies="$bodies$(curl -s -m 10 "http://127.0.0.1:${port:-0}/")|"
    done
    wait "$srv"; rc=$?
    want="request 1 of 3|request 2 of 3|request 3 of 3|"
    if [ -z "$port" ]; then
        printf 'FAIL serve (no port announced)\n'; status=1
    elif [ "$bodies" != "$want" ]; then
        printf 'FAIL serve (bodies %s, want %s)\n' "$bodies" "$want"; status=1
    elif [ "$rc" != 0 ] || ! grep -qx 'served 3' "$tmp/serve.log"; then
        sed 's/^/  /' "$tmp/serve.log" | tail -5
        printf 'FAIL serve (the function did not return having served 3, rc %s)\n' "$rc"; status=1
    else
        printf 'PASS serve (three requests answered by a function'"'"'s own server, then it returned)\n'
    fi
fi

echo "--- TIER qtrun ---"
# DOGFOOD's live row 1 until 2026-11-08: qt.run() from inside a FUNCTION hung,
# its timer ticking into timer.ticks and never delivered. Both forms must tick
# twice and exit 0 now; the top-level form is the control.
sed 's/^    HANDOVER$/    qt.run()/' tests/watch_scope/qtrun.bas.in > "$tmp/qtrun_top.bas"
sed 's/^    HANDOVER$/    handover()/' tests/watch_scope/qtrun.bas.in > "$tmp/qtrun_fn.bas"
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 15 ./gbasic --line-buffered "$tmp/qtrun_top.bas" >"$tmp/qt_top.out" 2>&1 </dev/null; top_rc=$?
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 15 ./gbasic --line-buffered "$tmp/qtrun_fn.bas" >"$tmp/qt_fn.out" 2>&1 </dev/null; fn_rc=$?
if grep -q NODISPLAY "$tmp/qt_top.out" || grep -q "not available in this build" "$tmp/qt_top.out"; then
    printf 'SKIP qtrun (no Qt or no display): qt.run from a function was NOT measured\n'
elif [ "$top_rc" != 0 ] || ! grep -qx 'TICKS 2' "$tmp/qt_top.out"; then
    tail -3 "$tmp/qt_top.out" | sed 's/^/  /'
    printf 'FAIL qtrun CONTROL (qt.run at the top level must tick and exit 0, rc %s)\n' "$top_rc"; status=1
elif [ "$fn_rc" != 0 ] || ! grep -qx 'TICKS 2' "$tmp/qt_fn.out"; then
    tail -3 "$tmp/qt_fn.out" | sed 's/^/  /'
    printf 'FAIL qtrun (qt.run inside a function: rc %s; 124 is the old hang)\n' "$fn_rc"; status=1
else
    printf 'PASS qtrun (qt.run ticks from the top level and from inside a function)\n'
fi

echo "--- TIER shape ---"
shape_prog() {   # N calls, each registering a watch and assigning fifty times
    cat <<EOF
function work(i)
    v = 0
    watch(v)
        v = v
    end watch
    for k = 1 to 50
        v = v + k
    end for
    return v
end function
total = 0
for i = 1 to $1
    total = total + work(i)
end for
print total
EOF
}
shape_prog 5000 > "$tmp/s1.bas"
shape_prog 20000 > "$tmp/s4.bas"
# A TIMEOUT IS A FAILURE, not a time: the first version timed out the large
# run of a registry that kept its tombstones at 120 s, and the CAPPED time
# made the ratio look linear (measured: P1 is 14x for 4x the calls).
ms() { local a b rc; a=$(date +%s%N); timeout -k 5 120 ./gbasic "$1" >/dev/null 2>&1; rc=$?; b=$(date +%s%N)
       [ "$rc" = 124 ] || [ "$rc" = 137 ] && { echo timeout; return; }; echo $(( (b - a) / 1000000 )); }
t1=$(ms "$tmp/s1.bas"); t4=$(ms "$tmp/s4.bas")
if [ "$t1" = timeout ] || [ "$t4" = timeout ]; then
    printf 'FAIL shape (a run did not finish in 120 s: %s, %s -- ended watches are being kept)\n' "$t1" "$t4"; status=1
elif [ "$t1" -lt 100 ]; then
    printf 'FAIL shape (the small run took only %sms -- too short to form a ratio, so nothing was measured)\n' "$t1"; status=1
else
    r10=$(( t4 * 10 / t1 ))
    if [ "$r10" -le 80 ]; then
        printf 'PASS shape (5000 calls %sms, 20000 calls %sms: %s.%sx for 4x the calls)\n' "$t1" "$t4" $(( r10 / 10 )) $(( r10 % 10 ))
    else
        printf 'FAIL shape (5000 calls %sms, 20000 calls %sms: %s.%sx for 4x the calls -- ended watches are being kept)\n' "$t1" "$t4" $(( r10 / 10 )) $(( r10 % 10 ))
        status=1
    fi
fi

echo "--- TIER valgrind ---"
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    # BOUNDED: a wait that never ends hangs valgrind with it, and an unbounded
    # tier blocked a perturbation run for 15 minutes (2026-11-08). `timeout`
    # cannot wrap the shell function vg_run, so it wraps a shell (run_net.sh).
    GBASIC_PATH=stdlib timeout -k 5 240 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/watch_scope/scope_test.bas' \
        >/dev/null 2>"$tmp/vg.err" </dev/null
    rc=$?
    GBASIC_PATH=stdlib timeout -k 5 240 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/watch_scope/wait_test.bas' \
        >/dev/null 2>>"$tmp/vg.err" </dev/null
    rc2=$?
    if [ "$rc" = 124 ] || [ "$rc2" = 124 ] || [ "$rc" = 137 ] || [ "$rc2" = 137 ]; then
        printf 'FAIL valgrind (a fixture did not finish under valgrind in 240 s: a hang)\n'; status=1
    elif [ "$rc" = "$VG_EXIT" ] || [ "$rc2" = "$VG_EXIT" ]; then
        cat "$tmp/vg.err"; printf 'FAIL valgrind\n'; status=1
    else
        printf 'PASS valgrind (no definite leak or invalid access)\n'
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

[ "$status" = 0 ] && echo "run_watch_scope: all checks passed"
exit $status
