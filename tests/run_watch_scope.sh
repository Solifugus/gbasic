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
    GBASIC_PATH=stdlib vg_run ./gbasic tests/watch_scope/scope_test.bas >/dev/null 2>"$tmp/vg.err" </dev/null
    rc=$?
    if [ "$rc" = "$VG_EXIT" ]; then
        cat "$tmp/vg.err"; printf 'FAIL valgrind\n'; status=1
    else
        printf 'PASS valgrind (no definite leak or invalid access)\n'
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

[ "$status" = 0 ] && echo "run_watch_scope: all checks passed"
exit $status
