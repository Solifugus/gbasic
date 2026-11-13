#!/bin/bash
# STUDIO'S FIND-AND-REPLACE REQUESTS (docs/qt_gui_design.md §0.36): `qt.label`
# (a part's caption, and a `choice` in a form that showed none), `whitespace`
# on a source or memo, `qt.select_text`/`qt.selection`, `qt.replace_text` as
# one undo step, `qt.highlight_text`/`qt.highlights`, and `qt.click`, the left
# button aimed as `qt.right_click` aims.
#
# EVERY ORACLE IS QT: a caption is the QLabel whose buddy is the part, the
# whitespace setting is the document's text option, a selection and the marks
# are read back from the cursor and the extra selections, an undo is Qt's own
# slot and a click is a mouse press Qt turns into `itemClicked` itself.
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

if ! build_has qt; then
    printf 'SKIP run_qt_find semantics (no Qt in this build -- captions, editing and clicks were NOT measured)\n'
    [ "$status" = "0" ] && printf 'run_qt_find: all checks passed\n' || printf 'run_qt_find: FAILED\n'
    exit "$status"
fi

printf 'TIER semantics: captions, whitespace, selection, edits, marks, clicks\n'
QT_QPA_PLATFORM=offscreen timeout -k 5 60 \
    ./gbasic --line-buffered tests/qt_find_test.bas \
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
    elif [ "${checks:-0}" -lt 55 ]; then
        fail "only ${checks:-0} checks ran -- the fixture stopped early"
    else
        pass "$checks checks, 0 mismatches"
    fi
    grep -v -e 'This plugin does not support' -e 'built without KSyntaxHighlighting, so it will work as a plain editor' "$work/sem.err" > "$work/sem.err2"
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing on stderr"
    fi
fi

printf 'TIER valgrind: no invalid access across the editing verbs\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen \
        vg_run_access_only ./gbasic tests/qt_find_test.bas \
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
            pkill -KILL -f "gbasic tests/qt_find_test.bas" 2>/dev/null
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
        pass "no invalid access across the edits, marks and teardown"
    fi
    if pgrep -f "gbasic tests/qt_find_test.bas" >/dev/null 2>&1; then
        pkill -KILL -f "gbasic tests/qt_find_test.bas" 2>/dev/null
        fail "the valgrind tier left a gbasic process behind"
    else
        pass "and nothing was left running afterwards"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_find: all checks passed\n'
else
    printf 'run_qt_find: FAILED\n'
fi
exit "$status"
