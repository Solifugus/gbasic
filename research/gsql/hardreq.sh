#!/bin/bash
# Test 3 (docs/gsql_storage_design.md §6): the hard requirements, run against
# 2.C with 2.B alongside and 2.A as a reference. One JSON line per case on
# stdout; see hardreq.c for what each line has checked.
#
#   research/gsql/hardreq.sh [OUT.jsonl]          Test 3
#   SET=test4 research/gsql/hardreq.sh [OUT.jsonl] Test 4: the sliced backup, and
#                                                  open question 6's file counts
#
# Data goes under BENCH_DIR (default /var/tmp/gsql-bench, ext4: never tmpfs),
# each case in its own directory, removed when the case ends. A case is
# skipped, and says so, when free space is under BENCH_MIN_FREE_GB.
set -uo pipefail
cd "$(dirname "$0")"
OUT=${1:-/dev/stdout}
BENCH_DIR=${BENCH_DIR:-/var/tmp/gsql-bench}
MIN_FREE=${BENCH_MIN_FREE_GB:-50}
mkdir -p "$BENCH_DIR"
bin="$BENCH_DIR/hardreq.bin"
cc -std=c11 -O2 -g -Wall -Wextra -Werror -DHAVE_WAL -DHAVE_LSM -I../../include -I. -o "$bin" hardreq.c \
    cow.c btengine.c btree.c common.c util.c wal.c lsm.c ../../src/gsql/io.c ../../src/gsql/io_sim.c || exit 1

run() { # test engine args...
    local free
    free=$(df --output=avail -BG "$BENCH_DIR" | tail -1 | tr -dc 0-9)
    if [ "$free" -lt "$MIN_FREE" ]; then
        printf '{"test":"%s","engine":"%s","skipped":"only %sG free"}\n' "$1" "$2" "$free" >> "$OUT"
        return
    fi
    local d
    d=$(mktemp -d -p "$BENCH_DIR" case.XXXXXX)
    local t=$1 e=$2; shift 2
    timeout -k 10 3600 "$bin" "$t" "$e" "$d" "$@" >> "$OUT" 2> "$d.err" ||
        printf '{"test":"%s","engine":"%s","args":"%s","failed":"exit %s: %s"}\n' "$t" "$e" "$*" "$?" "$(tail -1 "$d.err" | tr -d '"')" >> "$OUT"
    rm -rf "$d" "$d.err"
}

if [ "${SET:-test3}" = test4 ]; then
    # one step (the monolithic backup) against slices, a write between each
    for b in 18446744073709551615 4194304 1048576 262144; do
        for e in wal lsm; do run sbackup $e 1000000 $b; done
    done
    run sbackup cow 1000000 1048576
    for e in wal lsm; do run sbackup $e 10000000 1048576; done
    for n in 10000 100000 1000000 10000000; do for e in cow wal lsm; do run files $e $n; done; done
    rm -f "$bin"
    exit 0
fi

for e in cow wal lsm; do run g4 $e; done
for n in 100 10000 1000000; do for e in cow wal lsm; do run g1 $e $n; done; done
for n in 10000 1000000; do for e in cow wal lsm; do run backup $e $n 1000; done; done
for e in cow wal lsm; do for h in 0 1; do run held $e 20000 $h; done; done
for e in wal lsm; do for h in 0 1; do run held $e 200000 $h; done; done
for h in 0 1; do run held lsm 2000000 $h; done
for c in 1000 10000 100000 1000000; do
    for e in wal lsm; do
        run recovery $e $c 0
        # a held reader stops 2.B's checkpoint, so its log grows ~9 KB a
        # commit: capped at 10^5 commits to stay inside the disk budget
        if [ "$e" = lsm ] || [ "$c" -le 100000 ]; then run recovery $e $c 1; fi
    done
done
rm -f "$bin"
