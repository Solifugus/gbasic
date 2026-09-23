#!/usr/bin/env bash
# `exists` on a directory, and `make_dir(path, { parents: true })`.
#
# WHAT THIS REPLACED. `stdlib/persist.bas` carried a 24-line `ensure_dir` that
# split a path and walked it segment by segment, and the walk was not a matter
# of taste: `make_dir` had no parents mode, and `exists` would not take a
# directory reference, so each level had to be guarded through a FILE reference
# to a path that is not a file. The ledger carried it as a live limitation for
# months. It is one call now.
#
# WHAT DELIBERATELY DID NOT CHANGE. `make_dir` was NOT made idempotent, which
# is the obvious repair and the wrong one: a bare mkdir is ATOMIC, so "did I
# create it" is how a program takes a lock across processes without one, and an
# existing directory has to stay an error for that to keep working. The other
# question -- "make sure this path exists" -- got its own spelling instead.
# Same argument `accounting` made for refusing a second close.
#
# Tiers:
#   SEMANTICS   the self-checking fixture: 17 checks over both builtins, the
#               atomicity CONTROL, and five refusals each beside a legal
#               neighbour. Self-checking rather than golden because every
#               defect here is an ORDINARY-LOOKING BOOLEAN -- an `exists` that
#               always says true and one that is right are the same transcript
#               on a fixture that only asks about things that are there.
#   UNREADABLE  THE TIER NOTHING ELSE CAN SEE. `exists` used to be
#               `fopen(path, "rb") != NULL`, which answers a DIFFERENT question
#               and agreed with the right one only by accident: a file that is
#               present but not readable was reported ABSENT. It answers by
#               `stat` now, and only a file with its permissions removed
#               distinguishes the two implementations. The mode change has to
#               be made from OUTSIDE, because gBASIC has no chmod (a live
#               ledger bullet), which is also why this tier is here rather than
#               in the fixture.
#   MUTEX       the atomicity claim as a DEMONSTRATION rather than an assertion:
#               two contenders race for the same name and EXACTLY ONE may win.
#               Without it, "an existing directory is refused" is a fact about
#               one process in isolation, and the property being protected is
#               a property between processes.
#   VALGRIND    a new heap path walk on every parents call.
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null
export GBASIC_PATH=stdlib

work="$(mktemp -d)"
trap 'chmod -R u+rwX "$work" 2>/dev/null || true; rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER semantics: both builtins, the atomicity control, and the refusals\n'
mkdir -p "$work/fixture"
if ! timeout -k 5 60 ./gbasic tests/dir_builtins_test.bas "$work/fixture" >"$work/out" 2>"$work/err"; then
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

printf 'TIER unreadable: exists means EXISTS, not "can I open it"\n'
if [ "$(id -u)" = "0" ]; then
    pass "SKIP (running as root, which can read anything -- the tier would measure nothing)"
else
    printf 'locked\n' > "$work/locked.txt"
    chmod 000 "$work/locked.txt"
    if [ -r "$work/locked.txt" ]; then
        # Belt and braces: a filesystem or a capability could make the mode a
        # no-op, and a tier that cannot create its own premise must SAY SO
        # rather than pass.
        pass "SKIP (the file stayed readable after chmod 000 -- nothing to measure)"
    else
        printf 'print exists({file}"%s/locked.txt")\n' "$work" > "$work/u.bas"
        got=$(./gbasic "$work/u.bas" 2>&1)
        if [ "$got" = "true" ]; then
            pass "an unreadable file still exists"
        else
            fail "an unreadable file was reported ABSENT (got: $got) -- exists is answering 'can I open it'"
        fi
        # THE CONTROL. Without it, "true" is satisfied by an exists that always
        # says yes, which is exactly what this tier would otherwise reward.
        printf 'print exists({file}"%s/never-existed.txt")\n' "$work" > "$work/u2.bas"
        got2=$(./gbasic "$work/u2.bas" 2>&1)
        if [ "$got2" = "false" ]; then
            pass "CONTROL: a file that is not there is still absent"
        else
            fail "an absent file was reported present (got: $got2)"
        fi
    fi
fi

printf 'TIER mutex: exactly one contender may create a name\n'
# The property `make_dir` was deliberately NOT made idempotent to protect, run
# BETWEEN PROCESSES, which is where it matters and where the fixture cannot
# reach. Both contenders are started before either is waited on.
cat > "$work/race.bas" <<'BAS'
program main(args)
    on error goto next
    make_dir(args[0])
    if error then
        error.clear()
        print "lost"
    else
        print "won"
    end if
end program
BAS
target="$work/contended"
./gbasic "$work/race.bas" "$target" > "$work/r1" 2>&1 &
p1=$!
./gbasic "$work/race.bas" "$target" > "$work/r2" 2>&1 &
p2=$!
wait $p1 || true
wait $p2 || true
wins=$(cat "$work/r1" "$work/r2" | grep -cx 'won' || true)
losses=$(cat "$work/r1" "$work/r2" | grep -cx 'lost' || true)
if [ "$wins" = "1" ] && [ "$losses" = "1" ]; then
    pass "exactly one of two contenders created it (the lock idiom still holds)"
else
    cat "$work/r1" "$work/r2"
    fail "$wins winners and $losses losers -- make_dir is no longer a usable lock"
fi
# CONTROL: under `parents: true` the same race has NO loser, which is what
# makes the two spellings different things rather than one with a flag.
cat > "$work/race2.bas" <<'BAS'
program main(args)
    on error goto next
    make_dir(args[0], { parents: true })
    if error then
        error.clear()
        print "lost"
    else
        print "won"
    end if
end program
BAS
./gbasic "$work/race2.bas" "$work/shared" > "$work/s1" 2>&1
./gbasic "$work/race2.bas" "$work/shared" > "$work/s2" 2>&1
if [ "$(cat "$work/s1" "$work/s2" | grep -cx 'won' || true)" = "2" ]; then
    pass "CONTROL: under parents both succeed -- the two spellings are two questions"
else
    cat "$work/s1" "$work/s2"; fail "parents did not make the second call succeed"
fi

printf 'TIER valgrind\n'
if vg_available; then
    rm -rf "$work/vg"; mkdir -p "$work/vg"
    if vg_run ./gbasic tests/dir_builtins_test.bas "$work/vg" >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        tail -30 "$work/vg.err"; fail "valgrind objected"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

exit $status
