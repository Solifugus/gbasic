#!/bin/bash
# HANDLERS THAT CALL BACK INTO THE BACKEND (§0.31) -- the suite for a CLASS
# rather than for a defect.
#
# THREE DEFECTS IN A ROW HAVE BEEN PATHS THAT ONLY EXIST ONCE SOMETHING CALLS
# BACK INTO THE BACKEND FROM INSIDE IT, and a fixture that drives the verbs in
# sequence never builds that frame:
#
#   §0.22's use-after-free, which showed up as `sizes` silently never being set;
#   §0.27's reconcile never connecting a BUILT child's handlers, invisible
#     because no perturbation put a handler on a new child;
#   and §0.31's, reported from the gBASIC Studio bench with valgrind naming
#     both ends -- `qt.select` on a tree enters the selection handler, the
#     handler calls `qt.refresh`, a tree refresh DELETES EVERY ITEM, and
#     `QTreeWidgetItem::setSelected` runs on with `this` freed. It segfaulted
#     about three runs in five and otherwise answered `unknown` for a row that
#     had just been selected -- the failure class this backend is tested
#     against, with the crash as the merciful case.
#
# run_qt_tree.sh COULD NOT SEE IT, and why is the point: it selects and then
# refreshes, and connects no selection handler, so nothing re-enters. The
# ordering was never the bug. The Studio session isolated that itself -- four
# orderings clean, and only "a selection handler that refreshes, then two
# selects" reproduces -- and suggested the general answer this suite is: ONE
# FIXTURE WHOSE HANDLERS DO WHAT A SHELL'S DO (select, refresh, read, set)
# rather than counting.
#
# TWO DEFECTS, AND FIXING THE FIRST EXPOSED THE SECOND.
#
#   THE LIFETIME. A re-lookup after the call CANNOT fix it, which valgrind said
#   plainly: `setSelected` emits the signal ITSELF, so the handler frees the
#   item while the call doing the freeing is the one still executing. So the
#   DELETION WAITS -- §0.17's arrangement for a window closed inside its own
#   event dispatch, reached by the same reasoning. While a handler is on the
#   stack a tree's refresh TAKES ITS ITEMS OUT (so they stop being found, which
#   is what a refresh means) and keeps them ALIVE; measured, `setSelected` on
#   an item with no model returns at once, so the rest of the call becomes a
#   harmless no-op. AND THE DRAIN POINT WAS WRONG ON THE FIRST ATTEMPT: freeing
#   when the handler depth returns to zero is not safe either, because the call
#   that emitted the signal is STILL ON THE STACK at depth zero -- the same
#   crash a few instructions later. The safe moment is a FRESH ENTRY from
#   gBASIC, at the top of the module's verb dispatch, where nothing of ours is
#   on the stack at all.
#
#   THE LOOP, which the crash was masking. A selection handler that refreshes
#   CHANGES THE SELECTION, which calls it again -- a feedback loop in the
#   PROGRAM rather than a Qt fault, since every round genuinely alters the
#   selection. Before the lifetime fix it terminated by corrupting memory (it
#   "fired 8 times"); with the items alive it recursed until the stack ran out.
#   So the backend owes a BOUND AND A SENTENCE, which is warning 2117 -- and a
#   warning rather than a raise is forced rather than preferred, this running
#   inside a Qt call with Qt's own frames in between and nothing to unwind
#   into, the position §0.25 records for a cell callback.
#
# THE CAP IS MEASURED, NOT CHOSEN: across every Qt fixture, example and
# cookbook recipe in the tree the deepest legitimate nesting is TWO -- a
# handler opening a modal window whose own button handler runs -- and every
# other one is one. So 16 is eight times the real maximum, the margin §0.28's
# tree-depth cap uses. `GB_QT_DEPTH_TRACE=1` prints the maximum a program
# reached, which is how that was measured and how it can be re-measured.
. "$(dirname "$0")/portable.sh"
# NO `set -e`: this header omits it, so a bare `set -e` would ARM it rather
# than restore it. run_portable.sh tripwires that and has now caught two of my
# suites within minutes of their existing.
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/build_has.sh
source tests/valgrind_tier.sh
if ! make >/dev/null; then
    printf 'run_qt_reentry: FAILED (the build failed -- nothing was measured)\n'
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# ------------------------------------------------------------------ SURVIVES
# THE FIXTURE RUNNING AT ALL IS MOST OF THE ASSERTION, which is unusual and is
# why it is said: before the fix this shape segfaulted about three runs in
# five, so the tier runs it SEVERAL TIMES rather than once -- a single clean
# run of an intermittent crash proves very little.
printf 'TIER survives: a handler that refreshes what called it, ten times over\n'
if ! build_has qt; then
    printf '  SKIP (no Qt in this build -- re-entry was NOT measured)\n'
else
    crashes=0
    hangs=0
    runs=10
    for i in $(seq 1 $runs); do
        set +e
        QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 60 \
            ./gbasic tests/qt_reentry_test.bas > "$work/r$i.out" 2>"$work/r$i.err"
        rc=$?
        if [ "$rc" = "124" ] || [ "$rc" = "137" ]; then
            hangs=$((hangs + 1))
        elif [ "$rc" != "0" ]; then
            crashes=$((crashes + 1))
            [ "$crashes" = "1" ] && printf '     exit %s\n' "$rc"
        fi
    done
    if [ "$hangs" != "0" ]; then
        fail "$hangs of $runs runs HUNG"
    elif [ "$crashes" != "0" ]; then
        fail "$crashes of $runs runs did not survive -- the re-entry is unsafe again"
    else
        pass "$runs of $runs runs survived"
    fi
    # AND THE LAST ONE'S CHECKS, which is what says it survived USEFULLY rather
    # than by doing nothing: a window that merely did not crash would pass the
    # tier above.
    if grep -q NODISPLAY "$work/r$runs.out"; then
        printf '  SKIP (no display and no offscreen platform)\n'
    else
        bad=$(grep -c '^MISMATCH' "$work/r$runs.out" || true)
        checks=$(sed -n 's/^ok  *\([0-9]*\) checks.*/\1/p' "$work/r$runs.out")
        if [ "${bad:-1}" != "0" ]; then
            grep '^MISMATCH' "$work/r$runs.out" | head -5
            fail "$bad mismatches"
        elif [ "${checks:-0}" -lt 12 ]; then
            fail "only ${checks:-0} checks ran -- the fixture stopped early"
        else
            pass "$checks checks, 0 mismatches"
        fi
    fi
fi

# ------------------------------------------------------------------ BOUNDED
# THE LOOP IS BROKEN AND SAID SO. Both halves: the warning must FIRE on the
# blunt handler, and the program must CARRY ON -- a bound that stopped the
# program would be a crash with better manners.
printf 'TIER bounded: the loop is broken, named, and survivable\n'
if ! build_has qt; then
    printf '  SKIP (no Qt in this build)\n'
elif grep -q NODISPLAY "$work/r1.out" 2>/dev/null; then
    printf '  SKIP (no display)\n'
else
    if grep -qE "\[2117\]" "$work/r1.err"; then
        pass "warning 2117 fired on the handler that refreshes its own subject"
    else
        head -2 "$work/r1.err"
        fail "the recursion was not reported -- 2117 never fired"
    fi
    if grep -qF "re-entered itself" "$work/r1.err"; then
        pass "and the message names the shape rather than only the depth"
    else
        fail "the message does not say what went wrong"
    fi
    # THE PROGRAM CARRIED ON, which is the half that separates a bound from a
    # refusal: the fixture's last check runs AFTER the loop was broken.
    if grep -qE "^ok  *[0-9]+ checks" "$work/r1.out"; then
        pass "and the program carried on to the end"
    else
        fail "the program did not finish after the loop was broken"
    fi
fi

# ------------------------------------------------------------- DEPTH MEASURED
# THE CAP IS EIGHT TIMES THE REAL MAXIMUM, and this is where that number is
# kept honest: if an ordinary program ever nests deeper than a handful, the cap
# wants re-measuring rather than the check loosening -- which is the lesson
# run_rules.sh's own bound records ("re-measure, do not just move the bound").
printf 'TIER depth: an ordinary window nests only a step or two\n'
if ! build_has qt; then
    printf '  SKIP (no Qt in this build)\n'
else
    worst=0
    worst_file=""
    for f in tests/qt_backend_test.bas tests/qt_table_test.bas \
             tests/qt_tree_test.bas tests/qt_modal_test.bas \
             tests/qt_frame_test.bas examples/qt/shell.bas; do
        [ -f "$f" ] || continue
        set +e
        d=$(GB_QT_DEPTH_TRACE=1 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
            GBASIC_SYNTAX_PATH=editors/kate timeout -k 5 60 \
            ./gbasic "$f" check 2>&1 >/dev/null \
            | sed -n 's/GB_QT_DEPTH_MAX=//p' | head -1)
        if [ -n "${d:-}" ] && [ "${d:-0}" -gt "$worst" ]; then
            worst=$d
            worst_file=$f
        fi
    done
    if [ "$worst" = "0" ]; then
        fail "no program reached a handler at all -- the trace measured nothing"
    elif [ "$worst" -le 4 ]; then
        pass "the deepest ordinary nesting is $worst ($worst_file), against a cap of 16"
    else
        fail "an ordinary program nests $worst deep ($worst_file) -- re-measure the cap of 16 rather than loosening this"
    fi
fi

# ---------------------------------------------------------------- ONE DOOR
# RUNS WITH NO QT: a source claim. Four places call back into gBASIC -- a
# relayed signal, a wheel, a gutter click and a close -- and the depth that
# makes the deferral and the cap possible is tracked at ONE of them. A fifth
# caller added beside `g_invoke` instead would be invisible to both.
printf 'TIER one door: every call back into gBASIC goes through one place\n'
# COUNTED OUTSIDE `invoke_handler`'S OWN BODY, which the first version of this
# scan got wrong: that function necessarily calls `g_invoke` once, and counting
# it reported the one door as a breach of itself -- the same shape
# run_qt_builder.sh's scanner hit when it read the COMMENT arguing for a rule
# as an instance of breaking it.
raw=$(sed '/^static void invoke_handler/,/^}/d' src/modules/qt_backend.cpp \
      | grep -c 'g_invoke(handler' || true)
via=$(grep -c 'invoke_handler(' src/modules/qt_backend.cpp || true)
inside=$(sed -n '/^static void invoke_handler/,/^}/p' src/modules/qt_backend.cpp \
         | grep -c 'g_invoke(' || true)
if [ "${raw:-1}" != "0" ]; then
    fail "something still calls g_invoke directly, so its depth is not counted"
elif [ "${inside:-0}" != "1" ]; then
    fail "invoke_handler does not call g_invoke exactly once (got ${inside:-0})"
elif [ "${via:-0}" -lt 5 ]; then
    fail "only ${via:-0} references to invoke_handler -- expected its definition, its prototype and four callers"
else
    pass "one door, $via references, and nothing calls g_invoke directly"
fi
# THE CONTROL, since "nothing calls it directly" is equally satisfied by a scan
# that read nothing.
printf 'static void x(void) { g_invoke(handler); }\n' > "$work/planted.cpp"
if [ "$(grep -c 'g_invoke(handler' "$work/planted.cpp")" != "0" ]; then
    pass "CONTROL: the scan finds a planted direct call"
else
    fail "the scan cannot see a direct call, so it is measuring nothing"
fi

# -------------------------------------------------------------- VALGRIND
printf 'TIER valgrind: no invalid access across a re-entrant refresh\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
elif ! build_has qt; then
    printf '  SKIP (no Qt in this build)\n'
else
    # THE TIER THAT WOULD HAVE CAUGHT THE ORIGINAL DEFECT, and the one the
    # Studio session used to name both ends of it.
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_reentry_test.bas \
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
            pkill -KILL -f "gbasic tests/qt_reentry_test.bas" 2>/dev/null
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
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -6
        fail "valgrind objected"
    else
        pass "no invalid access across a re-entrant refresh and teardown"
    fi
    if pgrep -f "gbasic tests/qt_reentry_test.bas" >/dev/null 2>&1; then
        pkill -KILL -f "gbasic tests/qt_reentry_test.bas" 2>/dev/null
        fail "the valgrind tier left a gbasic process behind"
    else
        pass "and nothing was left running afterwards"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_reentry: all checks passed\n'
else
    printf 'run_qt_reentry: FAILED\n'
fi
exit "$status"
