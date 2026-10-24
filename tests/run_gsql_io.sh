#!/bin/bash
# G-SQL's I/O LAYER AND CRASH HARNESS (include/gsql_io.h, include/gsql_crash.h;
# docs/gsql_storage_design.md §3.4 and §5.3) -- the second Phase 0a artefact,
# needed whichever storage candidate wins, because a kill -9 harness can never
# see a lost or torn write: the page cache outlives the process and writes
# everything out. A SIMULATED layer keeps a durable image plus pending
# operations and builds the disk a crash at any operation would leave.
#
# THE RISK IS A SIMULATION OF A DISK THAT DOES NOT EXIST, so the layer is held
# to the five POSIX rules in its header, EACH AS A DIFFERENCE -- an unsynced
# write must be LOST by some power loss AND KEPT by another, since a layer that
# always did one passes any one-sided check -- and the same seeded operation
# sequence is run through the REAL layer and the simulated one with every
# answer and every byte required to agree.
#
# THE HARNESS IS HELD TO A HARDER STANDARD THAN "IT FOUND NOTHING": a harness
# reporting zero violations is indistinguishable from one that checks nothing.
# So it is pointed at a correct toy engine AND at eight deliberately broken
# copies, each omitting one thing correctness rests on, and each must be caught
# BY THE CHECK WRITTEN FOR IT in the mode that can see it -- not merely caught,
# since a defect caught only by a neighbouring check says the check written
# for it is blind. The correct toy must clear the design's gate of 10,000
# injected crashes per mode with zero violations, and its own census must show
# the oracle's range was really exercised (unsynced commits both kept and
# dropped by some crash).
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

CC=${CC:-cc}
if ! command -v "$CC" >/dev/null 2>&1; then
    printf 'SKIP run_gsql_io (no C compiler -- the I/O layer and the crash harness were NOT measured)\n'
    exit 0
fi
srcs="src/gsql/io.c src/gsql/io_sim.c"
if ! "$CC" -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude -g -O1 \
        -o "$work/io_check" tests/gsql/io_check.c $srcs 2> "$work/cc.err" ||
   ! "$CC" -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude -g -O2 \
        -o "$work/crash_check" tests/gsql/crash_check.c src/gsql/crash.c $srcs 2>> "$work/cc.err"; then
    head -8 "$work/cc.err"
    printf 'FAIL run_gsql_io (the I/O layer, the harness or a checker did not compile cleanly)\n'
    exit 1
fi

# A hang is not a failure: every checker run is bounded.
bounded() { timeout -k 5 300 "$@"; }

printf 'TIER rules: the simulated layer against the five durability rules\n'
bounded "$work/io_check" rules > "$work/rules.out"
rc=$?
if [ "$rc" = "0" ]; then
    n=$(sed -n 's/^ok \([0-9]*\) checks/\1/p' "$work/rules.out")
    if [ "${n:-0}" -lt 45 ]; then fail "only ${n:-0} rule checks ran"; else pass "$n checks, each rule as a difference"; fi
else
    grep '^MISMATCH' "$work/rules.out" | head -8
    fail "the simulated layer broke its own contract (exit $rc)"
fi

printf 'TIER parity: the real layer and the simulated one agree, answer for answer\n'
for seed in 1 2 3 4; do
    dir="$work/parity$seed"
    mkdir -p "$dir"
    bounded "$work/io_check" parity "$dir" "$seed" 3000 > "$work/parity.out"
    rc=$?
    census=$(sed -n 's/^census //p' "$work/parity.out")
    errors=$(printf '%s' "$census" | sed -n 's/.*errors=\([0-9]*\).*/\1/p')
    bytes=$(printf '%s' "$census" | sed -n 's/.*bytes_read=\([0-9]*\).*/\1/p')
    if [ "$rc" != "0" ]; then
        grep '^MISMATCH' "$work/parity.out" | head -5
        fail "seed $seed: the two layers disagree"
    elif [ "${errors:-0}" -lt 50 ] || [ "${bytes:-0}" -lt 100000 ]; then
        # A sequence that never met an error, or never read a byte back, would
        # agree about nothing worth agreeing on.
        fail "seed $seed: the sequence exercised too little (errors=${errors:-0} bytes_read=${bytes:-0})"
    else
        pass "seed $seed: $(tail -1 "$work/parity.out"), $errors agreed errors, $bytes bytes read back"
    fi
done

# One run: VARIANT MODE SEEDS [IMAGES]. Prints the summary; leaves the
# violations in $work/run.out.
crash_run() {
    GSQL_CRASH_REPORTS=1000000 bounded "$work/crash_check" "$1" "$2" "$3" 16 1500 1 "" ${4:-} \
        > "$work/run.out" 2>&1
    tail -1 "$work/run.out"
}
field() { printf '%s' "$1" | sed -n "s/.*$2=\([0-9]*\).*/\1/p"; }

printf 'TIER gate: the correct toy, 10,000 crashes per mode, zero violations\n'
for spec in "process 1-120" "power 1-120" "torn 1-120" "failed-sync 1-1000" "full-disk 1-700"; do
    set -- $spec
    summary=$(crash_run ok "$1" "$2")
    crashes=$(field "$summary" crashes)
    violations=$(field "$summary" violations)
    if [ "${violations:-1}" != "0" ]; then
        grep '^VIOLATION' "$work/run.out" | head -5
        fail "$1: the correct toy was reported broken ($summary)"
    elif [ "${crashes:-0}" -lt 10000 ]; then
        fail "$1: only ${crashes:-0} crashes injected, short of the 10,000 the gate asks"
    else
        extra=""
        case "$1" in
        power|torn)
            # The oracle's range must be EXERCISED: some crash keeps a commit
            # that had not returned durable, and some drops one. A harness whose
            # crashes always landed at one edge would accept an engine that
            # never kept anything unsynced, or always did.
            kept=$(field "$summary" kept_unsynced)
            dropped=$(field "$summary" dropped_unsynced)
            durable=$(field "$summary" durable_checked)
            if [ "${kept:-0}" -lt 100 ] || [ "${dropped:-0}" -lt 100 ] || [ "${durable:-0}" -lt 1000 ]; then
                fail "$1: the oracle's range was not exercised ($summary)"
                continue
            fi
            extra=", unsynced commits kept by $kept and dropped by $dropped" ;;
        esac
        pass "$1: $crashes crashes, 0 violations$extra"
    fi
done

printf 'TIER controls: every broken copy is caught by the check written for it\n'
# variant | mode | seeds | the violation text that must appear
while IFS='|' read -r variant mode seeds want; do
    [ -n "$variant" ] || continue
    summary=$(crash_run "$variant" "$mode" "$seeds")
    violations=$(field "$summary" violations)
    if [ "${violations:-0}" = "0" ]; then
        fail "$variant: NOT CAUGHT in $mode mode ($summary)"
    elif ! grep -q -- "$want" "$work/run.out"; then
        printf '%s\n' "$(grep '^VIOLATION' "$work/run.out" | head -2)"
        fail "$variant: caught in $mode mode, but not by the check for it (no \"$want\")"
    else
        pass "$variant caught in $mode mode by: $want ($violations violations)"
    fi
done <<'EOF'
nosync|power|1-20|recovered the state after
nodirsync|power|1-20|recovered the state after 0 commits
nocrc|torn|1-40|matching NO prefix
notrunc|power|1-20|a durable commit made after recovery was lost
swallow|failed-sync|1-20|a failed flush was reported as success
carryon|failed-sync|1-20|did not refuse further work with FAILED
fullskip|full-disk|1-20|a commit made before or after the full disk was lost
rewrite|power|1-5|stage=recovery-op-
EOF
# And the stage that crashes the recovery is the ONLY one that sees rewrite:
# with it turned off, a recovery that is correct when uninterrupted passes.
GSQL_CRASH_REPORTS=1000000 bounded "$work/crash_check" rewrite power 1-5 16 1500 0 > "$work/run.out" 2>&1
v=$(field "$(tail -1 "$work/run.out")" violations)
if [ "${v:-1}" = "0" ]; then
    pass "rewrite is invisible without crashing the recovery, so that stage is what caught it"
else
    fail "rewrite was caught WITHOUT crashing the recovery ($v violations) -- the control does not isolate that stage"
fi

printf 'TIER valgrind: the layers and the harness\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
elif [ "$status" != "0" ]; then
    # `timeout` cannot wrap `vg_run` (a shell FUNCTION), so run only over code
    # the bounded tiers passed.
    printf '  SKIP (an earlier tier failed -- memory access was NOT measured)\n'
else
    mkdir -p "$work/vgpar"
    vg_failed=0
    for args in "io_check rules" "io_check parity $work/vgpar 9 600" \
                "crash_check ok torn 1-2 16 1500 1" "crash_check ok failed-sync 1-3 16 1500 1" \
                "crash_check ok full-disk 1-3 16 1500 1"; do
        set -- $args
        prog=$1; shift
        vg_run "$work/$prog" "$@" > /dev/null 2> "$work/vg.err"
        vrc=$?
        if [ "$vrc" = "$VG_EXIT" ]; then
            grep -E "Invalid|uninitialised|definitely" "$work/vg.err" | head -4
            fail "valgrind objected: $args"
            vg_failed=1
        elif [ "$vrc" != "0" ]; then
            fail "$args exited $vrc under valgrind"
            vg_failed=1
        fi
    done
    [ "$vg_failed" = "0" ] && pass "no definite leak or invalid access across five runs"
fi

if [ "$status" = "0" ]; then
    printf 'run_gsql_io: all checks passed\n'
else
    printf 'run_gsql_io: FAILED\n'
fi
exit "$status"
