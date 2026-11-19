#!/bin/bash
# GATE-LANE: serial -- asserts that four forked readers finish four reads faster than one owner slicing them, which needs cores no other suite is using
# G-SQL'S FORKED READERS (docs/gsql_parallel_readers_design.md, increment 2),
# through the real SQL layer: a read still working after its first slice is
# finished by a reader the owner forks, whose snapshot is the fork, and whose
# answers the owner relays. What the owner does around it (CANCEL, a closed
# cursor, the cap, a read in a transaction, a stopped owner) is held by
# run_gsql_owner.sh's readers tier over its test service.
#
# TIERS:
#   differential  14 statements (aggregates, sorts, joins, windows, a set
#                 operation, a subquery, WITH) over 40 000 rows, run with
#                 readers and with GBASIC_GSQL_READERS=0: every row printed,
#                 the two outputs byte-identical, readers in one run and none
#                 in the other
#   snapshot      a reader holding a cursor while another connection deletes,
#                 updates and inserts enough to compact many times: its rows
#                 are the ones there when it began, every file its snapshot
#                 names is still there while it reads, and they go once it ends
#                 (a 64 KB memtable, 2 read handles, so files are reopened by
#                 name)
#   killreader    the reader killed part way through a read: the rest is LOST,
#                 never a short answer, and the owner and the next read go on
#   killowner     the owner killed part way through: its reader goes with it
#   write         a write planted in every reader is refused READ_ONLY, while
#                 a read no reader answers still works
#   speed         N clients each running a GROUP BY over 100 000 rows ten
#                 times, against an owner with readers and one without; 4 must
#                 be at least 1.5x faster forked, and a point reader beside
#                 them must see a p99 under half the sliced one. Skips under
#                 6 cores or at GATE_TIME_SCALE above 1, saying so
#   leftovers     no owner and no reader outlives the suite
. "$(dirname "$0")/portable.sh"
. "$(dirname "$0")/premise.sh"   # premise_time_claims
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
    printf 'run_gsql_readers: FAILED (./gbasic did not build)\n'
    exit 1
fi
printf 'load gsql\nprint "loaded"\n' > "$work/probe.bas"
if ! probe=$(./gbasic "$work/probe.bas" 2>&1) || [ "$probe" != loaded ]; then
    printf 'SKIP run_gsql_readers (this build has no G-SQL: %s -- forked readers were NOT measured)\n' "$probe"
    exit 0
fi
export GBASIC_PATH=stdlib
bin="$PWD/gbasic"
here="$PWD"

# each tier in a directory of its own, closed to other users as G-SQL requires
tierdir() {
    mkdir -p "$work/$1"
    chmod 700 "$work/$1"
    printf '%s' "$work/$1"
}

starts() {
    if [ -f "$1" ]; then
        grep -c '^start ' "$1"
    else
        printf '0\n'
    fi
}

printf 'TIER differential: the same statements forked and sliced answer the same rows\n'
d=$(tierdir diff)
GSQL_DIR=$d GSQL_NAME=forked.gsql GBASIC_GSQL_READER_LOG=$d/forked.log \
    timeout -k 5 300 ./gbasic tests/gsql/readers_diff.bas >"$d/forked.out" 2>"$d/forked.err"
frc=$?
GSQL_DIR=$d GSQL_NAME=sliced.gsql GBASIC_GSQL_READERS=0 GBASIC_GSQL_READER_LOG=$d/sliced.log \
    timeout -k 5 300 ./gbasic tests/gsql/readers_diff.bas >"$d/sliced.out" 2>"$d/sliced.err"
src=$?
nf=$(starts "$d/forked.log")
ns=$(starts "$d/sliced.log")
answers=$(grep -c ' rows$' "$d/forked.out")
lines=$(wc -l < "$d/forked.out" | tr -d ' ')
if [ "$frc" != 0 ] || [ "$src" != 0 ] || [ -s "$d/forked.err" ] || [ -s "$d/sliced.err" ]; then
    head -3 "$d/forked.err" "$d/sliced.err"
    fail "differential: a run failed (forked $frc, sliced $src)"
elif [ "$answers" != 14 ]; then
    fail "differential: $answers of 14 statements answered"
elif ! cmp -s "$d/forked.out" "$d/sliced.out"; then
    diff "$d/forked.out" "$d/sliced.out" | head -6
    fail "differential: forked and sliced answers differ"
elif [ "$nf" -lt 8 ] || [ "$ns" != 0 ]; then
    fail "differential: $nf readers forked (at least 8 wanted) and $ns with readers off (none wanted)"
else
    pass "differential: 14 statements, $lines lines identical; $nf reads forked, none with readers off"
fi

printf 'TIER snapshot: a reader reads the database as it began, and the files it reads are kept for it\n'
d=$(tierdir snapshot)
(cd "$d" && GSQL_DIR=$d GBASIC_GSQL_TEST_TUNE=65536,2,0,2 GBASIC_GSQL_READER_LOG=$d/log \
    timeout -k 5 300 "$bin" "$here/tests/gsql/readers_snapshot.bas") >"$d/out" 2>"$d/err"
rc=$?
grep MISMATCH "$d/out" | head -8
if [ "$rc" != 0 ] || [ -s "$d/err" ]; then
    head -3 "$d/err"
    fail "snapshot: the fixture exited $rc"
elif ! grep -qx 'mismatches: 0' "$d/out" || ! grep -qx 'checks: 9' "$d/out"; then
    fail "snapshot: $(tail -2 "$d/out" | tr '\n' ' ')"
elif [ "$(starts "$d/log")" -lt 1 ]; then
    fail "snapshot: no reader was forked, so nothing was held"
else
    pass "snapshot: 9 checks, $(starts "$d/log") readers"
fi

# a reader, or its owner, killed while the program waits after the first row
killtier() {
    local mode=$1
    local d
    d=$(tierdir "kill$mode")
    (cd "$d" && GSQL_DIR=$d GBASIC_GSQL_READER_LOG=$d/log timeout -k 5 120 "$bin" "$here/tests/gsql/readers_kill.bas") \
        >"$d/out" 2>"$d/err" &
    local prog=$!
    for _ in $(seq 1 300); do
        [ -e "$d/streaming" ] && break
        sleep 0.1
    done
    local reader owner
    reader=$(grep '^start ' "$d/log" 2>/dev/null | tail -1 | awk '{print $2}')
    owner=$( [ -n "$reader" ] && ps -o ppid= -p "$reader" | tr -d ' ')
    if [ ! -e "$d/streaming" ] || [ -z "$reader" ] || [ -z "$owner" ]; then
        touch "$d/go"
        wait "$prog"
        fail "kill$mode: the read never reached a reader (${reader:-no reader})"
        return
    fi
    if [ "$mode" = reader ]; then
        kill -9 "$reader"
    else
        kill -9 "$owner"
        for _ in $(seq 1 50); do
            kill -0 "$reader" 2>/dev/null || break
            sleep 0.1
        done
    fi
    # a killed reader is a zombie until its owner reaps it, so only a reader
    # whose OWNER was killed is asked whether it is still there
    local alive=no
    [ "$mode" = owner ] && kill -0 "$reader" 2>/dev/null && alive=yes
    touch "$d/go"
    wait "$prog"
    local rc=$?
    if [ "$rc" != 0 ] || [ -s "$d/err" ]; then
        head -3 "$d/err"
        fail "kill$mode: the program exited $rc"
    elif ! grep -qx 'rows before it ended: fewer than all' "$d/out" || ! grep -q '\[LOST\]' "$d/out"; then
        sed 's/^/       /' "$d/out"
        fail "kill$mode: the read did not end LOST part way"
    elif ! grep -qx 'a new read: 30000' "$d/out"; then
        fail "kill$mode: the database did not answer the next read"
    elif [ "$alive" = yes ]; then
        kill -9 "$reader" 2>/dev/null
        fail "kill$mode: the reader $reader outlived its killed owner"
    elif [ "$mode" = reader ] && ! grep -qx "reaped $reader signal 9" "$d/log"; then
        fail "killreader: the owner did not reap its killed reader"
    else
        pass "kill$mode: LOST part way ($(grep -o 'cursor [0-9]* was read by a reader\|the owner (pid [0-9]*) exited' "$d/out")), and the next read answered"
    fi
}
printf 'TIER killreader: a reader killed part way is LOST for its read, and nothing else\n'
killtier reader
printf 'TIER killowner: an owner killed part way takes its reader with it\n'
killtier owner

printf 'TIER write: a write planted in a reader is refused\n'
d=$(tierdir write)
GSQL_DIR=$d GBASIC_GSQL_TEST_READER_WRITE=1 timeout -k 5 120 ./gbasic tests/gsql/readers_write.bas >"$d/out" 2>"$d/err"
rc=$?
want='a long read: gsql.query: a reader'"'"'s planted write was answered READ_ONLY [STORAGE] -- in "select c, count(*) as n from t group by c order by c"'
if [ "$rc" != 0 ] || [ -s "$d/err" ]; then
    head -3 "$d/err"
    fail "write: the fixture exited $rc"
elif [ "$(sed -n 1p "$d/out")" != "$want" ]; then
    sed 's/^/       /' "$d/out"
    fail "write: the planted write was not refused READ_ONLY"
elif [ "$(sed -n 2p "$d/out")" != 'a short read: 5074' ]; then
    fail "write: a read no reader answers failed: $(sed -n 2p "$d/out")"
else
    pass "write: refused READ_ONLY, and the owner went on"
fi

printf 'TIER speed: N clients at once, forked against sliced\n'
cores=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
if ! premise_time_claims; then
    printf '  SKIP speed: at GATE_TIME_SCALE=%s -- forked readers against slicing was NOT measured\n' "$GATE_TIME_SCALE"
elif [ "$cores" -lt 6 ]; then
    printf '  SKIP speed: %s cores, under the 6 four readers, the owner and the clients need -- NOT measured\n' "$cores"
else
    # one database per mode, loaded once; each run's wall from outside
    run_speed() {
        local dir=$1 readers=$2 n=$3
        rm -f "$dir/done"
        GSQL_DIR=$dir GBASIC_GSQL_READERS=$readers GSQL_ROLE=point ./gbasic tests/gsql/readers_speed.bas >"$dir/point.out" 2>&1 &
        local pp=$!
        sleep 0.3
        local t0 t1 pids=""
        t0=$(date +%s%N)
        for i in $(seq 1 "$n"); do
            GSQL_DIR=$dir GBASIC_GSQL_READERS=$readers GSQL_ROLE=group GSQL_TIMES=10 ./gbasic tests/gsql/readers_speed.bas \
                >"$dir/group$i.out" 2>&1 &
            pids="$pids $!"
        done
        # shellcheck disable=SC2086
        wait $pids
        t1=$(date +%s%N)
        touch "$dir/done"
        wait "$pp"
        for i in $(seq 1 "$n"); do
            [ "$(cat "$dir/group$i.out")" = "groups 10090" ] || printf 'BAD %s\n' "$(head -1 "$dir/group$i.out")"
        done
        printf '%s %s\n' $(( (t1 - t0) / 1000000 )) "$(sed -n 's/.*p99 \([0-9.]*\) ms.*/\1/p' "$dir/point.out")"
    }
    f=$(tierdir speedforked)
    s=$(tierdir speedsliced)
    GSQL_DIR=$f GSQL_ROLE=load ./gbasic tests/gsql/readers_speed.bas >/dev/null 2>&1
    GSQL_DIR=$s GBASIC_GSQL_READERS=0 GSQL_ROLE=load ./gbasic tests/gsql/readers_speed.bas >/dev/null 2>&1
    bad=0
    summary=""
    for n in 1 2 4 8; do
        rf=$(run_speed "$f" "" "$n")
        rs=$(run_speed "$s" 0 "$n")
        if printf '%s\n%s\n' "$rf" "$rs" | grep -q '^BAD'; then
            printf '%s\n%s\n' "$rf" "$rs" | grep '^BAD' | head -2
            bad=1
            continue
        fi
        read -r wf pf <<<"$(printf '%s\n' "$rf" | tail -1)"
        read -r ws ps <<<"$(printf '%s\n' "$rs" | tail -1)"
        ratio=$(awk -v a="$ws" -v b="$wf" 'BEGIN { printf "%.2f", a / b }')
        summary="$summary N=$n ${ratio}x (p99 ${pf}/${ps} ms);"
        if [ "$n" = 4 ]; then
            r4=$ratio
            p4f=$pf
            p4s=$ps
        fi
    done
    printf '       wall, sliced over forked, and a point reader'"'"'s p99 forked/sliced:%s\n' "$summary"
    if [ "$bad" = 1 ]; then
        fail "speed: a client's answer was wrong"
    elif ! awk -v r="$r4" 'BEGIN { exit !(r >= 1.5) }'; then
        fail "speed: 4 clients were ${r4}x faster forked, under the 1.5x required"
    elif ! awk -v f="$p4f" -v s="$p4s" 'BEGIN { exit !(f * 2 < s) }'; then
        fail "speed: beside 4 clients a point read's p99 was $p4f ms forked and $p4s ms sliced; under half wanted"
    else
        pass "speed: 4 clients ${r4}x faster forked (1.5x required); point-read p99 $p4f ms against $p4s ms"
    fi
fi

printf 'TIER leftovers: no owner and no reader outlives the suite\n'
# an owner stops by itself after its linger (3 s by default); a reader is the
# owner's child and shows the same command line
left=""
for _ in $(seq 1 100); do
    left=$(pgrep -f -- "--gsql-owner (--linger=[0-9]+ )?$work/" | tr '\n' ' ')
    [ -z "$left" ] && break
    sleep 0.1
done
if [ -z "$left" ]; then
    pass "no owner or reader process is left"
else
    fail "processes outlived their programs: $left"
fi

if [ "$status" = 0 ]; then
    printf 'run_gsql_readers: all checks passed\n'
else
    printf 'run_gsql_readers: FAILED\n'
fi
exit "$status"
