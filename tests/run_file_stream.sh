#!/usr/bin/env bash
# STREAMING A FILE BY LINES -- `for each line in f` (docs/etl_files_design.md §2).
#
# THE DEFECT IT CLOSES IS A WALL, NOT A NUISANCE. Every file read in gBASIC was
# whole-file, and MEASURED that costs ~4.5x the file size: 200,000 lines of a
# 7.6 MB CSV took 48.7 MB resident against a 13.8 MB baseline, 179 bytes a line.
# A 1 GB extract needed ~4.5 GB and a 10 GB one was impossible rather than slow
# -- in the one area the next book is about.
#
# NO GRAMMAR CHANGED. `for each line in f` already PARSED and raised at ONE
# runtime site, so no working program contained the shape and nothing could
# change meaning -- the proof the array-projection work rested on, and the reason
# this cost zero bison conflicts.
#
# FOUR TIERS AND NONE OF THEM IS A GOLDEN, because every defect here is either a
# plausible number of lines or an invisible leak:
#
#   AGREEMENT  -- the ORACLE. Streaming and `read_lines` are two independent
#                 implementations of one rule (split on \n, drop one trailing
#                 \r, a last line without a newline is still a line), so their
#                 agreeing over twelve shapes is evidence where a golden of
#                 either alone is a transcript. Includes an interior NUL and a
#                 200,000-byte single line, which is the buffer-growth path.
#   EXITS      -- the handle is the loop's, so it must close on ALL SIX ways out:
#                 normal end, `break`, a `break` NAMING AN OUTER LOOP, a
#                 `return` from inside a function, a raise escaping a frame, and
#                 two streams open at once. Run under valgrind with --track-fds,
#                 because a leaked descriptor is the failure a long ETL run
#                 reaches and no output reveals.
#   REFUSALS   -- a read error RAISES. An absent file and a directory must both
#                 raise rather than report zero lines: a short read that looks
#                 like end-of-file is the file-reading form of "a directory that
#                 cannot be read is not an empty one".
#   MEMORY     -- the headline claim, asserted as a RATIO across a 10x file-size
#                 step rather than an absolute, since an absolute is a fact about
#                 this machine. WITH A CONTROL: `read_lines` over the SAME files
#                 must EXCEED the gate, or "streaming is flat" is being asserted
#                 by a harness that cannot tell flat from proportional.
. "$(dirname "$0")/portable.sh"
. "$(dirname "$0")/valgrind_tier.sh"   # the ONE valgrind policy every suite shares
set -uo pipefail
cd "$(dirname "$0")/.."

make >/dev/null
status=0
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

run_selfchecking() {   # <fixture> <floor> <label> [args...]
    local f="$1" floor="$2" label="$3"; shift 3
    local out bad ok
    out="$({ GBASIC_PATH=stdlib timeout -k 5 300 ./gbasic "$f" "$@" 2>&1 </dev/null || true; })"
    bad="$(printf '%s\n' "$out" | grep -cE '^(MISMATCH|.*NO RAISE|.*NOT RAISED)' || true)"
    ok="$(printf '%s\n' "$out" | grep -c '^ok  ' || true)"
    if [ "${bad:-0}" -ne 0 ]; then
        printf '%s\n' "$out" | grep -E '^(MISMATCH|.*NO RAISE|.*NOT RAISED)' | sed 's/^/  /'
        printf 'FAIL %s\n' "$label"; status=1; return
    fi
    if [ "${ok:-0}" -lt "$floor" ]; then
        printf 'FAIL %s (only %s checks ran; at least %s expected)\n' "$label" "$ok" "$floor"
        printf '%s\n' "$out" | tail -3 | sed 's/^/  /'
        status=1; return
    fi
    printf 'PASS %s (%s checks)\n' "$label" "$ok"
}

echo "--- TIER 1: AGREEMENT -- streaming and read_lines are one rule, twice ---"
run_selfchecking tests/file_stream/agreement.bas 12 "agreement over 12 shapes" "$work/a.txt"

echo "--- TIER 2: EXITS -- the loop owns the handle, on every way out ---"
out="$({ GBASIC_PATH=stdlib ./gbasic tests/file_stream/exits.bas "$work/e.txt" 2>&1 </dev/null || true; })"
for want in "normal end" "break " "break outer" "return " "raise escapes" "nested streams"; do
    printf '%s' "$out" | grep -q "$want" \
        && printf 'ok   exit path: %s\n' "$want" \
        || { printf 'FAIL exit path missing: %s\n' "$want"; status=1; }
done
printf '%s' "$out" | grep -q "NOT RAISED" && { printf 'FAIL the escaping raise did not raise\n'; status=1; }
# THROUGH THE SHARED POLICY, not a hand-typed invocation: run_valgrind_policy.sh
# tripwires a suite that types its own flags, and it caught this suite doing
# exactly that on its first run. `vg_run` is the ordinary tier and ALREADY
# carries --track-fds, which is the axis this tier needs -- a leaked descriptor
# is the failure a long ETL run reaches and no output reveals.
if vg_available; then
    if GBASIC_PATH=stdlib vg_run ./gbasic tests/file_stream/exits.bas \
         "$work/v.txt" >/dev/null 2>"$work/vg.txt"; then
        printf 'ok   no leaked memory and no leaked descriptor across all six exits\n'
    else
        printf 'FAIL valgrind over the exit paths\n'; sed 's/^/  /' "$work/vg.txt" | head -12; status=1
    fi
else
    printf 'SKIP valgrind (unavailable) -- NOTHING was checked about leaked descriptors,\n'
    printf '     which is the failure mode this tier exists for\n'
fi

echo "--- TIER 3: REFUSALS -- a read error raises, it does not end the loop ---"
run_selfchecking tests/file_stream/refusals.bas 2 "refusals (absent file, mid-stream read error)"

echo "--- TIER 4: MEMORY -- flat in the file size, with read_lines as the control ---"
peak() {   # <fixture> <file> -> peak RSS in kB
    /usr/bin/time -v env GBASIC_PATH=stdlib ./gbasic "$1" "$2" 2>&1 >/dev/null \
        | awk '/Maximum resident/{print $NF}'
}
gen() { awk -v n="$1" 'BEGIN{for(i=0;i<n;i++) printf "%d,customer-%d,2026-01-01,%d.%02d\n", i, i, i%997, i%100}' > "$2"; }
if ! command -v /usr/bin/time >/dev/null 2>&1; then
    printf 'SKIP memory (/usr/bin/time absent) -- the headline claim was NOT measured\n'
else
    gen 50000  "$work/small.csv"
    gen 500000 "$work/large.csv"
    s_small="$(peak tests/file_stream/count.bas "$work/small.csv")"
    s_large="$(peak tests/file_stream/count.bas "$work/large.csv")"
    w_small="$(peak tests/file_stream/whole.bas "$work/small.csv")"
    w_large="$(peak tests/file_stream/whole.bas "$work/large.csv")"
    printf '     10x more input: streaming %s -> %s kB, read_lines %s -> %s kB\n' \
        "$s_small" "$s_large" "$w_small" "$w_large"
    # Streaming must stay within 1.5x across a 10x step. The gate is loose on
    # purpose: what it has to separate is FLAT from PROPORTIONAL, and the control
    # below is what proves it can.
    if awk -v a="$s_small" -v b="$s_large" 'BEGIN{exit !(b < a*1.5)}'; then
        printf 'ok   streaming is flat (%s -> %s kB across 10x the input)\n' "$s_small" "$s_large"
    else
        printf 'FAIL streaming grew with the file: %s -> %s kB\n' "$s_small" "$s_large"; status=1
    fi
    if awk -v a="$w_small" -v b="$w_large" 'BEGIN{exit !(b > a*1.5)}'; then
        printf 'ok   CONTROL: read_lines over the same files exceeds the gate (%s -> %s kB)\n' \
            "$w_small" "$w_large"
    else
        printf 'FAIL the CONTROL did not exceed the gate -- this tier cannot tell flat\n'
        printf '     from proportional and is measuring nothing (%s -> %s kB)\n' "$w_small" "$w_large"
        status=1
    fi
fi

[ "$status" = 0 ] && printf 'PASS tests/run_file_stream.sh\n' || printf 'FAIL tests/run_file_stream.sh\n'
exit "$status"
