#!/bin/bash
# G-SQL through the whole stack at several sizes (scale.c), one fresh process
# and directory per size, one JSON line each appended to RESULTS.
#
#   research/gsql/scale.sh RESULTS [ROWS...]      (default 1000000 10000000)
#
# The disk rules bench.sh follows: never tmpfs (BENCH_DIR, default
# /var/tmp/gsql-bench), every file capped at BENCH_CAP_GB (20), a size not
# started without BENCH_MIN_FREE_GB (50) free, and the directory removed after.
set -uo pipefail
cd "$(dirname "$0")/../.."
out=${1:?results file}
shift
sizes=("$@")
[ ${#sizes[@]} -eq 0 ] && sizes=(1000000 10000000)
base=${BENCH_DIR:-/var/tmp/gsql-bench}
fs=$(findmnt -no FSTYPE -T "$(dirname "$base")" 2>/dev/null || echo unknown)
if [ "$fs" = tmpfs ]; then echo "refusing: $base is on tmpfs, where a flush costs nothing" >&2; exit 2; fi
mkdir -p "$base"
bin=$(mktemp)
trap 'rm -f "$bin"; rm -rf "$base"/scale-*' EXIT
# the commit the binary is BUILT from, taken now: stamped at the end of a run,
# a size that finished after a later commit recorded the wrong one
built_from=$(git rev-parse --short HEAD)
make -s gbasic libgbasic.a || exit 1
# shellcheck disable=SC2046
cc -std=c11 -O2 -g -Iinclude -Isrc/gsql -o "$bin" research/gsql/scale.c libgbasic.a $(make -s print-ldlibs) || exit 1
cap_gb=${BENCH_CAP_GB:-20}
min_free_gb=${BENCH_MIN_FREE_GB:-50}
ulimit -f $(( cap_gb * 1024 * 1024 ))
for n in "${sizes[@]}"; do
    avail=$(df -Pk "$base" | awk 'NR==2 {print $4}')
    if [ "${avail:-0}" -lt $(( min_free_gb * 1024 * 1024 )) ]; then
        echo "stopping: under $min_free_gb GB free on $base" >&2
        exit 3
    fi
    d="$base/scale-$n"
    rm -rf "$d"; mkdir -p "$d"
    line=$(timeout 14400 "$bin" "$d" "$n")
    rc=$?
    used=$(du -sk "$d" | awk '{print $1}')
    rm -rf "$d"
    if [ $rc -ne 0 ] || [ -z "$line" ]; then
        echo "{\"rows\": $n, \"failed\": $rc}" >> "$out"
        echo "FAILED $n ($rc)" >&2
    else
        echo "${line%\}}, \"disk_kb_at_end\": $used, \"commit\": \"$built_from\"}" >> "$out"
        echo "$n done" >&2
    fi
done
