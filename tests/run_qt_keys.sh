#!/bin/bash
# SHORTCUTS, ONE OR SEVERAL, AND PRESSING A KEY -- `shortcut: [ ... ]` on a
# menu or toolbar item, `qt.shortcuts` and `qt.key`.
#
# ASKED FOR BY gBASIC STUDIO, from a measurement made by hand on a real
# desktop: its Bigger Text item declared `shortcut: "Ctrl++"`, and on a US
# layout plain Ctrl+= delivers Key_Equal, so it matched nothing. Editors bind
# zoom-in to both keys. And no accelerator in Studio could be tested, because
# nothing could press a key.
#
# MEASURED BEFORE BUILDING, in C++: the `shortcut` property holds ONE sequence
# and refuses a list, while QAction::setShortcuts takes several, so a list is
# a branch in the one property door; a misspelled shortcut (`Ctlr+S`) became
# Qt's Key_unknown and was accepted in silence, so every shortcut is parsed
# and an unknown key refused; a sendEvent'd KeyPress DOES reach Qt 6's
# shortcut map, so `qt.key` needs no Qt Test dependency; and activation is
# asynchronous, so the window is made active synchronously rather than by
# processing events, which would fire gBASIC's own pump inside the call.
#
# THE LIMIT: `qt.key` sends the key Qt delivers, not the physical keys, so it
# cannot reproduce a keyboard layout. Stated in the fixture and the reference.
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
    printf 'SKIP run_qt_keys (no Qt in this build -- NOTHING about shortcuts or key presses was measured)\n'
    exit 0
fi

printf 'TIER semantics: lists, the difference, typing, clearing and refusals\n'
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 60 \
    ./gbasic --line-buffered tests/qt_keys_test.bas \
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
    elif [ "${checks:-0}" -lt 25 ]; then
        fail "only ${checks:-0} checks ran -- the fixture stopped early"
    else
        pass "$checks checks, 0 mismatches"
    fi
    # THE DIFFERENCE, named separately: a count alone does not say it ran,
    # and the two halves fail on opposite wrong implementations.
    for want in \
        'with ONE shortcut "Ctrl++", plain Ctrl+= does nothing (Studio'"'"'s bug)' \
        'and with the list set by qt.set it fires' \
        'a key nothing takes answers false'
    do
        if grep -qF "ok   $want" "$work/sem.out"; then
            pass "$want"
        else
            fail "the check '$want' did not run"
        fi
    done
    grep -v 'This plugin does not support' "$work/sem.err" > "$work/sem.err2"
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing on stderr"
    fi
fi

printf 'TIER valgrind: no invalid access across the presses and the lists\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    # BOUNDED FROM OUTSIDE, because vg_run_access_only is a shell function
    # and `timeout` cannot wrap one; the children are killed by pid while the
    # parent is still there to ask, the arrangement run_qt_which.sh uses.
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen \
        vg_run_access_only ./gbasic tests/qt_keys_test.bas \
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
            pkill -KILL -f "gbasic tests/qt_keys_test.bas" 2>/dev/null
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
        pass "no invalid access across the presses, the lists and teardown"
    fi
    if pgrep -f "gbasic tests/qt_keys_test.bas" >/dev/null 2>&1; then
        pkill -KILL -f "gbasic tests/qt_keys_test.bas" 2>/dev/null
        fail "the valgrind tier left a gbasic process behind"
    else
        pass "and nothing was left running afterwards"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_keys: all checks passed\n'
else
    printf 'run_qt_keys: FAILED\n'
fi
exit "$status"
