#!/usr/bin/env bash
# THE WINDOW LIFECYCLE (docs/qt_gui_design.md §0.17) -- `qt.close`,
# `qt.is_open`, `on <win>.closed`, and the window addressable by its own name.
#
# WHAT WAS MISSING WAS NOT "A SECOND WINDOW". That was MEASURED FIRST and turned
# out to work already: two windows open at once, parts scoped per window, a
# dialog parenting to whichever one is named, `qt.stop()` with both up -- and
# opening the same one twice already refused by name. Third time in this backend
# that measuring a gap before filling it found it closed (menus needed no
# grammar; the builder's door was already open).
#
# WHAT WAS MISSING WAS THE LIFECYCLE, and the four gaps were measured one
# program at a time:
#
#   qt.close(win)                undefined function: qt.close
#   a method on the WINDOW       qt.invoke: this window has no `a_win`
#   on a_win.closed              names nothing in this window
#   quitOnLastWindowClosed       never set, never asserted, nothing could close
#                                a window to find out
#
# THEN SIX THINGS WERE MEASURED IN C++ BEFORE ANY OF IT WAS WRITTEN, and the
# fifth is the one that decided the design:
#
#   quitOnLastWindowClosed       true (Qt's default)
#   close() offscreen            delivers QEvent::Close to an event filter
#   closing one of two           exec() keeps running
#   closing the last one         exec() RETURNS
#   AFTER close()                THE WIDGET IS STILL ALIVE AND READABLE
#   delete right after close()   safe
#
# SO A CLOSE IS BOOKKEEPING AND NOT A HIDE. Qt does not set `WA_DeleteOnClose`,
# so a window the person dismissed goes on answering `qt.read` with what it was
# showing -- a program reading off a screen nobody is looking at, raising
# nothing. The close removes it from gBASIC's registry and every verb that needs
# an open window then refuses by name, WHICH MAKES `on <win>.closed`
# LOAD-BEARING: it fires before the registry forgets, so it is the one place a
# program can still read what the person typed.
#
# ONE PATH FOR ALL THREE WAYS IN -- the program's verb, an invoked `close`, and
# the window manager's button all go through `QWidget::close()` -- the rule
# `would` and `execute` follow in `automation`, and for the same reason: if they
# could disagree, a suite driving one would be describing the other. The X
# button is the one that cannot be driven here (offscreen has no window
# manager) and it reaches the same call through Qt's platform layer, so that
# much is construction rather than measurement, and is said rather than implied.
. "$(dirname "$0")/portable.sh"
. "$(dirname "$0")/premise.sh"
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

# ------------------------------------------------------- THE WINDOW IS A PART
# RUNS WHETHER OR NOT QT IS IN THE BUILD, because it is a claim about the
# DECLARATION and the load-time pass needs no Qt -- the property that let the
# whole window vocabulary ship before any backend existed. Here it is the half
# that makes the window addressable at all: the handler-target list is seeded
# with the window's own name, so `on app.closed` is a handler rather than a
# refusal, and a part sharing that name is refused rather than silently winning
# the by-name table.
printf 'TIER declaration: the window is a target like any part, and a part may not shadow it\n'
cat > "$work/shadow.bas" <<'BAS'
window app( title: "x" )
    column
        field app( label: "collides" )
    end column
end window
program main(args)
    print "RAN"
end program
BAS
set +e
./gbasic "$work/shadow.bas" > "$work/shadow.out" 2>&1
set -e
if grep -q "the window itself is called 'app'" "$work/shadow.out" &&
   ! grep -q RAN "$work/shadow.out"; then
    pass "a part taking the window's name is refused, and nothing ran"
else
    cat "$work/shadow.out"
    fail "a part was allowed to shadow the window's own name"
fi
# THE SAME RULE THROUGH THE RECORD ADAPTER, since §4's builder gets every rule
# the block gets -- one checker, two adapters, and the direction that drifts
# silently is a rule only one of them knows.
cat > "$work/shadow_rec.bas" <<'BAS'
program main(args)
    w = { kind: "window", name: "app", options: { title: "x" },
          items: [ { kind: "part", word: "field", name: "app",
                     options: { label: "collides" } } ] }
    print "CHECK " + qt.check(w)
end program
BAS
set +e
./gbasic "$work/shadow_rec.bas" > "$work/shadow_rec.out" 2>&1
set -e
if grep -q "the window itself is called 'app'" "$work/shadow_rec.out"; then
    pass "and a BUILT window is refused in the same words"
else
    cat "$work/shadow_rec.out"
    fail "the record adapter missed the collision the block adapter catches"
fi
# AND THE HANDLER IS LEGAL, which is the control: without it the rule above is
# satisfied by a build where the window's name means nothing at all.
cat > "$work/handler_ok.bas" <<'BAS'
window app( title: "x" )
    column
        field who( label: "w" )
    end column
    on app.closed
        print "closed"
    end on
end window
program main(args)
    print "RAN"
end program
BAS
set +e
./gbasic "$work/handler_ok.bas" > "$work/handler_ok.out" 2>&1
set -e
if grep -q RAN "$work/handler_ok.out"; then
    pass 'CONTROL: `on app.closed` on the window itself is accepted'
else
    cat "$work/handler_ok.out"
    fail "CONTROL: a handler on the window's own name was refused"
fi
# AND IT NAMES NOTHING ELSE. The refusal load-time validation exists for is a
# handler that can never fire, and the window's name must not have widened it
# into accepting anything.
cat > "$work/handler_bad.bas" <<'BAS'
window app( title: "x" )
    column
        field who( label: "w" )
    end column
    on nobody.closed
        print "never"
    end on
end window
program main(args)
    print "RAN"
end program
BAS
set +e
./gbasic "$work/handler_bad.bas" > "$work/handler_bad.out" 2>&1
set -e
if grep -q "names nothing in this window" "$work/handler_bad.out" &&
   ! grep -q RAN "$work/handler_bad.out"; then
    pass "CONTROL: and a handler naming nothing is still refused"
else
    cat "$work/handler_bad.out"
    fail 'CONTROL: `on nobody.closed` was not refused'
fi

if ! build_has qt; then
    printf 'SKIP run_qt_lifecycle (no Qt in this build -- NOTHING about closing a window was measured)\n'
    exit 0
fi

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: open, address, close, reopen -- and the handler in between\n'
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic \
    --line-buffered tests/qt_lifecycle_test.bas > "$work/sem.out" 2> "$work/sem.err"
rc=$?
set -e
if grep -q "SKIP no display" "$work/sem.out"; then
    printf '  SKIP (no display -- the lifecycle was NOT exercised)\n'
elif [ "$rc" != "0" ]; then
    tail -4 "$work/sem.out"; tail -3 "$work/sem.err"
    fail "the fixture exited $rc"
else
    checks=$(grep -c '^ok' "$work/sem.out" || true)
    bad=$(grep -c 'MISMATCH' "$work/sem.out" || true)
    # A FLOOR, because a fixture that dies early leaves no MISMATCH lines and
    # would report `ok 0 checks, 0 mismatches` -- a green line above the FAILs
    # that named the problem.
    if [ "$bad" = "0" ] && [ "${checks:-0}" -ge 30 ]; then
        pass "$checks checks, 0 mismatches"
    else
        grep MISMATCH "$work/sem.out" | head -6
        fail "$checks checks, $bad mismatches (want >= 30 and none)"
    fi
    # `propagateSizeHints` IS QT'S OWN NOISE under the offscreen platform --
    # it says so whenever a window is resized, which every window here is --
    # and filtering it is what run_qt_examples.sh already does. Everything
    # else on stderr is the program's.
    grep -v 'propagateSizeHints' "$work/sem.err" > "$work/sem.err2" || true
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing of its own on stderr"
    fi
    # THE TIER TO READ FIRST, named so a failure says which claim went: the
    # bookkeeping is unconditional and the handler is the optional part, so a
    # filter installed only where a handler appears passes every other check in
    # the file.
    if grep -q "^ok   plain is NOT open" "$work/sem.out"; then
        pass "a window with NO closed handler is forgotten too"
    else
        fail "the handler-less window was not measured, so the bookkeeping could be conditional on a handler"
    fi
    # AND IT IS NAMED rather than left to a count, because the perturbation for
    # it CRASHES (the registry keeps an entry pointing at a deleted window), so
    # the mismatch that names the cause is followed by an exit 139 that does
    # not -- and without this line the crash would be the whole report.
    # HONEST OVERLAP: the growth fixture's window has no handler either, so
    # that tier sees the same perturbation; this is the one that says why.
    # AND THE ORDER, which is the whole argument for the handler existing.
    if grep -q "^ok   it read the LIVE widget" "$work/sem.out" &&
       grep -q "^ok   reading a part refuses, naming the window" "$work/sem.out"; then
        pass "the handler reads what the person typed, and the same read after refuses"
    else
        fail "the before/after pair did not run, so the handler's ordering is unasserted"
    fi
fi

# ----------------------------------------------------------------------- LOOP
# `quitOnLastWindowClosed` IS QT'S DEFAULT AND WE NEVER SET IT, which is
# exactly why it needs asserting: nothing here chose it, so nothing here would
# notice it changing -- and a desktop application whose loop outlives its last
# window is a process the person cannot get rid of.
#
# BOUNDED, because a loop that never ends is a HANG and a hang is not a failure
# (run_string_nul.sh's recorded trap). `-k` because this interpreter installs a
# SIGTERM handler for pool drain, so a bound that might not fire is not a bound.
printf 'TIER loop: closing the last window ends qt.run(), and closing one of two does not\n'
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 60 ./gbasic \
    --line-buffered tests/qt_lifecycle_loop_fixture.bas \
    > "$work/loop.out" 2> "$work/loop.err"
loop_rc=$?
set -e
if grep -q "SKIP no display" "$work/loop.out"; then
    printf '  SKIP (no display -- what closing a window does to the loop was NOT measured)\n'
elif [ "$loop_rc" != "0" ]; then
    cat "$work/loop.out"; tail -3 "$work/loop.err"
    fail "the loop fixture exited $loop_rc (a timeout here means the loop never ended)"
else
    # THE CONTROL FIRST. Without it "the loop ended" is satisfied by a build
    # where `qt.run()` returns immediately.
    if grep -q "STILL RUNNING WITH ONE OPEN" "$work/loop.out"; then
        pass "CONTROL: four ticks after one of two closed, the loop is still running"
    else
        cat "$work/loop.out"
        fail "CONTROL: the loop did not survive closing one of two windows"
    fi
    if grep -q "LOOP DID NOT END" "$work/loop.out"; then
        cat "$work/loop.out"
        fail "closing the last window left qt.run() running -- quitOnLastWindowClosed no longer holds"
    elif grep -q "^RUN RETURNED 0" "$work/loop.out" &&
         grep -q "^TICKS 6" "$work/loop.out"; then
        # TICKS PINNED, because `RUN RETURNED 0` alone is satisfied by a loop
        # that ended for any reason at any time -- 6 is the tick that closed it.
        pass "and closing the last one ended it, on the tick that closed it"
    else
        cat "$work/loop.out"
        fail "qt.run() did not return at the close of the last window"
    fi
    if grep -q "^FIRST OPEN false" "$work/loop.out" &&
       grep -q "^OTHER OPEN false" "$work/loop.out"; then
        pass "and both windows report closed afterwards"
    else
        cat "$work/loop.out"; fail "a closed window still reported itself open"
    fi
fi

# ------------------------------------------------- STOP IS NOT A CLOSE
# A CORRECTION THIS INCREMENT EXPOSED RATHER THAN INTRODUCED. `gb_qt_stop`
# called `QCoreApplication::quit()`, and MEASURED on Qt 6.10.2 that CLOSES
# EVERY TOP-LEVEL WINDOW (it calls closeAllWindows) where `exit(0)` ends the
# loop and leaves them alone. So asking for the thread back was dismissing the
# program's windows, and nothing could see it -- they were merely HIDDEN, every
# part stayed readable, and no fixture asked whether the window was still up.
# It surfaced only once a close became bookkeeping, when a program that reads
# its window after `qt.run()` returned was told it was not open -- MEASURED
# rather than grepped, eight programs reading a window textually after
# `qt.run()` and exactly ONE actually breaking.
printf 'TIER stop: asking for the thread back does not dismiss the windows\n'
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 60 ./gbasic \
    --line-buffered tests/qt_lifecycle_stop_fixture.bas \
    > "$work/stop.out" 2> "$work/stop.err"
stop_rc=$?
set -e
if grep -q "SKIP no display" "$work/stop.out"; then
    printf '  SKIP (no display -- what qt.stop does to a window was NOT measured)\n'
elif [ "$stop_rc" != "0" ]; then
    cat "$work/stop.out"; tail -3 "$work/stop.err"
    fail "the stop fixture exited $stop_rc"
else
    # BOTH HALVES, because each catches a different wrong implementation:
    # `is_open` catches a stop that runs the close bookkeeping, and `visible`
    # catches one that merely HIDES the window -- which is what the defect did,
    # and which `is_open` alone would go on missing.
    if grep -q "^STILL OPEN true" "$work/stop.out" &&
       grep -q "^STILL READS Ada Lovelace" "$work/stop.out"; then
        pass "after qt.run() returns the window is still open and still readable"
    else
        cat "$work/stop.out"; fail "qt.stop() closed the window it was not asked to close"
    fi
    if grep -q "^STILL VISIBLE true" "$work/stop.out"; then
        pass "and still up on the screen, which is the half a registry check cannot see"
    else
        cat "$work/stop.out"; fail "qt.stop() hid the window -- quit() closes every window, exit(0) does not"
    fi
    if grep -q "^CLOSED FIRED 0" "$work/stop.out"; then
        pass "and no closed handler fired, so that word still means one thing"
    else
        cat "$work/stop.out"; fail "qt.stop() fired a closed handler, which would make that word mean two things"
    fi
fi

# ------------------------------------------------------------------ REFUSALS
# EACH BESIDE ITS NEAREST LEGAL NEIGHBOUR -- the semantics tier above is the
# control, since every one of these verbs is used there successfully.
printf 'TIER refusals: a close is not a no-op, and `closed` is the window\x27s alone\n'
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
    set -e
    if grep -q "NO DISPLAY" "$work/r.out"; then
        printf '  SKIP %s (no display)\n' "$1"
    elif grep -q "REFUSED" "$work/r.out" && grep -qF "$3" "$work/r.out"; then
        pass "$1"
    else
        head -3 "$work/r.out"
        fail "$1 (wanted a refusal containing \"$3\")"
    fi
}

refuse 'closing a window that is not open names it, rather than doing nothing' \
    '    x = qt.close(app)' \
    'window `app` is not open, so there is nothing to close'
refuse 'qt.close takes one window' \
    '    o = qt.open(app)
    x = qt.close(app, "who")' \
    'qt.close expects one window declaration'
refuse 'and something that is not a window at all' \
    '    x = qt.close({ a: 1 })' \
    'qt.close'
refuse 'qt.is_open takes one window' \
    '    x = qt.is_open(app, 1)' \
    'qt.is_open expects one window declaration'
# `closed` IS THE WINDOW'S OWN EVENT, and this is what says the special case is
# NARROW rather than a word that now works anywhere: a button has no close
# worth reporting, so `on save.closed` falls through to the by-name signal
# route and gets ITS refusal -- naming the widget class, which is the sentence
# a reader needs. It does not use the helper above because the handler has to
# be IN THE DECLARATION, and putting it in the shared template would make every
# case above refuse for this reason instead of its own.
cat > "$work/part_closed.bas" <<'BAS'
window app( title: "R", width: 200, height: 100 )
    column( margin: 6 )
        button save( label: "Save" )
    end column
    on save.closed
        print "never"
    end on
end window
program main(args)
    if not qt.can_display() then
        print "NO DISPLAY"
        return
    end if
    on error goto refused
    o = qt.open(app)
    print "ACCEPTED"
    return
refused:
    print "REFUSED " + error.message
    error.clear()
end program
BAS
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 40 ./gbasic \
    --line-buffered "$work/part_closed.bas" > "$work/part_closed.out" 2>&1
set -e
if grep -q "NO DISPLAY" "$work/part_closed.out"; then
    printf '  SKIP a part has no close (no display)\n'
elif grep -q 'emits no .closed. signal' "$work/part_closed.out"; then
    pass 'a part has no `closed` -- the special case is the window only'
else
    head -3 "$work/part_closed.out"
    fail 'a part was given a `closed` handler, so the special case is too wide'
fi

# `qt.is_open` ANSWERS where `qt.close` refuses, which is the same split
# `qt.can_display` and `qt.can_highlight` already make: a question asked IN
# ORDER TO CHOOSE must not raise, or the choice cannot be made. It is needed
# here because a window can close WITHOUT THE PROGRAM ASKING, so every other
# verb starts raising for a reason that is nobody's mistake.
printf 'TIER degrading: qt.is_open ANSWERS where the others refuse\n'
cat > "$work/ask.bas" <<'BAS'
window app( title: "Ask", width: 200, height: 100 )
    column
        field who( label: "w" )
    end column
end window
program main(args)
    print "ASKED " + string(qt.is_open(app))
end program
BAS
set +e
env -u DISPLAY -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM -u XDG_RUNTIME_DIR \
    -u XDG_SESSION_TYPE timeout -k 5 40 ./gbasic "$work/ask.bas" \
    > "$work/ask.out" 2>&1
ask_rc=$?
set -e
if [ "$ask_rc" = "0" ] && grep -q "^ASKED false" "$work/ask.out"; then
    pass "with no display at all it answers false without raising"
else
    cat "$work/ask.out"; fail "qt.is_open did not answer with no display (exit $ask_rc)"
fi

# ---------------------------------------------------------------------- GROWTH
# REOPENING IS A NEW SHAPE, not a faster old one: before this a second
# `qt.open` met "already open", so nothing could open and close a window in a
# loop -- and the obvious implementation grows for ever, one node per part per
# open, plus a widget each time a close is noticed where it cannot be deleted.
#
# A RATIO ACROSS A 10x CHANGE IN CYCLES, never an absolute size, so it measures
# the same under any load and on any machine. MEASURED on this host:
#
#              200 cycles   2000 cycles   ratio
#   retired      41.9 MB      42.3 MB     1.01
#   not retired  45.1 MB      81.6 MB     1.81
#
# so the gate at 1.25 separates them with room on both sides. Half the cycles
# close by the verb and half by the event, because the two retire at different
# moments and a fixture exercising one would leave the other unmeasured.
printf 'TIER growth: opening and closing 2000 times does not grow\n'
if ! premise_rss_meter; then
    # NAME THE CLAIM THAT WENT UNMEASURED: a program cannot watch its own peak
    # resident size, so without a meter outside it nothing checks that
    # reopening is bounded.
    printf '  SKIP (/usr/bin/time is absent, so the BOUNDED-REOPEN claim was NOT measured)\n'
else
    set +e
    small=$(QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib /usr/bin/time -f '%M' \
        timeout -k 5 300 ./gbasic tests/qt_lifecycle_growth_fixture.bas 200 \
        2>"$work/s.rss"); s_rc=$?
    big=$(QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib /usr/bin/time -f '%M' \
        timeout -k 5 300 ./gbasic tests/qt_lifecycle_growth_fixture.bas 2000 \
        2>"$work/b.rss"); b_rc=$?
    set -e
    s_rss=$(tail -1 "$work/s.rss"); b_rss=$(tail -1 "$work/b.rss")
    if printf '%s' "$small" | grep -q "SKIP no display"; then
        printf '  SKIP (no display -- reopening was NOT measured)\n'
    elif [ "$s_rc" != "0" ] || [ "$b_rc" != "0" ]; then
        cat "$work/s.rss" "$work/b.rss"
        fail "a growth run did not finish ($s_rc / $b_rc)"
    elif [ "$small" != "CYCLES 200" ] || [ "$big" != "CYCLES 2000" ]; then
        printf '%s / %s\n' "$small" "$big"
        fail "the growth fixture did not run the cycles it was asked for"
    else
        ratio=$(awk -v a="$s_rss" -v b="$b_rss" 'BEGIN{ printf "%.2f", (a>0)?b/a:99 }')
        ok=$(awk -v r="$ratio" 'BEGIN{ print (r <= 1.25) ? "yes" : "no" }')
        if [ "$ok" = "yes" ]; then
            pass "2000 cycles cost ${ratio}x what 200 did (${s_rss}kB -> ${b_rss}kB, gate 1.25)"
        else
            fail "2000 cycles cost ${ratio}x what 200 did (${s_rss}kB -> ${b_rss}kB, gate 1.25) -- closed windows are not being retired"
        fi
    fi
fi

# ------------------------------------------------------------------ VALGRIND
# ACCESS-ONLY, for the reason run_qt_backend.sh records: Qt does not free its
# platform plugin, font configuration or QApplication statics, so a LEAK claim
# would be a suppression file describing Qt rather than gBASIC. The JIT is off
# because PCRE2's regex JIT over-reads its subject by design (§0.14).
#
# IT IS THE TIER THIS INCREMENT MOST NEEDS, because what is new here is a
# DELETE: a window's widget is destroyed while gBASIC still holds the record it
# was built from, and its nodes are dropped from a list the teardown also
# walks. A wrong answer is not the failure mode available here.
printf 'TIER valgrind: no invalid access across close, reopen and teardown\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access across a close was NOT measured)\n'
else
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_lifecycle_test.bas \
        >/dev/null 2>"$work/vg.err"
    vg_rc=$?
    set -e
    if [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    else
        pass "no invalid access across seven closes (five by the verb, two by an invoked close), seven opens and teardown"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_lifecycle: all checks passed\n'
else
    printf 'run_qt_lifecycle: FAILED\n'
fi
exit "$status"
