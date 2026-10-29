#!/bin/bash
# Run the Phase 0a workloads (bench.c) over every engine, each run a fresh
# process on a fresh directory, and append one JSON line per run to RESULTS.
#
#   research/gsql/bench.sh RESULTS [RECORDS [OPS]] [-- WORKLOAD...]
#
# The data directory must be on a real disk: tmpfs makes every flush free
# (BENCH_DIR, default /var/tmp/gsql-bench). The OS page cache is not dropped
# between runs (that needs root), so this measures the engines' caches, not
# cold reads.
set -uo pipefail
cd "$(dirname "$0")"
out=${1:?results file}
records=${2:-100000}
ops=${3:-20000}
shift $(( $# < 3 ? $# : 3 ))
[ "${1:-}" = "--" ] && shift
workloads=("$@")
[ ${#workloads[@]} -eq 0 ] && workloads=(ycsb-load ycsb-a ycsb-b ycsb-c ycsb-d ycsb-e ycsb-f w1-uniform w2 w5-100)
engines=(${BENCH_ENGINES:-wal lsm sqlite lmdb rocksdb})
base=${BENCH_DIR:-/var/tmp/gsql-bench}
fs=$(findmnt -no FSTYPE -T "$(dirname "$base")" 2>/dev/null || echo unknown)
if [ "$fs" = tmpfs ]; then echo "refusing: $base is on tmpfs, where a flush costs nothing" >&2; exit 2; fi
mkdir -p "$base"
dev=$(findmnt -no SOURCE -T "$base" 2>/dev/null || echo unknown)
bin=$(mktemp)
trap 'rm -f "$bin"; rm -rf "$base"/run-*' EXIT
extra_defs="" extra_libs=""
[ -f /usr/include/lmdb.h ] && { extra_defs="$extra_defs -DHAVE_LMDB"; extra_libs="$extra_libs -llmdb"; }
[ -f /usr/include/rocksdb/c.h ] && { extra_defs="$extra_defs -DHAVE_ROCKSDB"; extra_libs="$extra_libs -lrocksdb"; }
cc -std=c11 -O2 -g -DHAVE_WAL -DHAVE_LSM -DHAVE_SQLITE $extra_defs -I../../include -I../../src/gsql -I. -o "$bin" bench.c wal.c ../../src/gsql/lsm.c \
    btengine.c btree.c ../../src/gsql/store_common.c ../../src/gsql/store_util.c ../../src/gsql/io.c ../../src/gsql/io_sim.c -lsqlite3 -lm $extra_libs || exit 1
# A runaway engine must not fill the disk (one did: 419 GB, from 2.A's first
# free list). Every file is capped, and a run does not start without room.
cap_gb=${BENCH_CAP_GB:-20}
# a small device (a flash drive) needs a lower floor; BENCH_MIN_FREE_GB=0
# with a small BENCH_CAP_GB keeps a run inside it
min_free_gb=${BENCH_MIN_FREE_GB:-50}
ulimit -f $(( cap_gb * 1024 * 1024 ))
# BENCH_MEM=2G runs each process under a memory cgroup of that size, swap
# off. Page cache is charged to the cgroup, so data larger than the cap is
# genuinely larger than memory, which needs no root and no cache dropping.
wrap=()
if [ -n "${BENCH_MEM:-}" ]; then
    wrap=(systemd-run --user --scope -q -p "MemoryMax=$BENCH_MEM" -p MemorySwapMax=0)
fi
for w in "${workloads[@]}"; do
    for e in "${engines[@]}"; do
        avail=$(df -Pk "$base" | awk 'NR==2 {print $4}')
        if [ "${avail:-0}" -lt $(( min_free_gb * 1024 * 1024 )) ]; then
            echo "stopping: under $min_free_gb GB free on $base" >&2
            exit 3
        fi
        d="$base/run-$e-$w"
        rm -rf "$d"; mkdir -p "$d"
        line=$("${wrap[@]}" timeout 3600 "$bin" "$e" "$w" "$d" "$records" "$ops")
        rc=$?
        rm -rf "$d"
        if [ $rc -ne 0 ] || [ -z "$line" ]; then
            echo "{\"engine\": \"$e\", \"workload\": \"$w\", \"failed\": $rc}" >> "$out"
            echo "FAILED $e $w ($rc)" >&2
        else
            line="${line%\}}, \"device\": \"$dev\"}"
            [ -n "${BENCH_MEM:-}" ] && line="${line%\}}, \"mem_cap\": \"$BENCH_MEM\"}"
            echo "$line" >> "$out"
            printf '%-7s %-11s done\n' "$e" "$w" >&2
        fi
    done
done
