#!/usr/bin/env bash
# A WINDOW SHOWN MODALLY (docs/qt_gui_design.md §0.18) -- `qt.open_modal`,
# `qt.accept`, `qt.reject`.
#
# WHAT §0.15 NAMED THIS FOR: `qt.confirm` answers a boolean because two buttons
# is the whole of what a message box offers, and ANYTHING RICHER IS A WINDOW
# SHOWN MODALLY -- a form to fill in, a list to choose from, a thing with a
# layout of its own. It has been the last item on §0.15's open list since
# dialogs shipped.
#
# IT IS THE SAME WINDOW, and that is the claim this suite is built around: the
# declaration, the validation, the binding and the handlers are not merely
# similar to an ordinary window's, they are THE SAME CODE. Held by one shared
# open path in the source, which no behavioural check can see -- hence a
# TRIPWIRE, and beside it a PARITY tier that opens one declaration BOTH WAYS
# and requires every observable to agree.
#
# WHAT A QDialog ROOT BUYS, measured before choosing it: everything this
# backend builds works on one (menu bar, form, tabs, table, buttons), and
# `accept` (39), `reject` (40) and `done` (38) are invokable BY NAME on a
# QDialog while being ABSENT from a QWidget (-1) -- so the answer mechanism is
# Qt's own, and a non-modal window genuinely has nothing to accept.
#
# FOUR WAYS A MODAL WINDOW CAN END, and the fourth row is the one that shaped
# the implementation:
#
#   qt.accept(win)   exec -> true    `closed` fires
#   qt.reject(win)   exec -> false   fires
#   qt.close(win)    exec -> false   fires
#   the X button     exec -> false   fires
#
# ACCEPTING DELIVERS NO QEvent::Close AT ALL. Measured 2026-10-19, and finer
# than the guess that prompted it: `close()` delivers one on a visible widget
# AND on a merely hidden one, and NOTHING on a QDialog that `done()` has
# already finished with. So bookkeeping hung off the event alone would silently
# not happen for the commonest ending, and `qt.is_open` would answer true for a
# window the program had finished with. The ending is therefore one function
# called from two places and idempotent, not a side effect of an event.
#
# AND `qt.open_modal` CLOSES THE WINDOW BEFORE RETURNING, so `false` means one
# state rather than two and §0.17's single rule -- the `closed` handler is
# where a dismissed form is read -- holds here with no exception.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
# `set +e` BELOW WITHOUT A MATCHING `set -e`, DELIBERATELY. This file's mode is
# `set -uo pipefail` -- NO `-e` -- so `set +e` is already a no-op and a `set -e`
# after it does not RESTORE anything, it TURNS THE MODE ON. Copied from a suite
# whose header really is `set -euo pipefail`, it silently armed `-e` for the
# rest of the file, and the only thing that then fires is an unguarded command
# that FAILS -- which on a red path is a `grep` finding nothing. MEASURED
# 2026-10-19: a perturbation of this suite died immediately after printing its
# tier header, with no verdict and no FAIL line, indistinguishable from still
# running. The `set +e` lines are kept because they say "a failure is expected
# here", which is true; the mode changes are gone.
cd "$(dirname "$0")/.."
source tests/build_has.sh
source tests/valgrind_tier.sh
make >/dev/null

work="$(mktemp -d)"
trap 'rm -rf "$work"; pkill -KILL -f qt_modal_extent_fixture 2>/dev/null || true; pkill -KILL -f "gbasic tests/qt_modal_test.bas" 2>/dev/null || true' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# ------------------------------------------------------------ ONE OPEN PATH
# RUNS WHETHER OR NOT QT IS IN THE BUILD, because it reads the source: it is
# the one claim no behavioural check can make. A second open path would pass
# every tier below while drifting -- the record check, the already-open
# refusal, the display refusal, the syntax paths, the options, the build, the
# connect and the bind are all in it, and two copies of that is the failure
# shape this tree tripwires against (`would`/`execute` in `automation`, one
# checker and two adapters in §0.9).
printf 'TIER one open path: a modal window is the same window by construction\n'
# OCCURRENCES, NOT LINES, which is a correction the control forced: `grep -c`
# counts matching LINES, so two calls on one line read as one -- and the
# planted control put them on one line, which is exactly how somebody would
# write a second path by hand (`modal ? a : b`).
sites=$(grep -o 'gb_qt_window(' src/modules/qt.c | wc -l)
if [ "${sites:-0}" = "1" ]; then
    pass "exactly one place builds a window, so both verbs go through it"
else
    grep -n 'gb_qt_window(' src/modules/qt.c
    fail "src/modules/qt.c builds a window in $sites places -- qt.open and qt.open_modal can now drift"
fi
# THE VACUITY CONTROL: "exactly one" is equally satisfied by a grep that cannot
# match, so a planted second call must break it.
sed 's|void \*root = gb_qt_window(|void *root = (modal ? gb_qt_window(0,0,0,0,1) : gb_qt_window(|' \
    src/modules/qt.c > "$work/two.c"
planted=$(grep -o 'gb_qt_window(' "$work/two.c" | wc -l)
if [ "${planted:-0}" -gt 1 ]; then
    pass "CONTROL: and a planted second call site is counted"
else
    fail "CONTROL: the scan did not notice a planted second call site, so it is measuring nothing"
fi

if ! build_has qt; then
    printf 'SKIP run_qt_modal (no Qt in this build -- NOTHING about a modal window was measured)\n'
    exit 0
fi

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: four endings, the handler between them, and PARITY\n'
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic \
    --line-buffered tests/qt_modal_test.bas > "$work/sem.out" 2> "$work/sem.err"
rc=$?
if grep -q "SKIP no display" "$work/sem.out"; then
    printf '  SKIP (no display -- a modal window was NOT exercised)\n'
elif [ "$rc" != "0" ]; then
    # THE MISMATCHES FIRST, because a fixture that dies has usually already
    # named the cause and the exit code never does: every perturbation of the
    # ending here kills the NEXT `qt.open_modal` with "already open", which
    # says nothing about the window that failed to close.
    grep MISMATCH "$work/sem.out" | head -4
    tail -3 "$work/sem.out"; tail -3 "$work/sem.err"
    fail "the fixture exited $rc (a timeout here means the modal window was never answered)"
else
    checks=$(grep -c '^ok' "$work/sem.out" || true)
    bad=$(grep -c 'MISMATCH' "$work/sem.out" || true)
    # A FLOOR, because a fixture that dies early leaves no MISMATCH lines and
    # would report `ok 0 checks, 0 mismatches`.
    if [ "$bad" = "0" ] && [ "${checks:-0}" -ge 18 ]; then
        pass "$checks checks, 0 mismatches"
    else
        grep MISMATCH "$work/sem.out" | head -6
        fail "$checks checks, $bad mismatches (want >= 18 and none)"
    fi
    grep -v 'propagateSizeHints' "$work/sem.err" > "$work/sem.err2" || true
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing of its own on stderr"
    fi
    # THE LOAD-BEARING CHECK, NAMED so a failure says which claim went rather
    # than only moving a count.
    if grep -q "^ok   every observable agrees" "$work/sem.out" &&
       grep -q "^ok   and it observed something" "$work/sem.out"; then
        pass "PARITY: one declaration opened both ways agrees on every observable"
    else
        fail "the parity tier did not run, so nothing says a modal window is the same window"
    fi
    # AND THE ENDING THAT HAS NO EVENT BEHIND IT. Without this the bookkeeping
    # could still be hung off QEvent::Close and three of the four endings would
    # pass.
    if grep -q "^ok   the handler read what was typed" "$work/sem.out" &&
       grep -q "^ok   and it is closed afterwards" "$work/sem.out"; then
        pass "an ACCEPTED window still closed and still ran its handler"
    else
        fail "the accepted ending was not measured -- it is the one Qt delivers no close event for"
    fi
fi

# --------------------------------------------------------------- MODAL EXTENT
# THE CONTROL FOR §0.15's LIMIT, and the reason a modal window is usable at
# all. §0.15 measured that a dialog raised FROM A HANDLER suspends gBASIC's own
# sources; this is the other case and it is the common one -- "show this form
# and wait" is opened from `main`, and there the sources KEEP RUNNING. Without
# it, §0.15 reads as "a nested Qt loop starves gBASIC", which is false: what
# starves it is re-entering the INTERPRETER, not the loop.
printf 'TIER extent: a modal window opened from main does not starve the server\n'
if ! command -v curl >/dev/null 2>&1; then
    printf '  SKIP (no curl -- how far a modal window reaches was NOT measured)\n'
else
    QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 60 ./gbasic \
        --line-buffered tests/qt_modal_extent_fixture.bas "$work/go" \
        > "$work/ext.out" 2>&1 &
    fixture=$!
    port=""
    for _ in $(seq 1 100); do
        port=$(sed -n 's/^PORT //p' "$work/ext.out" | head -1)
        [ -n "$port" ] && break
        sleep 0.2
    done
    up=0
    for _ in $(seq 1 100); do
        grep -q "MODAL UP" "$work/ext.out" && { up=1; break; }
        sleep 0.2
    done
    if grep -q "NO DISPLAY" "$work/ext.out"; then
        printf '  SKIP (no display -- modal extent was NOT measured)\n'
    elif [ -z "$port" ] || [ "$up" != "1" ]; then
        head -6 "$work/ext.out"
        fail "the fixture never published a port or never raised the window"
    else
        # THE MEASUREMENT: asked from another process, while the window is up.
        if timeout 5 curl -s "http://127.0.0.1:$port/ping" | grep -q pong; then
            pass "the webserver answers while a modal window from main is up"
        else
            fail "the server did not answer during a modal window -- direction A's promise does not hold for this nested loop"
        fi
        # AND THE TIMER FIRED, which is the same claim from inside and is what
        # makes the window answerable without a person.
        if grep -q "^TICK 1" "$work/ext.out"; then
            pass "and a gBASIC timer fires inside the modal loop"
        else
            fail "no timer tick arrived inside the modal loop"
        fi
        # IT IS ANSWERED BY A GATE FILE, so nothing here rests on a guessed
        # interval: the window stays up until the measurement is done.
        : > "$work/go"
        for _ in $(seq 1 100); do
            grep -q "^DONE" "$work/ext.out" && break
            sleep 0.2
        done
        if grep -q "^MODAL DOWN true" "$work/ext.out"; then
            pass "and answering it from the program returns an accept"
        else
            tail -4 "$work/ext.out"; fail "the window was not answered"
        fi
        # IT ENDS ITSELF, which is not tidiness: a fixture the suite has to
        # kill is one that can be left behind, and run_qt_dialog.sh leaked one
        # gBASIC process per run for exactly that reason.
        if grep -q "^DONE" "$work/ext.out"; then
            pass "and the fixture ended itself, with nothing to kill"
        else
            fail "the fixture did not end itself"
        fi
    fi
    wait "$fixture" 2>/dev/null || true
    pkill -KILL -f qt_modal_extent_fixture 2>/dev/null || true
    for _ in $(seq 1 25); do
        pgrep -f qt_modal_extent_fixture >/dev/null 2>&1 || break
        sleep 0.2
    done
    if pgrep -f qt_modal_extent_fixture >/dev/null 2>&1; then
        fail "an extent fixture is still running after the tier"
    else
        pass "and nothing was left running behind it"
    fi
fi

# ------------------------------------------------------------------ REFUSALS
# EACH BESIDE ITS NEAREST LEGAL NEIGHBOUR -- the semantics tier is the control,
# since every one of these verbs is used there successfully.
printf 'TIER refusals: accepting is for a window that was shown modally\n'
refuse() {                      # name, program body, expected fragment
    cat > "$work/r.bas" <<BAS
window app( title: "R", width: 200, height: 100 )
    column( margin: 6 )
        button save( label: "Save" )
    end column
end window
program main(args)
    if not qt.can_display() then
        print "NO DISPLAY"
        return
    end if
    on error goto refused
$2
    print "ACCEPTED"
    return
refused:
    print "REFUSED " + error.message
    error.clear()
end program
BAS
    set +e
    QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 40 ./gbasic \
        --line-buffered "$work/r.bas" > "$work/r.out" 2>&1
    if grep -q "NO DISPLAY" "$work/r.out"; then
        printf '  SKIP %s (no display)\n' "$1"
    elif grep -q "REFUSED" "$work/r.out" && grep -qF "$3" "$work/r.out"; then
        pass "$1"
    else
        head -3 "$work/r.out"
        fail "$1 (wanted a refusal containing \"$3\")"
    fi
}

refuse 'accepting an ordinary window names the verb that would have made it modal' \
    '    o = qt.open(app)
    x = qt.accept(app)' \
    'not opened modally'
refuse 'and rejecting one is refused the same way' \
    '    o = qt.open(app)
    x = qt.reject(app)' \
    'not opened modally'
refuse 'accepting a window that is not open names the window' \
    '    x = qt.accept(app)' \
    'window `app` is not open'
refuse 'qt.accept takes one window' \
    '    o = qt.open(app)
    x = qt.accept(app, 1)' \
    'qt.accept expects one window declaration'
refuse 'qt.reject takes one window' \
    '    x = qt.reject()' \
    'qt.reject expects one window declaration'
refuse 'qt.open_modal takes one window' \
    '    x = qt.open_modal(app, 1)' \
    'qt.open_modal expects one window declaration'
refuse 'and refuses a window that is already open, like qt.open' \
    '    o = qt.open(app)
    x = qt.open_modal(app)' \
    'is already open'
refuse 'and something that is not a window at all' \
    '    x = qt.open_modal({ a: 1 })' \
    'qt.open_modal'

# ----------------------------------------------------------------- NO DISPLAY
# THE SAME SENTENCE `qt.open` GIVES, because it is the same open path -- which
# is this tier's real subject: a second path would have had to remember to
# refuse, and §0.3's whole point is that forgetting means an uncatchable abort.
printf 'TIER no display: the shared path means the refusal is shared too\n'
cat > "$work/nd.bas" <<'BAS'
window app( title: "ND", width: 200, height: 100 )
    column
        button save( label: "Save" )
    end column
end window
program main(args)
    on error goto refused
    x = qt.open_modal(app)
    print "ACCEPTED"
    return
refused:
    print "REFUSED " + error.message
    error.clear()
end program
BAS
set +e
env -u DISPLAY -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM -u XDG_RUNTIME_DIR \
    -u XDG_SESSION_TYPE timeout -k 5 60 ./gbasic "$work/nd.bas" \
    > "$work/nd.out" 2>&1
nd_rc=$?
if [ "$nd_rc" = "0" ] && grep -q "no display is available" "$work/nd.out"; then
    pass "with no display it names the display, and is catchable"
else
    cat "$work/nd.out"
    fail "qt.open_modal did not refuse by naming the display (exit $nd_rc)"
fi

# ------------------------------------------------------------------ VALGRIND
# ACCESS-ONLY, for the reason run_qt_backend.sh records: Qt does not free its
# platform plugin, font configuration or QApplication statics, so a LEAK claim
# would be a suppression file describing Qt. The JIT is off because PCRE2's
# regex JIT over-reads its subject by design (§0.14).
#
# WHAT IS NEW HERE IS A NESTED MODAL LOOP OVER A QDialog ROOT, entered five
# times in one run, with a window deleted after each -- so the interesting
# question is whether anything reads the window after `exec()` handed back.
printf 'TIER valgrind: no invalid access across five modal loops and teardown\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access in a modal loop was NOT measured)\n'
else
    # BOUNDED, AND THAT IS NOT CAUTION -- a perturbation proved it. With the
    # pump not armed around `exec()` the fixture waits for an answer that can
    # never come, and valgrind makes the wait an order of magnitude longer, so
    # this tier ran for ever and the whole suite was killed from outside with
    # NO verdict printed at all: red, silent, and indistinguishable from still
    # running. A hang is not a failure, which this tree records in several
    # places and which I had bounded everywhere here except the one tier that
    # needed it most.
    #
    # `timeout` CANNOT BE USED: `vg_run_access_only` is a shell FUNCTION, so
    # `timeout vg_run_access_only ...` exits 127 -- run_odbc.sh's recorded trap,
    # and one this session already hit once. So it runs in the background
    # against a deadline, the way the extent tier above waits.
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_modal_test.bas \
        >/dev/null 2>"$work/vg.err" &
    vg_pid=$!
    vg_killed=0
    vg_deadline=$((SECONDS + 420))
    while kill -0 "$vg_pid" 2>/dev/null; do
        if [ "$SECONDS" -ge "$vg_deadline" ]; then
            # AND THE CHILD, NOT ONLY THE WAITER. `$!` is the SUBSHELL bash
            # forks to run a shell function in the background, so `kill -9`
            # on it leaves valgrind and the interpreter underneath it
            # ORPHANED -- holding a modal dialog for ever. MEASURED, not
            # reasoned: a leftover from this tier was found 19 hours after the
            # run that started it, by sweeping the machine rather than by any
            # check. Identical in cause to the leak run_qt_dialog.sh records
            # (`timeout ... &` makes `$!` the bound, not the child), which is
            # why the fixture is killed BY NAME here and in the EXIT trap.
            # THE CHILDREN FIRST, while the parent is still there for
            # `pgrep -P` to ask, then the waiter, then a sweep BY NAME as the
            # backstop. By-pid is the one that cannot hit an innocent
            # bystander, and the by-name sweep is why there is a backstop at
            # all: a pattern matches any process whose command line merely
            # MENTIONS the fixture, which is not hypothetical -- the EXIT trap
            # killed the very shell that was verifying this fix, so the
            # perturbation's restore never ran and the perturbed file sat
            # there looking restored.
            for vg_kid in $(pgrep -P "$vg_pid" 2>/dev/null); do
                kill -9 "$vg_kid" 2>/dev/null
            done
            kill -9 "$vg_pid" 2>/dev/null
            pkill -KILL -f "gbasic tests/qt_modal_test.bas" 2>/dev/null
            vg_killed=1
            break
        fi
        sleep 1
    done
    wait "$vg_pid"
    vg_rc=$?
    if [ "$vg_killed" = "1" ]; then
        # A KILLED VALGRIND EXITS 137, WHICH IS NOT $VG_EXIT, so without this
        # branch a hang would read as a clean run -- the "a red tier that
        # reports nothing" class, one layer in.
        fail "the fixture hung under valgrind and was killed, so memory access was NOT measured"
    elif [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    else
        pass "no invalid access across five modal loops, four closes and teardown"
    fi

    # THE ABSENCE IS ASSERTED, because the leak is invisible from inside: the
    # tier passed, the suite passed, and an interpreter holding a modal dialog
    # went on running for 19 hours. docs/reference.md promises in bold that
    # nothing this interpreter starts outlives it, and here it was the SUITE
    # breaking that promise about the interpreter -- which
    # run_process_lifetime.sh structurally cannot see.
    if pgrep -f "gbasic tests/qt_modal_test.bas" >/dev/null 2>&1; then
        pkill -KILL -f "gbasic tests/qt_modal_test.bas" 2>/dev/null
        fail "the valgrind tier left a gbasic process behind (killed now, but the bound leaks children)"
    else
        pass "and nothing was left running afterwards"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_modal: all checks passed\n'
else
    printf 'run_qt_modal: FAILED\n'
fi
exit "$status"
