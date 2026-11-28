#!/bin/bash
# The analytics benchmark (docs/columnar_analytics_design.md), not a suite:
#   tools/bench_analytics/run.sh WORKDIR       (SIZES="100000 1000000" to choose)
# Makes WORKDIR/data with gen.py if it is not there, and writes
# WORKDIR/results.tsv. pandas is python3's; polars is PYTHON_POLARS's
# (default ~/.venvs/gbench/bin/python). Runs ./gbasic from the tree.
# Runs every implementation at every size into results.tsv:
#   impl  rows  task  seconds  peak_kb  answer
# gBASIC tasks are one process each (load included; `load` alone is its own
# row, so a task's own cost is its seconds less load's). Python's seconds are
# timed inside the process, best of three, load apart. Best of RUNS for a
# gBASIC process at 1M and below, one run at 10M.
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
B="${1:?usage: run.sh WORKDIR}"
mkdir -p "$B/data" "$B/db"
B="$(cd "$B" && pwd)"
cd "$HERE"
export GBASIC_PATH="$REPO/stdlib"
G="$REPO/gbasic"
POLARS_PY="${PYTHON_POLARS:-$HOME/.venvs/gbench/bin/python}"
OUT="$B/results.tsv"
: >"$OUT"
TASKS="filter group bycust top join month"
T=/usr/bin/time

one() { # impl rows task timeout cmd...
    local impl="$1" n="$2" task="$3" lim="$4"; shift 4
    local runs=3; [ "$n" -ge 10000000 ] && runs=1
    [ "$task" = build ] && runs=1   # a second build meets the first's tables
    local best="" bestkb="" ans=""
    for _ in $(seq 1 $runs); do
        $T -f "%e %M" -o "$B/t.time" timeout "$lim" "$@" >"$B/t.out" 2>"$B/t.err"
        local rc=$?
        if [ "$rc" != 0 ]; then
            ans="FAILED rc=$rc $(head -c 160 "$B/t.err" | tr '\n\t' '  ')"
            [ "$rc" = 124 ] && ans="TIMEOUT after ${lim}s"
            best="$lim"; bestkb="-"; break
        fi
        read -r s kb <"$B/t.time"
        ans="$(head -c 400 "$B/t.out" | tr '\n\t' '  ')"
        if [ -z "$best" ] || awk "BEGIN{exit !($s < $best)}"; then best="$s"; bestkb="$kb"; fi
    done
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$impl" "$n" "$task" "$best" "$bestkb" "$ans" | tee -a "$OUT"
}

for n in ${SIZES:-100000 1000000 10000000}; do
    o="$B/data/orders_$n.csv"; c="$B/data/customers.csv"
    [ -f "$o" ] || python3 gen.py "$n" "$B/data"
    # python, in process
    for lib in pandas polars; do
        py=python3; [ "$lib" = polars ] && py="$POLARS_PY"
        $T -f "%M" -o "$B/p.time" $py py_bench.py "$lib" "$o" "$c" >"$B/p.out" 2>"$B/p.err" || cat "$B/p.err"
        kb=$(tail -1 "$B/p.time")
        while IFS=$'\t' read -r task s ans; do
            printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$lib" "$n" "$task" "$s" "$kb" "$ans" | tee -a "$OUT"
        done <"$B/p.out"
    done
    # gBASIC loops, interpreted and compiled
    for mode in 0 all; do
        impl=loop; [ "$mode" = all ] && impl=loop_compiled
        for task in load $TASKS; do
            GBASIC_COMPILE=$mode one "$impl" "$n" "$task" 1800 "$G" loop.bas "$o" "$c" "$task"
        done
    done
    # G-SQL: build once, then each query
    # a new folder each run, so nothing is removed
    dbdir="$(mktemp -d "$B/db/run.XXXXXX")"
    db="$dbdir/o$n"
    one gsql "$n" build 3600 "$G" gsqlbench.bas "$db" "$o" "$c" build
    for task in $TASKS; do
        one gsql "$n" "$task" 1800 "$G" gsqlbench.bas "$db" "$o" "$c" "$task"
    done
    # frame.bas: only where it can finish (its summarize and join are O(n x groups) and O(n x m))
    if [ "$n" -le 100000 ]; then
        for task in load filter group top; do
            one frame "$n" "$task" 900 "$G" framebench.bas "$o" "$c" "$task"
        done
    fi
done
