#!/bin/bash
# The Phase 0a spikes (docs/gsql_storage_design.md): build, test, and run the
# crash gate (§5.3) for every candidate built. NOT part of tests/run_all.sh, by
# the design's own rule (§1): spike code is not gated, and a written-off
# candidate is deleted with its numbers recorded in the design.
#
#   research/gsql/run.sh [ENGINE...]      default: every engine that exists
#   GATE_SCALE=quick research/gsql/run.sh  a smaller crash run, for iterating
set -uo pipefail
cd "$(dirname "$0")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
CC=${CC:-cc}
INC="-I../../include -I../../src/gsql -I."
IO="../../src/gsql/io.c ../../src/gsql/io_sim.c"
ENGINES=("$@")
if [ ${#ENGINES[@]} -eq 0 ]; then
    ENGINES=(cow)
    [ -f wal.c ] && ENGINES+=(wal)
    ENGINES+=(lsm)
fi
# 2.C is src/gsql/lsm.c since step 5; the gate's own suite is
# tests/run_gsql_store.sh, and this script keeps it beside the candidates
DEFS="-DHAVE_COW"
[ -f wal.c ] && DEFS="$DEFS -DHAVE_WAL"
ST=../../src/gsql
ENGINE_SRC="cow.c btengine.c btree.c $ST/store_common.c $ST/store_util.c $ST/lsm.c"
[ -f wal.c ] && ENGINE_SRC="$ENGINE_SRC wal.c"
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

build() { # out flags sources...
    local out=$1 flags=$2; shift 2
    if ! $CC -std=c11 -Wall -Wextra -Werror $flags $INC $DEFS -o "$work/$out" "$@" 2> "$work/cc.err"; then
        head -10 "$work/cc.err"
        fail "$out did not compile"
        return 1
    fi
}
SAN="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer"
build test_btree "$SAN" test_btree.c btree.c $ST/store_common.c $IO || exit 1
build test_store "$SAN" ../../tests/gsql/store_test.c $ENGINE_SRC $IO || exit 1
build crash_store "-g -O2" ../../tests/gsql/crash_store.c $ENGINE_SRC $IO ../../src/gsql/crash.c || exit 1
# the bench, for its version-store check (§4): the candidates only, no reference engines
build bench "$SAN" bench.c $ENGINE_SRC $IO -lm || exit 1

printf 'TIER btree: the shared B+tree against a model, both pager modes, under ASan\n'
for seed in 1 2 3; do
    out=$("$work/test_btree" "$seed" 2>&1 | tail -1)
    case "$out" in ok*) pass "seed $seed: $out" ;; *) fail "seed $seed: $out" ;; esac
done

for e in "${ENGINES[@]}"; do
    printf 'TIER store/%s: the interface against a model, under ASan\n' "$e"
    for seed in 1 2 3 4 5; do
        out=$("$work/test_store" "$e" "$seed" 2>&1)
        last=$(printf '%s\n' "$out" | tail -1)
        case "$last" in ok*) pass "seed $seed (simulated disk): $last" ;;
            *) printf '%s\n' "$out" | grep -E 'MISMATCH|ERROR|check:' | head -6; fail "seed $seed: $last" ;; esac
    done
    mkdir -p "$work/real-$e"
    out=$("$work/test_store" "$e" 9 real "$work/real-$e" 2>&1 | tail -1)
    case "$out" in ok*) pass "real filesystem: $out" ;; *) fail "real filesystem: $out" ;; esac

    printf 'TIER version/%s: the version store (§4) against a model, under ASan\n' "$e"
    # AS OF every commit that matters to each row, every second mapped, rows
    # that never existed, and the counter recovered across a reopen
    mkdir -p "$work/vs-$e"
    out=$("$work/bench" "$e" w9-check "$work/vs-$e" 200 2000 2>&1)
    case "$out" in *'"mismatches": 0}'*) pass "$(printf '%s' "$out" | grep -o '"checks": [0-9]*, "mismatches": 0')" ;;
        *) printf '%s\n' "$out" | grep MISMATCH | head -5; fail "version store: $(printf '%s' "$out" | tail -1)" ;; esac
    rm -rf "$work/vs-$e"

    printf 'TIER crash/%s: the gate, 10,000 crashes per mode, zero violations\n' "$e"
    # light: 512-byte pages, small workloads crashed at sampled points with
    # every recovery crashed too. heavy: 4 KiB pages and a delete-heavy load,
    # so free lists and meta pages span sectors (what a torn meta needs).
    # hot: 300 transactions over 6 keys, so a memtable holds several
    # versions of one key when it flushes -- no other workload does, and a
    # flush keeping the OLDEST version passed every one of them (Test 3).
    # The hot specs run 64 seeds in full: a manifest recording the death of a
    # tree whose commit was not yet durable failed 1 crash in 403,033, at
    # seed 35, and the 16 they ran before never reached it.
    if [ "${GATE_SCALE:-full}" = quick ]; then P="1-3" L="1-2" F="1-8" H="1-2" FLOOR=0; else P="1-28" L="1-16" F="1-200" H="1-64" FLOOR=10000; fi
    for spec in "process $P 40 200 8 2000 1 60 512" "power $L 40 200 8 2000 1 60 512" \
                "torn $L 40 200 8 2000 1 60 512" "failed-sync $F 40 200 8 2000 1 60 512" \
                "full-disk $L 40 200 8 2000 1 60 512" "torn 1-8 60 3000 24 2000 0 120 4096" \
                "process $H 300 6 3 40 1 60 512" "power $H 300 6 3 40 1 60 512" \
                "bigmem power $L 40 200 8 2000 1 60 512"; do
        # bigmem: a large memtable (tests/run_gsql_store.sh says why)
        # 2.C only: in 2.B the same knob is the checkpoint interval, and a
        # million frames grows the simulated log past what a run can hold
        tune=""; case "$spec" in bigmem\ *) [ "$e" = lsm ] || continue; spec=${spec#bigmem }; tune=1048576 ;; esac
        set -- $spec
        mode=$1 seeds=$2
        if [ -n "$tune" ]; then
            summary=$(GS_TUNE_A=$tune GSQL_CRASH_REPORTS=5 timeout -k 5 3600 "$work/crash_store" "$e" "$@" 2>&1)
        else
            summary=$(GSQL_CRASH_REPORTS=5 timeout -k 5 3600 "$work/crash_store" "$e" "$@" 2>&1)
        fi
        last=$(printf '%s\n' "$summary" | tail -1)
        crashes=$(printf '%s' "$last" | sed -n 's/.*crashes=\([0-9]*\).*/\1/p')
        violations=$(printf '%s' "$last" | sed -n 's/.*violations=\([0-9]*\).*/\1/p')
        if [ "${violations:-1}" != "0" ]; then
            printf '%s\n' "$summary" | grep VIOLATION | head -5
            fail "$mode (pages $9): $last"
        elif [ "$9" = 512 ] && [ "${crashes:-0}" -lt "$FLOOR" ]; then
            fail "$mode: only ${crashes:-0} crashes, short of the 10,000 the gate asks"
        else
            pass "$mode (pages $9, seeds $seeds): $crashes crashes, 0 violations"
        fi
    done
done

[ "$status" = 0 ] && printf 'research/gsql: all checks passed\n' || printf 'research/gsql: FAILED\n'
exit "$status"
