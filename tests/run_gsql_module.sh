#!/bin/bash
# G-SQL FROM A PROGRAM: the `gsql` module (src/modules/gsql.c;
# docs/gsql_module_design.md), PHASE 1's FOURTH INCREMENT. `load gsql`, then
# gsql.connect / query / exec / begin / commit / rollback / close, each call one
# request to the database's owner, which the program starts as its child.
#
# SELF-CHECKING NOT GOLDEN: every answer in tests/gsql/module_test.bas is
# written there by hand, and the defects this module can have are plausible
# values -- an integer one short past 2^53, money without its currency, a
# result cut at the first batch.
#
# TIERS:
#   semantics   the fixture's main phase: values in both directions, exec's
#               answer shape, batches, transactions, refusals with their codes
#   lost        the owner is killed under a program using it: the request in
#               flight fails with LOST and nothing is retried, and the next one
#               reaches a new owner that recovered what was committed
#   valgrind    the main phase again, no invalid access, no definite leak
#   leftovers   no owner outlives the suite, read-only
. "$(dirname "$0")/portable.sh"
. "$(dirname "$0")/valgrind_tier.sh"
set -uo pipefail
cd "$(dirname "$0")/.."

work="$(mktemp -d)"
cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

if ! make -s gbasic >"$work/make.out" 2>&1; then
    tail -20 "$work/make.out"
    printf 'run_gsql_module: FAILED (./gbasic did not build)\n'
    exit 1
fi
printf 'load gsql\nprint "loaded"\n' > "$work/probe.bas"
if ! probe=$(./gbasic "$work/probe.bas" 2>&1) || [ "$probe" != loaded ]; then
    printf 'SKIP run_gsql_module (this build has no G-SQL: %s -- the module was NOT measured)\n' "$probe"
    exit 0
fi

export GBASIC_PATH=stdlib
FIXTURE=tests/gsql/module_test.bas

# the fixture's verdict: its last line, and a floor on how many checks ran, so
# a fixture that stopped early cannot pass by mismatching nothing
verdict() {
    local name=$1 file=$2 rc=$3 floor=$4
    grep MISMATCH "$file" | head -12
    local last n
    last=$(tail -1 "$file")
    n=$(printf '%s\n' "$last" | sed -n 's/^ok \([0-9]*\) checks, 0 mismatches$/\1/p')
    if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        fail "$name: did not finish -- a hang, reported as one"
    elif [ "$rc" != 0 ]; then
        grep -m3 'runtime error' "$file"
        fail "$name: the fixture exited $rc (${last:-no verdict})"
    elif [ -z "$n" ]; then
        fail "$name: ${last:-no verdict}"
    elif [ "$n" -lt "$floor" ]; then
        fail "$name: only $n checks ran, under the floor of $floor"
    else
        pass "$name: $last"
    fi
}

printf 'TIER semantics: a program as a client, every answer written by hand\n'
mkdir -p "$work/main"
GSQL_PHASE=main GSQL_DIR="$work/main" timeout -k 5 120 ./gbasic "$FIXTURE" >"$work/main.out" 2>"$work/main.err"
verdict semantics "$work/main.out" $? 50
if [ -s "$work/main.err" ]; then
    sed 's/^/       /' "$work/main.err" | head -5
    fail "semantics: the fixture wrote to stderr"
fi

printf 'TIER lost: the owner is killed under a program that is using it\n'
mkdir -p "$work/lost"
GSQL_PHASE=lost GSQL_DIR="$work/lost" timeout -k 5 60 ./gbasic "$FIXTURE" >"$work/lost.out" 2>"$work/lost.err" &
prog=$!
for _ in $(seq 1 400); do
    [ -e "$work/lost/ready" ] && break
    sleep 0.05
done
owner=""
if [ -e "$work/lost/ready" ]; then
    owner=$(pgrep -f -- "--gsql-owner $work/lost/lost " | head -1)
fi
if [ -z "$owner" ]; then
    fail "lost: the fixture never said it was ready, or no owner was found to kill"
    kill -9 "$prog" 2>/dev/null
    wait "$prog" 2>/dev/null
else
    # SIGKILL, not SIGTERM: the owner gets no planned handoff, so the next
    # owner's recovery is the crash path
    kill -9 "$owner"
    # dead or a zombie: the owner is the program's child and stays a zombie
    # until the program reaps it, which it does only once it is told to go on,
    # so `kill -0` alone would wait for ever
    for _ in $(seq 1 250); do
        st=$(ps -o stat= -p "$owner" 2>/dev/null | tr -d ' ')
        case "$st" in ''|Z*) break ;; esac
        sleep 0.02
    done
    touch "$work/lost/killed"
    wait "$prog"
    verdict lost "$work/lost.out" $? 4
fi

printf 'TIER valgrind: the main phase, no invalid access, no definite leak\n'
if vg_available; then
    mkdir -p "$work/vg"
    GSQL_PHASE=main GSQL_DIR="$work/vg" vg_run ./gbasic "$FIXTURE" >"$work/vg.out" 2>"$work/vg.err"
    vgst=$?
    # compared against $VG_EXIT: the fixture failing and valgrind objecting
    # are different findings (run_repl.sh's lesson)
    if [ "$vgst" = 0 ] && tail -1 "$work/vg.out" | grep -q ' 0 mismatches$'; then
        pass "valgrind: clean over $(tail -1 "$work/vg.out" | sed 's/^ok //')"
    elif [ "$vgst" = "$VG_EXIT" ]; then
        grep -m8 'Invalid\|definitely lost\|uninitialised' "$work/vg.err"
        fail "valgrind objected"
    else
        tail -3 "$work/vg.out"
        fail "valgrind: the fixture did not pass under valgrind (exit $vgst; its mismatches are above)"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

printf 'TIER leftovers: no owner outlives the program that started it\n'
# the owner is the program's child and is ended with it (SIGTERM, then a
# planned handoff), which takes a moment
left=""
for _ in $(seq 1 100); do
    left=$(pgrep -f -- "--gsql-owner $work/" | tr '\n' ' ')
    [ -z "$left" ] && break
    sleep 0.1
done
if [ -z "$left" ]; then
    pass "no owner process is left"
else
    fail "owner processes outlived their programs: $left"
fi

if [ "$status" = 0 ]; then
    printf 'run_gsql_module: all checks passed\n'
else
    printf 'run_gsql_module: FAILED\n'
fi
exit "$status"
