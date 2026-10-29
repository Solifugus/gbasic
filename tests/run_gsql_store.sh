#!/bin/bash
# G-SQL's STORAGE ENGINE AND VERSION STORE under the gate (include/gsql_store.h,
# src/gsql/lsm.c, include/gsql_version.h, src/gsql/version.c;
# docs/gsql_storage_design.md §1, §2.C, §4, §5.3 and §6 step 5).
#
# §2.C, the log-structured merge tree, was chosen on 2026-10-29 (§6 step 4) and
# moved here from research/gsql in step 5. Research code was deliberately NOT
# gated (§1); the chosen engine is, from the day it moved. The harnesses that
# chose it moved with it (tests/gsql/store_test.c, tests/gsql/crash_store.c),
# so the gate and research/gsql/run.sh -- which still runs 2.B, the fallback,
# beside it -- hold the engine to ONE test, not two that could drift.
#
# FOUR TIERS:
#   build    every source under -std=c11 -Wpedantic -Werror. The research build
#            did not ask for -Wpedantic, and moving the engine found an
#            unsigned __int128 in its blob collector: a GCC extension, not ISO
#            C, and absent from MSVC, so a Windows build of the engine would
#            not have compiled. Replaced with a portable 64x64 product.
#   store    the §1 interface against a model under ASan and UBSan, five seeds
#            on the simulated disk and one on the real filesystem.
#   version  the version store against a model holding every value every row
#            ever had, AS OF every id that matters and every second, with the
#            clock stepped back, rows removed and re-made, transactions
#            aborted, commits left unsynced, and reopens. It is the first
#            consumer of the engine's `txn_id` (added in step 5), and checks
#            the property that makes it usable: past every earlier commit id,
#            at most its own.
#   crash    the §5.3 gate: process crash, power loss, torn writes, a failed
#            sync and a full disk, every recovery crashed too, each mode at
#            least 10,000 crashes with ZERO violations. The gate runs a
#            reduced seed range, each mode floored well past 10,000; the hot
#            specs include seed 35, the one at which a manifest recording an
#            undurable death failed 1 crash in 403,033 (Test 3).
#            GSQL_GATE_SCALE=full runs research/gsql's full ranges, about
#            25 minutes for 2.C alone (measured 2026-10-29: 181-211 s per
#            512-byte mode).
#
# Perturbations proven red (2026-10-29), each against the version tier:
# history kept under the new id rather than the old (16,389 of 37,789 checks),
# a removal that records nothing (3,616), no clamp on a stepped-back clock
# (250), the current value answered whatever the AS OF (16,559), and txn_id
# one low (1,199). ONE WENT GREEN AND IS DOCUMENTED AS SUCH: keeping a value
# the transaction itself made is unobservable, since it sits at that
# transaction's own id, so the guard against it is a saving, not a rule.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

CC=${CC:-cc}
if ! command -v "$CC" >/dev/null 2>&1; then
    printf 'SKIP run_gsql_store (no C compiler -- the storage engine was NOT measured)\n'
    exit 0
fi
FLAGS="-std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc/gsql"
ENGINE="src/gsql/lsm.c src/gsql/store_common.c src/gsql/store_util.c src/gsql/io.c src/gsql/io_sim.c"
SAN=""
probe="$work/probe.c"
printf 'int main(void){return 0;}\n' > "$probe"
if "$CC" -fsanitize=address,undefined -o "$work/probe" "$probe" 2>/dev/null; then
    SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi

printf 'TIER build: the engine, the version store and the harnesses, -Wpedantic -Werror\n'
built=1
for spec in "store_test:-g -O1 $SAN:tests/gsql/store_test.c $ENGINE" \
            "version_test:-g -O1 $SAN:tests/gsql/version_test.c src/gsql/version.c $ENGINE" \
            "crash_store:-g -O2:tests/gsql/crash_store.c src/gsql/crash.c $ENGINE"; do
    name=${spec%%:*}; rest=${spec#*:}; flags=${rest%%:*}; srcs=${rest#*:}
    # shellcheck disable=SC2086
    if "$CC" $FLAGS $flags -o "$work/$name" $srcs 2> "$work/$name.err"; then
        pass "$name"
    else
        head -8 "$work/$name.err"
        fail "$name did not compile cleanly"
        built=0
    fi
done
if [ "$built" = 0 ]; then printf 'run_gsql_store: FAILED (nothing below was run)\n'; exit 1; fi
[ -n "$SAN" ] || printf '  note the compiler has no ASan/UBSan: the model tiers run without them\n'

# A hang is not a failure: every run is bounded.
bounded() { timeout -k 5 "$1" "${@:2}"; }

# The model runs and the crash specs are independent, so each tier starts
# them together and reads the verdicts in order: measured serial, the suite
# took 7m39s on this machine, most of it waiting on one process at a time.
printf 'TIER store: the interface against a model%s\n' "${SAN:+, under ASan and UBSan}"
mkdir -p "$work/real" "$work/vreal"
for seed in 1 2 3 4 5; do bounded 300 "$work/store_test" lsm "$seed" > "$work/store.$seed" 2>&1 & done
bounded 300 "$work/store_test" lsm 9 real "$work/real" > "$work/store.real" 2>&1 &
for seed in 1 2 3; do bounded 300 "$work/version_test" "$seed" > "$work/version.$seed" 2>&1 & done
bounded 300 "$work/version_test" 4 real "$work/vreal" > "$work/version.real" 2>&1 &
wait
for seed in 1 2 3 4 5; do
    last=$(tail -1 "$work/store.$seed")
    case "$last" in ok*) pass "seed $seed (simulated disk): $last" ;;
        *) grep -E 'MISMATCH|ERROR|check:' "$work/store.$seed" | head -6; fail "seed $seed: $last" ;; esac
done
last=$(tail -1 "$work/store.real")
case "$last" in ok*) pass "real filesystem: $last" ;; *) fail "real filesystem: $last" ;; esac

printf 'TIER version: the version store against a model, on the engine'"'"'s own commit ids\n'
for seed in 1 2 3 real; do
    last=$(tail -1 "$work/version.$seed")
    n=$(printf '%s' "$last" | sed -n 's/^ok \([0-9]*\) checks/\1/p')
    where="seed $seed (simulated disk)"; [ "$seed" = real ] && where="real filesystem"
    if [ -n "$n" ] && [ "$n" -ge 20000 ]; then pass "$where: $n checks"
    elif [ -n "$n" ]; then fail "$where: only $n checks ran"
    else grep MISMATCH "$work/version.$seed" | head -6; fail "$where: $last"; fi
done

printf 'TIER crash: the §5.3 gate, at least 10,000 crashes per mode, zero violations\n'
if [ "${GSQL_GATE_SCALE:-gate}" = full ]; then
    P="1-28" L="1-16" F="1-200" D="1-16" H="1-64"
else
    P="1-4" L="1-2" F="1-24" D="1-8" H="30-40"
fi
specs=("process $P 40 200 8 2000 1 60 512" "power $L 40 200 8 2000 1 60 512"
       "torn $L 40 200 8 2000 1 60 512" "failed-sync $F 40 200 8 2000 1 60 512"
       "full-disk $D 40 200 8 2000 1 60 512" "torn 1-8 60 3000 24 2000 0 120 4096"
       "process $H 300 6 3 40 1 60 512" "power $H 300 6 3 40 1 60 512"
       "bigmem power $L 40 200 8 2000 1 60 512")
# `bigmem`: a 1 MiB memtable. The 2,000-byte default flushes the memtable,
# and so fsyncs the log, on nearly every commit, which MASKS a sync that does
# nothing: a no-op `flush` passed every spec above, and passes this one too
# under the old sampler (46,187 crashes, 0 violations); with the sampler's
# just-after-a-sync points it is caught (131 violations).
for i in "${!specs[@]}"; do
    spec=${specs[$i]} tune=""
    case "$spec" in bigmem\ *) spec=${spec#bigmem }; tune=1048576 ;; esac
    # shellcheck disable=SC2086
    # only when wanted: an EMPTY GS_TUNE_A reads as 0, which is the engine's
    # own 32 MiB default, not the adapter's 2,000 bytes
    if [ -n "$tune" ]; then
        GS_TUNE_A=$tune GSQL_CRASH_REPORTS=5 bounded 3600 "$work/crash_store" lsm $spec > "$work/crash.$i" 2>&1 &
    else
        GSQL_CRASH_REPORTS=5 bounded 3600 "$work/crash_store" lsm $spec > "$work/crash.$i" 2>&1 &
    fi
done
wait
for i in "${!specs[@]}"; do
    big=""; spec=${specs[$i]}
    case "$spec" in bigmem\ *) spec=${spec#bigmem }; big=", 1 MiB memtable" ;; esac
    # shellcheck disable=SC2086
    set -- $spec
    last=$(tail -1 "$work/crash.$i")
    crashes=$(printf '%s' "$last" | sed -n 's/.*crashes=\([0-9]*\).*/\1/p')
    violations=$(printf '%s' "$last" | sed -n 's/.*violations=\([0-9]*\).*/\1/p')
    if [ "${violations:-1}" != "0" ]; then
        grep VIOLATION "$work/crash.$i" | head -5
        fail "$1 (pages $9): $last"
    elif [ "$9" = 512 ] && [ "${crashes:-0}" -lt 10000 ]; then
        fail "$1 (seeds $2): only ${crashes:-0} crashes, short of the 10,000 the gate asks"
    else
        pass "$1 (pages $9, seeds $2$big): $crashes crashes, 0 violations"
    fi
done

[ "$status" = 0 ] && printf 'run_gsql_store: all checks passed\n' || printf 'run_gsql_store: FAILED\n'
exit "$status"
