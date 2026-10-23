#!/bin/bash
# A COLUMN'S ALIGNMENT -- `align:` on a table's or tree's `field`, and
# `qt.alignment`, which asks Qt where a cell's text sits.
#
# ASKED FOR BY gBASIC STUDIO: Matthew wants numbers right-aligned in Studio's
# grid, an asking table, and the model answered only Qt's DisplayRole, so a
# column's kind was gone before Qt saw it and nothing could align it.
#
# AND A DEFECT FOUND ON THE WAY: a table or tree column's options were read
# for `label`, `type` and `depth` and NOTHING ELSE, so any other option did
# nothing in silence -- measured, `alignn: "right"` and `bogus: 1` opened
# without a word. The column's options are a closed set now, checked by the
# shared load-time pass (src/window_check.c), so a declared window and a
# built one refuse identically, with the near miss.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/build_has.sh
source tests/valgrind_tier.sh
make >/dev/null

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# THE DECLARATION TIER NEEDS NO Qt: the column rule is the load-time pass, so
# it runs on every build, including one without Qt, where the window is
# declared and never opened.
printf 'TIER declaration: a misspelled column option fails at load\n'
cat > "$work/bad.bas" <<'BAS'
window w( title: "t" )
    table grid to show rows
        field n( label: "N", alignn: "right" )
    end table
end window
program main()
    print "RAN"
end program
BAS
out=$(./gbasic "$work/bad.bas" 2>&1); rc=$?
want="window: a table's column takes \`label\`, \`type\`, \`depth\` and \`align\`, and 'alignn' is none of them -- did you mean 'align'?"
if [ "$rc" = "0" ]; then
    fail "a misspelled column option was accepted"
elif printf '%s' "$out" | grep -q '^RAN'; then
    fail "the program ran despite the refusal"
elif printf '%s' "$out" | grep -qF "$want"; then
    pass "refused at load, by name, with the near miss, and nothing ran"
else
    printf '     %s\n' "$out" | head -2
    fail "refused, but not with the expected sentence"
fi
cat > "$work/good.bas" <<'BAS'
window w( title: "t" )
    table grid to show rows
        field n( label: "N", type: "number", align: "right" )
    end table
end window
program main()
    print "RAN"
end program
BAS
out=$(./gbasic "$work/good.bas" 2>&1)
if printf '%s' "$out" | grep -q '^RAN'; then
    pass "and the four known options are accepted (the control)"
else
    printf '     %s\n' "$out" | head -2
    fail "a column with only known options was refused"
fi

if ! build_has qt; then
    printf 'SKIP run_qt_align semantics (no Qt in this build -- alignment as Qt paints it was NOT measured)\n'
    [ "$status" = "0" ] && printf 'run_qt_align: all checks passed\n' || printf 'run_qt_align: FAILED\n'
    exit "$status"
fi

printf 'TIER semantics: asking, held and tree tables, refresh and refusals\n'
QT_QPA_PLATFORM=offscreen timeout -k 5 60 \
    ./gbasic --line-buffered tests/qt_align_test.bas \
    > "$work/sem.out" 2> "$work/sem.err"
rc=$?
if [ "$rc" = "124" ] || [ "$rc" = "137" ]; then
    fail "the fixture HUNG"
elif grep -q NODISPLAY "$work/sem.out"; then
    printf '  SKIP (no display and no offscreen platform)\n'
elif [ "$rc" != "0" ]; then
    tail -3 "$work/sem.out"; printf '     %s\n' "$(head -2 "$work/sem.err")"
    fail "the fixture exited $rc"
else
    bad=$(grep -c '^MISMATCH' "$work/sem.out" || true)
    checks=$(sed -n 's/^ok  *\([0-9]*\) checks.*/\1/p' "$work/sem.out")
    if [ "${bad:-1}" != "0" ]; then
        grep '^MISMATCH' "$work/sem.out" | head -5
        fail "$bad mismatches"
    elif [ "${checks:-0}" -lt 13 ]; then
        fail "only ${checks:-0} checks ran -- the fixture stopped early"
    else
        pass "$checks checks, 0 mismatches"
    fi
    grep -v 'This plugin does not support' "$work/sem.err" > "$work/sem.err2"
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing on stderr"
    fi
fi

printf 'TIER valgrind: no invalid access across the three table kinds\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen \
        vg_run_access_only ./gbasic tests/qt_align_test.bas \
        >/dev/null 2>"$work/vg.err" &
    vg_pid=$!
    vg_killed=0
    vg_deadline=$((SECONDS + 420))
    while kill -0 "$vg_pid" 2>/dev/null; do
        if [ "$SECONDS" -ge "$vg_deadline" ]; then
            for vg_kid in $(pgrep -P "$vg_pid" 2>/dev/null); do
                kill -9 "$vg_kid" 2>/dev/null
            done
            kill -9 "$vg_pid" 2>/dev/null
            pkill -KILL -f "gbasic tests/qt_align_test.bas" 2>/dev/null
            vg_killed=1
            break
        fi
        sleep 1
    done
    wait "$vg_pid"
    vg_rc=$?
    if [ "$vg_killed" = "1" ]; then
        fail "the fixture hung under valgrind, so memory access was NOT measured"
    elif [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    else
        pass "no invalid access across the alignments and teardown"
    fi
    if pgrep -f "gbasic tests/qt_align_test.bas" >/dev/null 2>&1; then
        pkill -KILL -f "gbasic tests/qt_align_test.bas" 2>/dev/null
        fail "the valgrind tier left a gbasic process behind"
    else
        pass "and nothing was left running afterwards"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_align: all checks passed\n'
else
    printf 'run_qt_align: FAILED\n'
fi
exit "$status"
