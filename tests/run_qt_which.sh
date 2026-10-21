#!/bin/bash
# WHICH THING WAS THIS EVENT ABOUT? (§0.33) -- `qt.which_tab`, and
# `qt.right_click` aimed at a tree row and at a tab.
#
# THE SHAPE IS WHAT THE gBASIC STUDIO BENCH ASKED TO HAVE NAMED, and it named
# it rather than this backend: a handler here takes no arguments, so every
# event that is ABOUT something needs a verb to ask which. `qt.selected`
# answers which table or tree row, `qt.gutter_click` which line, `qt.wheel`
# which way, and this adds which tab. Their words: *"a reader coming to this
# backend fresh would find that rule easier to apply than to rediscover per
# widget."*
#
# A TAB IS THE ONE CASE Qt DOES NOT ANSWER BY ITSELF, which is why it needs a
# verb where a row does not. MEASURED: a right-press MOVES a table's or a
# tree's current row, so `qt.selected` already says which was clicked -- and a
# right-press on a QTabBar correctly does NOT move the current index, so
# `currentIndex` answers the tab that was ALREADY ACTIVE. STU-21's rule is the
# opposite (*"right-clicking a background tab to close it must not first switch
# you to it"*), so without this a port acts on the wrong tab in SILENCE, which
# is this backend's named failure class -- and Studio shipped no tab menu at all
# rather than one that acts on the wrong tab.
#
# THE TABS ARE AT THE BOTTOM ON PURPOSE AND IT IS THE LOAD-BEARING DETAIL. The
# point a context-menu signal carries is in the EMITTING WIDGET'S coordinates
# and the bar is not at its origin, so `tabAt` must be asked of a MAPPED point.
# Measured with `tabPosition: South`, an unmapped `tabAt` answers **-1** -- "no
# tab" for a click squarely on one -- while with tabs at the TOP the bar IS at
# the origin and the two agree. Comfortable test data would have hidden the
# mapping entirely, so both positions are exercised and the bottom one is the
# one that can fail.
#
# THE int ROUTE IS ASSERTED THROUGH `currentChanged`, AND WHY IS SAID RATHER
# THAN IMPLIED. `tabCloseRequested(int)` carries the index that makes
# `tabsClosable: true` usable, and it reaches the same slot by the same rule --
# any single-int signal a tab widget emits -- but driving the close button needs
# the QTabBar's own child widget, which gBASIC cannot name. So what is exercised
# is the RULE, through a signal a program can drive; the close button itself is
# not, and a claim nobody has run is not a claim.
#
# AND THE SLOT CHOICE IS BY TYPE NAME, NOT BY ARGUMENT COUNT, which is a
# correctness claim NOTHING HERE CAN OBSERVE -- so it is a tripwire, and that
# is measured rather than assumed. QMetaObject::connect by index does not check
# argument types; it hands the slot a void* to reinterpret, so `toggled(bool)`
# reaching `fireIndex(int)` reads four bytes out of one. I expected the
# VALGRIND tier to catch it AND IT DOES NOT: the perturbation that chooses by
# argument count leaves every behavioural check green and valgrind silent,
# because the bool sits in an initialised, aligned stack slot and the value is
# discarded for anything that is not a tab widget. So the rule is held by
# tests/qt/slot_choice_scan.py with a planted-copy control -- the §0.16
# arrangement, where the native-dialog forcing is a tripwire because offscreen
# cannot tell the two dialogs apart.
#
# IT ALSO CARRIES A DEFECT §0.28 COULD NOT SEE, found by writing this fixture:
# a tree's depth column is declared and not drawn, and the declaration-to-widget
# column mapping counted it -- so a tree declaring `field depth( depth: true )`
# FIRST read back as EMPTY (`name` resolved to column 1 of a one-column widget)
# and the message blamed the ROW. §0.28's own fixture declares depth LAST, where
# the two indices agree by accident. The claim is asserted in run_qt_tree.sh,
# where a reader would look for it; what is here is the fixture that found it.
. "$(dirname "$0")/portable.sh"
# NO `set -e`: the `set +e` lines below are not paired with a restore, because
# this header omits -e and a bare `set -e` would ARM it (run_portable.sh
# tripwires for that).
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
    printf 'SKIP run_qt_which (no Qt in this build -- NOTHING about which tab an event is about was measured)\n'
    exit 0
fi

# -------------------------------------------------------------- SEMANTICS
printf 'TIER semantics: the tab, the mapping, the tree row and the refusals\n'
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 60 \
    ./gbasic --line-buffered tests/qt_which_test.bas \
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
    elif [ "${checks:-0}" -lt 22 ]; then
        fail "only ${checks:-0} checks ran -- the fixture stopped early"
    else
        pass "$checks checks, 0 mismatches"
    fi
    # THE THREE CHECKS THE INCREMENT EXISTS FOR, named separately because a
    # count alone does not say whether they ran -- and each is the one that
    # fails on a different wrong implementation.
    for want in \
        'and the handler was told which tab' \
        'and the right-click did not switch to it' \
        'and the handler was told which row'
    do
        if grep -q "^ok   $want" "$work/sem.out"; then
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

# ------------------------------------------------------------ SLOT CHOICE
printf 'TIER slot choice: by the signal TYPE, not by its argument count\n'
if ! command -v python3 >/dev/null 2>&1; then
    printf '  SKIP (no python3 -- the slot choice was NOT checked)\n'
else
    if python3 tests/qt/slot_choice_scan.py src/modules/qt_backend.cpp \
            > "$work/slot.out" 2>&1; then
        pass "every relay slot is chosen by an exact type comparison"
    else
        head -4 "$work/slot.out"
        fail "a relay slot is chosen without testing the signal's type"
    fi
    # THE CONTROL, planted in a COPY under the work directory and never in
    # src/, which is a build input the gate's lane scan watches.
    mkdir -p "$work/plant"
    if ! python3 tests/qt/slot_choice_plant.py src/modules/qt_backend.cpp \
            "$work/plant/planted.cpp" > "$work/plant.err" 2>&1; then
        head -2 "$work/plant.err"
        fail "the control could not be planted, so the scanner proved nothing"
    elif python3 tests/qt/slot_choice_scan.py "$work/plant/planted.cpp" \
            > "$work/plant.out" 2>&1; then
        fail "the scanner did not flag a planted count-based choice"
    else
        pass "and a planted count-based choice is flagged"
    fi
fi

# --------------------------------------------------------------- VALGRIND
printf 'TIER valgrind: no invalid access, including the bool-carrying signal\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    # THE TIER THE SLOT CHOICE RESTS ON. A signal's argument reaches a slot as
    # a void* the slot reinterprets, so a `bool` read as an `int` is a read of
    # three bytes nobody wrote -- which answers plausibly and crashes nothing,
    # so valgrind is the only thing that can see it.
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen \
        vg_run_access_only ./gbasic tests/qt_which_test.bas \
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
            pkill -KILL -f "gbasic tests/qt_which_test.bas" 2>/dev/null
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
        pass "no invalid access across the clicks, the signals and teardown"
    fi
    if pgrep -f "gbasic tests/qt_which_test.bas" >/dev/null 2>&1; then
        pkill -KILL -f "gbasic tests/qt_which_test.bas" 2>/dev/null
        fail "the valgrind tier left a gbasic process behind"
    else
        pass "and nothing was left running afterwards"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_which: all checks passed\n'
else
    printf 'run_qt_which: FAILED\n'
fi
exit "$status"
