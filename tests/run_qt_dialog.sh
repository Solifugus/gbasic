#!/usr/bin/env bash
# DIALOGS (docs/qt_gui_design.md §0.15) -- `qt.inform`, `qt.confirm`,
# `qt.answer`.
#
# A DIALOG OPENS A NESTED EVENT LOOP, which is the one case §5.1 ruled
# direction B out on: under it gBASIC's poll does not run for the dialog's
# whole life (a 651ms gap over a 600ms dialog, which is a webserver answering
# nothing). So the hazard was priced before any dialog existed.
#
# AND THE CLAIM THAT CAME WITH IT IS NARROWER THAN IT READS, which is this
# suite's reason for existing. §5.1 is about QT's loop surviving the nested
# one; a C++ probe in gBASIC's exact arrangement confirms the tick fires during
# a modal QMessageBox and can press its button. What cannot happen is gBASIC
# being serviced while the dialog went up from inside the pump's own tick:
#
#   raised from `main`              the sources keep running, and a `timer`
#                                   can answer the box
#   raised inside a watcher body    they are SUSPENDED until it is answered
#
# THE SECOND IS THE NORMAL CASE -- a `timer` body, a webserver handler, an
# actor mailbox watcher and a button clicked from any of them all run inside
# one -- so a modal dialog is modal to the whole PROGRAM, not just its window.
#
# THAT SECOND ROW USED TO SAY "FROM A HANDLER" AND NAMED THE WRONG CAUSE,
# corrected by the `stall` tier below: TWO guards hold it, each sufficient
# alone -- Qt declining to re-deliver its own timer, and gBASIC's
# `watcher_drain` declining to re-enter -- and the fixture that establishes it
# HAS NO HANDLER IN IT AT ALL. That is defensible for a desktop application and surprising
# for one that is also a server, and a limitation nobody can observe is one
# nobody will believe, so it is ASSERTED here with a real server on a real
# port rather than written down.
#
# SELF-CHECKING NOT GOLDEN AND FORCED: every defect is an ordinary-looking run.
# A confirm that always answers `false` is the safe answer by accident rather
# than by decision and reads exactly like a careful user; an `answer` that
# silently did nothing would leave a program waiting for somebody who is not
# there, which is a HANG and not a failure.
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
# `-KILL`, NOT THE DEFAULT SIGTERM, and that is measured rather than careful:
# this interpreter installs a SIGTERM handler for pool drain, and the modal
# fixture is BLOCKED INSIDE A NESTED QT LOOP, so it records the signal and
# never acts on it. Measured 2026-10-18 -- a leftover fixture survived SIGTERM
# and died only on SIGKILL -- after finding SIXTEEN of them on this machine,
# one per run of this suite since it shipped that morning.
trap 'rm -rf "$work"; pkill -KILL -f qt_dialog_modal_fixture 2>/dev/null || true' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

nodisplay() {
    env -u DISPLAY -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM -u XDG_RUNTIME_DIR \
        -u XDG_SESSION_TYPE "$@"
}

if ! build_has qt; then
    printf 'SKIP run_qt_dialog (no Qt in this build -- NOTHING about dialogs was measured)\n'
    exit 0
fi

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: both answers, the refusal, and the timer that answers\n'
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 90 ./gbasic --line-buffered \
    tests/qt_dialog_test.bas > "$work/sem.out" 2> "$work/sem.err"
rc=$?
if grep -q "SKIP no display" "$work/sem.out"; then
    printf '  SKIP (no display -- dialogs were NOT exercised)\n'
elif [ "$rc" != "0" ]; then
    tail -4 "$work/sem.out"; tail -3 "$work/sem.err"
    fail "the fixture exited $rc"
else
    checks=$(grep -c '^ok' "$work/sem.out" || true)
    bad=$(grep -c 'MISMATCH' "$work/sem.out" || true)
    # A FLOOR, because a fixture that dies early leaves no MISMATCH lines and
    # would otherwise report `ok 0 checks, 0 mismatches`.
    if [ "$bad" = "0" ] && [ "${checks:-0}" -ge 6 ]; then
        pass "$checks checks, 0 mismatches"
    else
        grep MISMATCH "$work/sem.out" | head -5
        fail "$checks checks, $bad mismatches (want >= 6 and none)"
    fi
fi

# ------------------------------------------------------------- MODAL EXTENT
# THE TIER THIS SUITE IS FOR, and it needs BOTH halves: a server that never
# answered would satisfy the "stalled" half on its own, and a dialog that never
# opened would satisfy the control.
printf 'TIER modal extent: how far a handler-raised dialog reaches\n'
if ! command -v curl >/dev/null 2>&1; then
    printf '  SKIP (no curl -- how far a dialog reaches was NOT measured)\n'
else
    QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 40 ./gbasic \
        --line-buffered tests/qt_dialog_modal_fixture.bas \
        > "$work/modal.out" 2>&1 &
    fixture=$!
    # WAIT FOR THE PORT IT CHOSE, never a guessed one: no golden in this tree
    # carries a number this machine had to be able to bind.
    port=""
    for _ in $(seq 1 60); do
        port=$(sed -n 's/^PORT //p' "$work/modal.out" | head -1)
        [ -n "$port" ] && break
        sleep 0.2
    done
    if [ -z "$port" ]; then
        head -5 "$work/modal.out"; fail "the fixture never published a port"
    elif grep -q "NO DISPLAY" "$work/modal.out"; then
        printf '  SKIP (no display -- modal extent was NOT measured)\n'
    else
        # THE CONTROL FIRST, while no dialog is up. Without it, "stalled"
        # is equally satisfied by a server that never worked.
        if timeout 5 curl -s "http://127.0.0.1:$port/ping" | grep -q pong; then
            pass "with no dialog up, the server answers"
        else
            fail "the server did not answer even with no dialog up"
        fi
        # WAIT FOR THE DIALOG, a positive signal rather than a guessed sleep.
        up=0
        for _ in $(seq 1 60); do
            grep -q "DIALOG UP" "$work/modal.out" && { up=1; break; }
            sleep 0.2
        done
        if [ "$up" != "1" ]; then
            fail "the handler never raised a dialog, so nothing was measured"
        elif timeout 5 curl -s "http://127.0.0.1:$port/ping" | grep -q pong; then
            # IF THIS EVER PASSES, THE LIMITATION IS GONE and the design note
            # plus the reference and the cookbook all have to be corrected --
            # which is the negative-control shape run_limitations.sh uses.
            fail "the server ANSWERED during a handler-raised dialog -- the limitation is gone, so §0.15, the reference and the cookbook now overstate it"
        else
            pass "and during one it does NOT: a modal dialog is modal to the whole program"
        fi
        # AND THE DIALOG IS STILL UP, so what was measured was a stall and not
        # a crash -- the fixture prints DIALOG DOWN only once answered.
        if grep -q "DIALOG DOWN" "$work/modal.out"; then
            fail "the dialog had already closed, so the stall was not what was measured"
        else
            pass "the dialog was still open, so that was a stall and not a crash"
        fi
    fi
    # `$fixture` IS THE `timeout` PROCESS, NOT `./gbasic`, which is the whole
    # defect: killing `timeout` leaves its child ORPHANED, and with `timeout`
    # dead nothing is left to escalate to it. So the fixture is named
    # explicitly and killed with SIGKILL, for the reason the trap above gives.
    kill -9 "$fixture" 2>/dev/null || true
    wait "$fixture" 2>/dev/null || true
    pkill -KILL -f qt_dialog_modal_fixture 2>/dev/null || true
    # AND IT IS ASSERTED, because a leak here is INVISIBLE: the tier passes,
    # the suite passes, and a gBASIC process holding a modal dialog sits on the
    # machine for ever. docs/reference.md promises in bold that nothing this
    # interpreter starts outlives it, and run_process_lifetime.sh exists for
    # that promise -- but it guards gBASIC's own CHILDREN, where this is the
    # suite leaking the interpreter itself.
    for _ in $(seq 1 25); do
        pgrep -f qt_dialog_modal_fixture >/dev/null 2>&1 || break
        sleep 0.2
    done
    if pgrep -f qt_dialog_modal_fixture >/dev/null 2>&1; then
        fail "a modal fixture is still running after the tier -- this suite leaks one gBASIC process per run"
    else
        pass "and nothing was left running behind it"
    fi
fi

# ------------------------------------------------------------------- REFUSALS
printf 'TIER refusals: each naming the cause it can act on\n'
cat > "$work/nd.bas" <<'BAS'
window app( title: "D", width: 200, height: 100 )
    column
        text t( label: "x" )
    end column
end window
program main(args)
    on error goto refused
    x = qt.confirm(app, "?")
    print "ACCEPTED"
    return
refused:
    print "REFUSED " + error.message
    error.clear()
end program
BAS
set +e
nodisplay timeout -k 5 60 ./gbasic "$work/nd.bas" > "$work/nd.out" 2>&1
nd_rc=$?
# NO DISPLAY NAMES THE DISPLAY. "window `app` is not open" is TRUE here and
# reports the wrong cause, which is the class this tree has recorded five
# times -- the window is shut because there is nowhere to put it.
if [ "$nd_rc" = "0" ] && grep -q "no display is available" "$work/nd.out"; then
    pass "with no display it names the display, not the shut window"
else
    cat "$work/nd.out"; fail "the no-display refusal did not name the display (exit $nd_rc)"
fi
# AND THE CONTROL: with a display, an unopened window gets the OTHER sentence,
# or naming the display would just be what this verb always says.
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 60 ./gbasic "$work/nd.bas" > "$work/nd2.out" 2>&1
if grep -q "is not open" "$work/nd2.out"; then
    pass "and with one, an unopened window gets the other sentence"
else
    cat "$work/nd2.out"; fail "the two refusals do not differ, so neither names a cause"
fi

# -------------------------------------------------------- WHAT HOLDS THE STALL
# TWO INDEPENDENT GUARDS, EACH SUFFICIENT ALONE, and it took removing them one
# at a time to tell -- which is why this tier exists rather than a paragraph.
#
# THE WORDING ABOVE USED TO NAME A HANDLER. Its measurement is right and the
# cause is wider: this fixture HAS NO HANDLER and stalls identically, so what
# matters is that a WATCHER BODY was on the stack -- a `timer` body, a
# webserver handler, an actor mailbox watcher, an `http` event watcher, or a
# button clicked from any of them.
#
#   QT'S      gBASIC's sources ride ONE reference-counted Qt timer, and Qt will
#             not re-deliver a timer whose handler is on the stack: measured in
#             C++ at 0 pump ticks against 3 for a fresh timer during one box.
#   gBASIC'S  `watcher_drain` returns at once while a watcher body is on the
#             stack, so the loop polls, finds work and declines to dispatch it.
#
# REMOVING EITHER CHANGES NOTHING -- given a timer of its own the pump reached
# gBASIC's loop 397 times during this dialog and the stall did not move, and
# with gBASIC's guard alone removed the pump still never ticked. WITH BOTH OFF
# IT DISSOLVES, which is what proves the pair rather than either.
#
# SO THE FIX IS NOT A TIMER CHANGE, which is the useful half to record: it
# needs watcher dispatch to become RE-ENTRANT, and that guard is deliberate and
# right -- a watcher body re-entered mid-statement is the hazard, not the
# remedy. A language decision, not this backend's to take.
#
# THE FIXTURE HANGS BY DESIGN, so it is bounded and judged from outside.
printf 'TIER stall: it needs no handler, which is what names its real cause\n'
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 12 ./gbasic \
    --line-buffered tests/qt_dialog_pump_fixture.bas > "$work/pump.out" 2>&1
pump_rc=$?
if grep -q "NO DISPLAY" "$work/pump.out"; then
    printf '  SKIP (no display -- what starves the pump was NOT measured)\n'
elif ! grep -q "DIALOG UP" "$work/pump.out"; then
    head -4 "$work/pump.out"
    fail "the fixture never raised a dialog, so nothing was measured"
elif grep -q "^TICK 2" "$work/pump.out"; then
    # IF THIS EVER HAPPENS the pump re-entered its own tick, the stall is gone,
    # and this suite's table plus §0.15 and the reference all overstate it --
    # run_limitations.sh's negative-control shape.
    fail "a watcher body ran during a dialog raised from inside another one -- watcher dispatch has become re-entrant, so the stall is gone and this suite, §0.15 and the reference now overstate it"
elif grep -q "DIALOG DOWN" "$work/pump.out"; then
    fail "the dialog closed by itself, so what was measured was not a stall"
elif [ "$pump_rc" = "0" ]; then
    fail "the program ended on its own, so the dialog was answered by something"
else
    pass "with NO handler involved, a dialog raised inside a watcher body stalls every source"
    pass "and the dialog was still open, so that was a stall and not a crash"
fi

# ------------------------------------------------------------------ VALGRIND
# ACCESS-ONLY, the policy run_qt_backend established: Qt does not free its
# platform plugin or QApplication statics, so a LEAK claim would be a
# suppression file describing Qt. What is new here is a heap QMessageBox held
# behind a QPointer and a refcounted pump, so the ACCESS claim is worth making.
printf 'TIER valgrind: no invalid access across a nested modal loop\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access in a dialog was NOT measured)\n'
else
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen vg_run_access_only ./gbasic \
        tests/qt_dialog_test.bas >/dev/null 2>"$work/vg.err"
    vg_rc=$?
    if [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    else
        pass "no invalid access across construction, exec and teardown"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_dialog: all checks passed\n'
else
    printf 'run_qt_dialog: FAILED\n'
fi
exit $status
