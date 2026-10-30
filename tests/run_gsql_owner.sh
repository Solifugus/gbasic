#!/bin/bash
# G-SQL's OWNER AND PROTOCOL (include/gsql_wire.h, include/gsql_owner.h,
# include/gsql_client.h; src/gsql/wire.c, owner.c, client.c;
# docs/gsql_owner_design.md), Phase 1's second increment: the one process that
# opens a database and serves every client over a local socket
# (docs/gsql_design.md §6.1, §6.5), and the client side that reaches it or
# starts it.
#
# THE OWNER IS GIVEN A SERVICE FOR ITS STATEMENTS, and there is no SQL layer
# yet, so tests/gsql/owner_test.c supplies a trivial one (put, get, scan...)
# that is never shipped. What is under test is everything the OWNER does
# around it: the handshake, transactions, the one writer and its queue,
# cursors and batches, CANCEL, timeouts, a dropped connection, a half-sent
# frame, a client that never reads, a race to become the owner, symlinks,
# stale sockets, a runtime directory someone else could reach, the owner
# killed, and the client killed.
#
# Each tier runs real processes: an owner is the test binary re-executed, the
# client's child, armed with PR_SET_PDEATHSIG. The runtime directory is a
# short one under /tmp, because a local socket's path is limited to 108 bytes
# and a suite's work directory need not be (the owner refuses a longer one by
# name, which the paths tier asserts). The suite ends by asserting that NO
# owner outlived it.
#
# The cost tier reports the round trip against the same point read in
# process; §6.1's gate is about 20 us added, asserted only with
# GSQL_OWNER_COST_STRICT=1, since an absolute bound under a parallel gate
# measures the machine.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."

work="$(mktemp -d)"
rt="$(mktemp -d /tmp/gsql-rt.XXXXXX)"
chmod 700 "$rt"
# Nothing here kills by pattern: an owner is a test process's child, armed
# with PR_SET_PDEATHSIG, and dies with it. That is the property under test,
# so the last tier asserts it rather than a cleanup papering over it.
cleanup() {
    rm -rf "$work" "$rt"
}
trap cleanup EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

CC=${CC:-cc}
if ! command -v "$CC" >/dev/null 2>&1; then
    printf 'SKIP run_gsql_owner (no C compiler -- the owner was NOT measured)\n'
    exit 0
fi
FLAGS="-std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc/gsql"
ENGINE="src/gsql/lsm.c src/gsql/store_common.c src/gsql/store_util.c src/gsql/io.c src/gsql/io_sim.c"
LAYER="src/gsql/owner.c src/gsql/client.c src/gsql/wire.c src/gsql/v3.c src/gsql/table.c src/gsql/key.c"
SAN=""
printf 'int main(void){return 0;}\n' > "$work/probe.c"
if "$CC" -fsanitize=address,undefined -o "$work/probe" "$work/probe.c" 2>/dev/null; then
    SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi
bounded() { timeout -k 5 "$1" "${@:2}"; }

printf 'TIER build: the owner, the client and their test, -Wpedantic -Werror\n'
# shellcheck disable=SC2086
if "$CC" $FLAGS -g -O1 $SAN -o "$work/owner_test" tests/gsql/owner_test.c $LAYER $ENGINE 2> "$work/build.err" &&
   "$CC" $FLAGS -O2 -o "$work/owner_cost" tests/gsql/owner_test.c $LAYER $ENGINE 2>> "$work/build.err"; then
    pass "owner_test${SAN:+ (under ASan and UBSan)} and owner_cost (-O2)"
else
    head -8 "$work/build.err"
    fail "the owner did not compile cleanly"
    printf 'run_gsql_owner: FAILED (nothing below was run)\n'
    exit 1
fi

export XDG_RUNTIME_DIR="$rt"

# one tier: a mode, what it claims, and its bound
tier() {
    local mode=$1 claim=$2 limit=$3; shift 3
    local dir="$work/$mode"
    mkdir -p "$dir"
    printf 'TIER %s: %s\n' "$mode" "$claim"
    local out rc
    # to a FILE, not a command substitution: an owner that outlived its test
    # process would hold the substitution's pipe open, and the suite would
    # wait on it for ever instead of reporting the leak below
    bounded "$limit" "$work/owner_test" "$mode" "$dir" "$@" >"$work/$mode.out" 2>"$work/$mode.err"
    rc=$?
    out=$(cat "$work/$mode.out")
    local last
    last=$(printf '%s\n' "$out" | tail -1)
    if grep -q 'runtime error\|ERROR: AddressSanitizer\|ERROR: LeakSanitizer' "$work/$mode.err"; then
        grep -m3 'runtime error\|ERROR: AddressSanitizer\|ERROR: LeakSanitizer' "$work/$mode.err"
        fail "$mode: the sanitizers objected"
    fi
    if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        printf '%s\n' "$out" | grep MISMATCH | head -10
        fail "$mode: did not finish within ${limit}s -- a hang, reported as one"
        return
    fi
    case "$last" in
        "ok "*" 0 mismatches") pass "$mode: $last" ;;
        *) printf '%s\n' "$out" | grep MISMATCH | head -10; fail "$mode: $last" ;;
    esac
    printf '%s\n' "$out" | grep -v '^ok \|^MISMATCH' | sed 's/^/       /'
}

printf 'TIER wire: frames split at every byte, coalesced, at the limit and past it\n'
bounded 60 "$work/owner_test" wire >"$work/wire.out" 2>&1
out=$(cat "$work/wire.out")
last=$(printf '%s\n' "$out" | tail -1)
case "$last" in
    "ok "*" 0 mismatches") pass "wire: $last" ;;
    *) printf '%s\n' "$out" | grep MISMATCH | head -10; fail "wire: $last" ;;
esac

tier protocol "the handshake, transactions, the writer queue, CANCEL, cursors, pipelining, refusals" 120
tier noservice "an owner with no SQL layer refuses statements by name and still runs transactions" 60
tier timeouts "an idle writer, snapshot and cursor are ended, each beside one that is kept alive" 60
tier stall "half a frame and an unread backlog stall nobody, and nothing is lost" 120
tier race "24 clients find no owner at once, 20 times over, and each time exactly one owner results" 300 24 20
tier paths "a symlink finds the same owner, a stale socket is replaced, the runtime directory is checked" 60
tier handoff "the owner killed: the transaction fails, commits survive, nothing is retried; SIGTERM replays nothing" 60
tier lifetime "the client killed: the owner it started goes too, taking the planned handoff" 60

printf 'TIER cost: the round trip against the same point read in process (-O2, no sanitizers)\n'
mkdir -p "$work/costdir"
bounded 120 "$work/owner_cost" cost "$work/costdir" 20000 >"$work/cost.out" 2>&1
out=$(cat "$work/cost.out")
last=$(printf '%s\n' "$out" | tail -1)
case "$last" in
    "ok "*" 0 mismatches") pass "cost: $(printf '%s\n' "$out" | grep '^cost' | sed 's/^cost //')" ;;
    *) printf '%s\n' "$out" | grep MISMATCH | head -5; fail "cost: $last" ;;
esac

printf 'TIER leftovers: no owner outlives the suite\n'
sleep 0.3
left=$(pgrep -f "$work/" | tr '\n' ' ')
if [ -z "$left" ]; then
    pass "no owner process is left"
else
    fail "owner processes outlived their clients: $left"
fi

if [ "$status" = 0 ]; then
    printf 'run_gsql_owner: all checks passed\n'
else
    printf 'run_gsql_owner: FAILED\n'
fi
exit "$status"
