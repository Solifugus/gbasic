#!/usr/bin/env bash
# Run every suite in tests/, discovered by GLOB rather than by a list, MOST OF
# THEM AT ONCE.
#
# The list is the point. Three suites (run_ari, run_nap_fs, run_render) sat
# broken from 0.1.0-rc6 to rc7 because the brace-modifier migration drove off
# `.bas` files and those three embed gBASIC in a shell heredoc -- and nothing
# noticed, because every gate anyone ran was a hand-maintained list that did not
# happen to name them. A gate you have to remember to extend is a gate that
# silently shrinks; docs/project_state.md's hand-maintained document index
# rotted the same way and for the same reason.
#
# So: no list. A new tests/run_*.sh is in the gate the moment it exists.
#
# ---------------------------------------------------------------------------
# WHY THIS RUNS IN PARALLEL, AND WHY IT USED NOT TO
#
# It ran ONE AT A TIME for a concrete reason, recorded here before this change
# and still true: every suite calls `make`, a few call `make clean && make`, and
# two gates -- or one gate beside a single suite somebody is iterating on --
# relink `./gbasic` underneath each other. The failures that produces look
# exactly like real regressions and are not: observed 2026-08-25 as
# `timeout: failed to execute process: No such file or directory` in run_stridx
# and `./gbasic: Permission denied` in run_examples, both from the binary being
# caught mid-link. Neither reproduced when the gate ran alone.
#
# THAT IS AN ARGUMENT ABOUT RELINKING, NOT ABOUT CONCURRENCY. This script
# already builds once up front, so for all but a handful of suites `make` is a
# NO-OP that relinks nothing. The handful that genuinely rebuild are the whole
# of the hazard, and they are identified by what they DO rather than by a list
# (see LANES below).
#
# MEASURED 2026-10-18 on this host: 22 cores, load average 1.9, and a serial
# gate of 185 suites takes ~50 minutes at ~17s a suite. The coverage argument
# above says nothing against running the same suites faster -- what it forbids
# is CHOOSING suites by judgment, which this does not do. Every suite still
# runs, still by glob.
#
# ---------------------------------------------------------------------------
# LANES, and the measurement that decides them
#
# A suite declares its own lane with a `# GATE-LANE:` line, so the knowledge
# lives beside the thing it describes rather than in a table here that a new
# suite silently stays out of -- the arrangement the `.nolineno` sidecars use
# and for the reason given there. A reason is REQUIRED after `--`, so the set
# cannot grow without one. No marker means `parallel`.
#
#   exclusive   the suite REBUILDS the tree (`make clean`, or `make` with the
#               optional-dependency switches turned off). Run last, alone, and
#               the tree is rebuilt afterwards -- they are the mid-link hazard,
#               entirely.
#
#   serial      the suite asserts an ABSOLUTE wall-clock bound. This is the
#               narrow set, and the reason it is narrow is worth stating: A
#               RATIO IS SELF-NORMALISING. A shape tier that compares a 4x
#               input against a baseline measures both halves under whatever
#               load the machine is carrying, so the ratio is unchanged when
#               everything is uniformly slower -- which is why run_arridx,
#               run_recidx, run_stridx's shape tiers, run_http's concurrency
#               ratio and run_compile's 70x all run in the parallel lane. An
#               ABSOLUTE bound has no such defence: run_xlsx's 1200 ms
#               ceiling, run_persist's 5 s and run_core's wall-clock `sleep`
#               are facts about how fast the machine happened to be.
#
#               This is also the lane for a suite driving a pty or a timeout
#               budget, where being descheduled reads as the thing under test
#               misbehaving.
#
# CLAUDE.md already records where that goes wrong: run_ari_discover took 35
# minutes AND PASSED at load average 45, five past the then 1800s cap, so a
# default invocation reported a suite that had not failed -- and "a gate that
# goes red because a colleague is building is a gate people learn to turn off,
# which is the failure this file exists to prevent". Saturating 22 cores is
# self-inflicted load average 45, so the serial lane runs FIRST, on an idle
# machine, before any worker starts.
#
# Usage:
#   tests/run_all.sh                 # every suite
#   tests/run_all.sh web             # only suites whose name contains "web"
#   GATE_JOBS=1 tests/run_all.sh     # the old serial behaviour, exactly
#   GATE_JOBS=8 tests/run_all.sh     # a different width
#   SUITE_TIMEOUT=1200 tests/run_all.sh
#   RUN_MANUAL=1 tests/run_all.sh    # include the manual/very-slow tiers
#
# Exit 0 only if every suite that ran passed. A suite that SKIPPED is reported
# as such and does NOT count as a pass -- a green line that ran nothing is the
# other way a gate silently shrinks.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -uo pipefail
cd "$(dirname "$0")/.."

filter="${1:-}"
# THE PER-SUITE CAP IS A HANG-GUARD AND ITS OLD 1800 WAS AN IDLE-MACHINE
# NUMBER. This host runs several gBASIC sessions at once; measured 2026-09-23 at
# load average 45, run_ari_discover takes 35 minutes and passes -- five minutes
# past the old cap, so a DEFAULT invocation of this gate reported a suite that
# had not failed. 3600 still bounds a genuinely hung suite; override for a
# slower host.
timeout_s="${SUITE_TIMEOUT:-3600}"

# HOW WIDE. Half the cores rather than all of them, deliberately: the point is
# to stop wasting 20 idle cores, not to drive the load average to the number
# that already made this gate lie once. Capped at 12 because the win is
# sublinear past that (most suites are seconds long and several are I/O bound)
# while the load is not.
default_jobs=$(( $(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2) / 2 ))
[ "$default_jobs" -lt 1 ] && default_jobs=1
[ "$default_jobs" -gt 12 ] && default_jobs=12
jobs_n="${GATE_JOBS:-$default_jobs}"
# `wait -n` ARRIVED IN BASH 4.3. Without it the call fails immediately rather
# than waiting, so a pool built on it would launch every suite at once -- the
# saturated machine this lane split exists to prevent. Asked of the shell
# rather than its version string, and a shell without it runs the gate SERIALLY
# and says so, which is slow and correct rather than fast and wrong.
if ! ( sleep 0 & wait -n ) >/dev/null 2>&1; then
    printf 'note: this bash has no `wait -n`, so the gate runs serially\n'
    jobs_n=1
fi
case "$jobs_n" in
    ''|*[!0-9]*) printf 'GATE_JOBS must be a number, got %s\n' "$jobs_n" >&2; exit 1 ;;
esac
[ "$jobs_n" -lt 1 ] && jobs_n=1

if [ -n "${RUN_ALL_LOGDIR:-}" ]; then
    logdir="$RUN_ALL_LOGDIR"
    mkdir -p "$logdir" || { printf 'cannot create RUN_ALL_LOGDIR %s\n' "$logdir" >&2; exit 1; }
    logdir_ours=0
else
    logdir="$(mktemp -d)"
    logdir_ours=1
fi

# Excluded unless RUN_MANUAL=1, and NAMED when excluded rather than quietly
# dropped: run_xml_bigfile generates a >=100 MB document and is documented as a
# manual tier. Anything added here must be announced the same way.
manual=(run_xml_bigfile.sh run_insight_calibration.sh)

is_manual() {
    local s
    for s in "${manual[@]}"; do [ "$1" = "$s" ] && return 0; done
    return 1
}

# A SUITE'S OWN DECLARATION. Anything but `serial` or `exclusive` -- including a
# marker with no reason -- is reported and treated as parallel, because a
# malformed marker must not quietly put a suite in a lane nobody intended.
lane_of() {
    local line
    line="$(grep -m1 -E '^# GATE-LANE:' "$1" 2>/dev/null)" || true
    [ -z "$line" ] && { printf 'parallel'; return; }
    case "$line" in
        *"GATE-LANE: exclusive --"*) printf 'exclusive' ;;
        *"GATE-LANE: serial --"*)    printf 'serial' ;;
        *) printf 'malformed' ;;
    esac
}

if ! make >/dev/null; then
    printf 'FAIL: build failed\n'
    exit 1
fi

# ---------------------------------------------------------------- partition
par=(); ser=(); exc=()
excluded=0
malformed=()
for path in tests/run_*.sh; do
    name="$(basename "$path")"
    [ "$name" = "run_all.sh" ] && continue
    if [ -n "$filter" ] && [[ "$name" != *"$filter"* ]]; then
        continue
    fi
    if is_manual "$name" && [ "${RUN_MANUAL:-0}" != "1" ]; then
        printf '%-32s MANUAL (excluded; RUN_MANUAL=1 to include)\n' "${name%.sh}"
        excluded=$((excluded + 1))
        continue
    fi
    case "$(lane_of "$path")" in
        exclusive) exc+=("$name") ;;
        serial)    ser+=("$name") ;;
        malformed) malformed+=("$name"); par+=("$name") ;;
        *)         par+=("$name") ;;
    esac
done

# THE TRIPWIRE THAT KEEPS THE LANES FROM ROTTING, and it is DERIVED rather than
# listed: a suite containing `make clean` relinks the binary, which is the
# entire hazard this script was serial for, so it MUST declare itself exclusive.
# A new suite that adds `make clean` and no marker fails here rather than
# producing the mid-link failures recorded above, which look like regressions
# and are not.
lane_rot=0
for path in tests/run_*.sh; do
    name="$(basename "$path")"
    [ "$name" = "run_all.sh" ] && continue
    # ONE HOP THROUGH AN EXECUTED TOOL, because the literal scan missed the
    # worst offender: run_packaging says `packaging/build-deb.sh`, and the
    # `make clean` plus the LEAN rebuild are inside that script. Measured
    # 2026-10-18 -- with it in the parallel lane, ten suites that pass serially
    # failed, reporting `./gbasic: No such file or directory`, `timeout:
    # failed to execute process: Permission denied` and a concurrent
    # `src/parser.tab.o` compile: the 2026-08-25 mid-link failures verbatim.
    builds="$(sed 's/#.*//' "$path" 2>/dev/null \
                | grep -oE '(\./)?(packaging|tools)/[A-Za-z0-9_.-]+\.sh' \
                | sed 's|^\./||' | sort -u)"
    rebuilds=0
    sed 's/#.*//' "$path" 2>/dev/null \
      | grep -qE '(^|&&|;|\|\||\()[[:space:]]*make([[:space:]]+-[A-Za-z]+[^;&|]*)?[[:space:]]+clean' \
      && rebuilds=1
    for tool in $builds; do
        [ -f "$tool" ] || continue
        sed 's/#.*//' "$tool" 2>/dev/null \
          | grep -qE '(^|&&|;|\|\||\()[[:space:]]*make([[:space:]]+-C[[:space:]]+[^[:space:]]+)?[[:space:]]+(clean|[A-Z][A-Z0-9_]*=)' \
          && rebuilds=1
    done
    if [ "$rebuilds" = "1" ] \
       && ! grep -qE "^# *GATE-LANE: *exclusive" "$path" 2>/dev/null; then
        printf 'FAIL lane: %s rebuilds the tree (its own `make clean`, or a tool it runs) and does not declare `# GATE-LANE: exclusive --`\n' "$name"
        lane_rot=1
    fi
done
# And the other direction, so the set cannot quietly empty: the mid-link
# hazard is real and at least one suite has always carried it.
if [ "${#exc[@]}" -eq 0 ] && [ -z "$filter" ]; then
    printf 'FAIL lane: no suite declares itself exclusive -- the scan stopped matching, it did not pass\n'
    lane_rot=1
fi
for m in "${malformed[@]+"${malformed[@]}"}"; do
    printf 'FAIL lane: %s has a GATE-LANE marker with no reason after `--`\n' "$m"
    lane_rot=1
done

pass=0; fail=0; skip=0
failed_names=()
skipped_names=()

# ------------------------------------------------------------ one suite's verdict
# ASKED OF THE OUTPUT RATHER THAN THE EXIT STATUS, unchanged from the serial
# version and for the reason recorded there: run_core.sh printed `FAIL sleep(0)
# returns 0` and exited 0, so 24 checks had been advisory and this gate printed
# OK over a failing suite for NINE DAYS. A suite that says FAIL has failed,
# whatever its exit code claims.
#
# WRITES A VERDICT FILE rather than printing, because in the parallel lane the
# parent is not the process that ran the suite. One line: verdict, then detail.
# The binary's identity, cheap enough to take per suite. `cksum` rather than a
# hash command, being in coreutils everywhere this gate runs.
bin_sig() { cksum gbasic 2>/dev/null || printf 'absent'; }

run_one() {
    local name="$1" path="tests/$1" log="$logdir/$1.log" res="$logdir/$1.res"
    local rc before after
    before=""
    [ "${guard_binary:-0}" = "1" ] && before="$(bin_sig)"
    timeout "$timeout_s" bash "$path" >"$log" 2>&1
    rc=$?
    if [ -n "$before" ]; then
        after="$(bin_sig)"
        if [ "$before" != "$after" ]; then
            # LOUD RATHER THAN FLAKY, and deliberately reported by every suite
            # that saw it: the culprit is one of them, and a victim's own
            # failure is otherwise indistinguishable from a real regression.
            printf 'FAIL\tthe interpreter was REBUILT while this suite ran\ta suite in the parallel lane rebuilds the tree and must declare `# GATE-LANE: exclusive --`\n' >"$res"
            return
        fi
    fi
    if [ "$rc" -eq 0 ]; then
        if grep -qE '^ *FAIL( |$)' "$log"; then
            printf 'FAIL\tsaid FAIL, exited 0\t%s\n' \
                "$(grep -m1 -E '^ *FAIL( |$)' "$log" | cut -c1-70)" >"$res"
        elif grep -q '^SKIP' "$log" && ! grep -q '^PASS\|^OK\|passed' "$log"; then
            printf 'SKIP\t%s\t\n' "$(grep -m1 '^SKIP' "$log" | cut -c1-70)" >"$res"
        else
            printf 'OK\t\t\n' >"$res"
        fi
    elif [ "$rc" -eq 124 ]; then
        printf 'FAIL\tTIMEOUT after %ss\t\n' "$timeout_s" >"$res"
    else
        printf 'FAIL\texit %d\t\n' "$rc" >"$res"
    fi
}

# Reads a verdict file and tallies it. Runs in the PARENT only, so the counters
# are the parent's -- a subshell's increments would be lost, which is the
# ordinary way a parallel bash tally reports zero failures.
# `named` says the line has already been started by the caller -- the serial
# lanes print the name BEFORE running, so the suite being waited for is on
# screen while it runs.
report_one() {
    local name="$1" res="$logdir/$1.res" named="${2:-0}"
    local verdict detail extra
    [ "$named" = "1" ] || printf '%-32s ' "${name%.sh}"
    if [ ! -f "$res" ]; then
        printf 'FAIL (no verdict -- the worker died) -> %s\n' "$logdir/$name.log"
        fail=$((fail + 1)); failed_names+=("$name"); return
    fi
    IFS=$'\t' read -r verdict detail extra < "$res"
    case "$verdict" in
        OK)   printf 'OK\n'; pass=$((pass + 1)) ;;
        SKIP) printf 'SKIP  %s\n' "$detail"; skip=$((skip + 1)); skipped_names+=("$name") ;;
        *)    printf 'FAIL (%s) -> %s\n' "$detail" "$logdir/$name.log"
              [ -n "${extra:-}" ] && printf '%34s%s\n' '' "$extra"
              fail=$((fail + 1)); failed_names+=("$name") ;;
    esac
}

# ------------------------------------------------------- lane 1: serial, idle
# FIRST, BEFORE ANY WORKER STARTS, because these are the suites whose
# assertions are facts about how fast the machine is.
for name in "${ser[@]+"${ser[@]}"}"; do
    printf '%-32s ' "${name%.sh}"
    run_one "$name"
    report_one "$name" 1
done

# ---------------------------------------------------- lane 2: the parallel one
# A FIXED-WIDTH POOL over `wait -n`. Verdicts are reported in GLOB ORDER rather
# than completion order, so two runs of this gate produce the same lines in the
# same order and a parallel run can be DIFFED against a serial one -- which is
# the evidence that this change altered nothing but the wall clock.
if [ "${#par[@]}" -gt 0 ]; then
    if [ "$jobs_n" -gt 1 ]; then
        printf -- '-- %d suites across %d workers --\n' "${#par[@]}" "$jobs_n"
    fi
    # NEXT TO REPORT, in glob order. A suite that finishes early waits for its
    # predecessor's line, which costs nothing and buys output two runs of this
    # gate agree on.
    next_i=0
    drain() {
        while [ "$next_i" -lt "${#par[@]}" ] \
              && [ -f "$logdir/${par[$next_i]}.res" ]; do
            report_one "${par[$next_i]}"
            next_i=$((next_i + 1))
        done
    }
    guard_binary=1
    running=0
    for name in "${par[@]}"; do
        if [ "$jobs_n" -gt 1 ]; then
            run_one "$name" &
            running=$((running + 1))
            if [ "$running" -ge "$jobs_n" ]; then
                # `|| true` IS LOAD-BEARING: `wait -n` returns the finished
                # job's exit status, and a suite that fails exits nonzero, so
                # anything that treated that as an error here would serialise
                # the rest of the gate -- silently, and only in the runs where
                # something is already wrong. The verdict is read from the
                # suite's result file, never from this status.
                wait -n || true
                running=$((running - 1))
                drain
            fi
        else
            run_one "$name"
            drain
        fi
    done
    wait
    drain
    guard_binary=0
fi

# --------------------------------------------------- lane 3: exclusive, last
# LAST AND ALONE. These relink the binary, and one of them (run_platform)
# deliberately builds with the optional dependencies switched OFF -- so
# anything running beside them sees a binary that is not the one under test, or
# no binary at all. Running them last means whatever they leave behind disturbs
# nothing, and the tree is restored afterwards for the next caller.
for name in "${exc[@]+"${exc[@]}"}"; do
    printf '%-32s ' "${name%.sh}"
    run_one "$name"
    report_one "$name" 1
done
if [ "${#exc[@]}" -gt 0 ]; then
    make >/dev/null 2>&1 || true
fi

# NOTHING IN THIS TREE MAKES A LIVE PAID CALL WITHOUT BEING ASKED.
#
# Two fixtures exist whose whole job is to call a real model and overwrite the
# recordings every other check replays: tests/ari_advisor/record.bas and
# tests/nlq/record.bas. Both said so in their headers and NEITHER ENFORCED IT,
# and on 2026-10-02 an instrumentation sweep that ran every `.bas` in the tree
# ran one of them -- a live paid call nobody asked for, and a committed
# recording overwritten (restored from git). The fixture's own comment was the
# only thing between a glob and somebody's API bill.
#
# CHECKED HERE because the property belongs to the whole gate rather than to
# either library's suite, and this is the one thing that sees every file. A
# static scan, deliberately: the behavioural check would be to RUN them, which
# is the act being prevented.
#
# FOUND BY WHAT IT DOES, NOT BY ITS NAME. The first draft globbed
# `tests/record*.bas` and reported tests/record_nul_test.bas, which is about
# RECORD VALUES -- a tripwire whose false positives look exactly like its true
# ones is not a tripwire, which is the lesson finio's amount-kind check already
# produced. Measured instead: `load llm` WITH `load webclient` is carried by
# exactly the two recorders and by none of the eight fixtures that REPLAY
# through `with_transport`, because a replayer's transport reads a file and only
# a recorder needs a real HTTP client. A future fixture that legitimately wants
# both will fail this and somebody will decide, which is the right direction for
# a check about spending money.
#
# BOTH DIRECTIONS, so the rule cannot rot in either: every recorder must carry
# the switch, AND at least two must exist -- a scan that matches nothing
# reports a clean run, which is how this gate goes quiet without going red.
rec_total=0
rec_bad=0
for rec in $(grep -rl 'load webclient' tests/*.bas tests/*/*.bas 2>/dev/null | xargs -r grep -l 'load llm' 2>/dev/null); do
    [ -f "$rec" ] || continue
    rec_total=$((rec_total + 1))
    grep -q 'GBASIC_RECORD_FIXTURES' "$rec" || {
        printf 'FAIL %-27s makes live model calls with no GBASIC_RECORD_FIXTURES guard\n' "$rec"
        rec_bad=1
    }
done
if [ "$rec_total" -lt 2 ]; then
    printf 'FAIL %-32s only %d recorder(s) found -- the scan stopped matching, it did not pass\n' 'record guard' "$rec_total"
    fail=$((fail + 1))
    failed_names+=("record-guard")
elif [ "$rec_bad" != "0" ]; then
    fail=$((fail + 1))
    failed_names+=("record-guard")
else
    printf '%-32s OK (%d recorders, each opt-in)\n' 'record guard' "$rec_total"
fi

if [ "$lane_rot" != "0" ]; then
    fail=$((fail + 1))
    failed_names+=("gate-lanes")
fi

printf '\n%d passed, %d failed, %d skipped entirely' "$pass" "$fail" "$skip"
[ "$excluded" -gt 0 ] && printf ', %d manual excluded' "$excluded"
printf '\n'

if [ "$skip" -gt 0 ]; then
    printf 'skipped entirely: %s\n' "${skipped_names[*]}"
fi
if [ "$fail" -gt 0 ]; then
    printf 'failed: %s\n' "${failed_names[*]}"
    printf 'logs in %s\n' "$logdir"
    exit 1
fi

# Only if we made it -- see the note where $logdir is chosen.
[ "$logdir_ours" = "1" ] && rm -rf "$logdir"
exit 0
