#!/usr/bin/env bash
# THE QT BACKEND -- the increment that RENDERS a `window` declaration.
#
# run_window.sh proves the declarative half with no Qt at all: a block that
# parses, validates, refuses and binds an introspectable record, headless on
# every platform. That was the `web.routes` bet -- a route table proven with no
# socket -- and it held. This suite is the other half, and it is where headless
# stops being free.
#
# THE ONE THING THAT MUST BE TRUE BEFORE ANY OF THE REST, and it is the tier to
# read first (docs/qt_gui_design.md §0.3): WITH NO DISPLAY, QT CALLS qFatal AND
# ABORTS. Measured: SIGABRT, exit 134, core dumped. `qFatal` cannot be caught,
# so by the time the process knows it is gone -- and a crash takes
# block-buffered stdout with it, so a program printing its progress prints
# NOTHING and the failure reads as line 1. Exactly the defect GTK produced and
# run_gi.sh records (a widget built before gtk_init, exit 139). So the refusal
# asks QT ITSELF IN A FORKED CHILD, a process we can afford to lose, because
# the environment cannot be read instead -- measured, Qt found Wayland through
# XDG_RUNTIME_DIR with WAYLAND_DISPLAY unset, so its own detection is richer
# than any reimplementation and would drift as Qt changes.
#
# SELF-CHECKING, NOT GOLDEN, AND HERE THAT IS FORCED. Every defect this backend
# can have produces an ORDINARY-LOOKING RUN: `setProperty` on a name a widget
# does not carry SUCCEEDS, storing a dynamic property nothing reads, so an
# option silently does nothing; a handler connected to the wrong signal never
# fires and nothing says so; a value written to the wrong part is still a
# window with a value in it. None of them raises, so a golden would record the
# damaged run as expected and defend it.
#
# Tiers:
#   NO DISPLAY   the sentence, the survival, and the stdout a crash would have
#                taken -- plus the CATCHABLE form, so a program can degrade to
#                a text interface rather than ending.
#   OFFSCREEN    real Qt widgets with no display: construction, properties set
#                at build time and READ BACK THROUGH QT, values in and out, a
#                method invoked by name, and a real signal reaching gBASIC.
#   LOOP         DIRECTION A IN THE PRODUCT. Qt owns the thread and a webserver
#                is still answered -- the claim §5.1 measured in isolation,
#                made here by the interpreter, which is the only place it can
#                be wrong for a reason the probe could not see.
#   REFUSALS     a property no widget carries, a word with no widget behind it,
#                a method that does not exist -- each refused BY NAME, each
#                beside a legal neighbour.
#   BUILD        a build without Qt refuses in the convention's own words, and
#                `qt.can_display()` still ANSWERS rather than raising, which is
#                what lets a program degrade gracefully.
#
# WHAT IS NOT ASSERTED HERE AND SAYS SO: valgrind. Qt's platform plugins,
# font configuration and QApplication statics are not freed by design, so a
# leak tier over this would be a suppression file describing Qt rather than a
# check on us -- and run_valgrind_policy.sh's whole argument is that an
# access-only tier must not become "valgrind is off". The gBASIC-side
# allocations this increment adds (the handler shells and their names) ARE
# covered, by run_window.sh's existing valgrind tier, which parses every window
# fixture without opening one.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."
source tests/build_has.sh
make >/dev/null

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# NO DISPLAY AT ALL, the recipe run_qt_loop.sh established: Qt's detection
# reaches further than DISPLAY, so every door has to be shut.
nodisplay() {
    env -u DISPLAY -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM -u XDG_RUNTIME_DIR \
        -u XDG_SESSION_TYPE "$@"
}

if ! build_has qt; then
    printf 'SKIP run_qt_backend (no Qt in this build -- NOTHING about rendering a window was measured)\n'
    exit 0
fi

# ---------------------------------------------------------------- NO DISPLAY
printf 'TIER no display: a sentence, and the program survives to print it\n'
set +e
nodisplay timeout -k 5 60 ./gbasic --line-buffered tests/qt_nodisplay_test.bas \
    > "$work/nd.out" 2> "$work/nd.err"
nd_rc=$?
set -e
if [ "$nd_rc" = "0" ]; then
    pass "a caught refusal leaves the program running and exiting 0"
else
    printf '    %s\n' "$(tail -3 "$work/nd.err")"
    fail "the program exited $nd_rc (134 = the abort this exists to prevent)"
fi
if grep -q '^REFUSED: ' "$work/nd.out"; then
    pass "the refusal is CATCHABLE, so a program can fall back to a text interface"
else
    fail "no catchable refusal: $(tr '\n' '|' < "$work/nd.out")"
fi
# THE MESSAGE NAMES WHAT IS MISSING -- a display -- rather than reporting a Qt
# plugin failure, which names the one thing the author cannot fix.
if grep -q '^REFUSED: .*no display' "$work/nd.out"; then
    pass "and it names a display, not a Qt plugin"
else
    fail "the refusal does not name a display: $(grep '^REFUSED' "$work/nd.out" || true)"
fi
# THE HALF A CRASH TAKES AWAY. Asserted separately because the refusal above is
# satisfied by a build that refuses and loses everything printed before it.
if grep -qx 'progress line one' "$work/nd.out" &&
   grep -qx 'progress line two' "$work/nd.out" &&
   grep -qx 'still here, and can say so' "$work/nd.out"; then
    pass "every line printed before AND after the attempt arrived"
else
    fail "output around the refusal was lost: $(tr '\n' '|' < "$work/nd.out")"
fi
if grep -q MISMATCH "$work/nd.out"; then
    fail "$(grep MISMATCH "$work/nd.out" | head -1)"
fi

printf 'TIER no display, uncaught: an ordinary runtime error, never a signal\n'
set +e
nodisplay timeout -k 5 60 ./gbasic --line-buffered tests/qt_nodisplay_fatal_test.bas \
    > "$work/ndf.out" 2> "$work/ndf.err"
ndf_rc=$?
set -e
# 134 IS THE ABORT, and anything above 128 is a death by signal -- which is the
# outcome §0.3 exists to replace. A plain nonzero exit is what a gBASIC runtime
# error gives.
if [ "$ndf_rc" -gt 0 ] && [ "$ndf_rc" -lt 128 ]; then
    pass "it exits $ndf_rc -- a refusal, not a signal"
else
    fail "exit $ndf_rc (over 128 means a signal; 134 is the abort itself)"
fi
if grep -qE 'runtime error at .*:[0-9]+:[0-9]+' "$work/ndf.err"; then
    pass "and the diagnostic is LOCATED, so it points at the line that asked"
else
    fail "unlocated diagnostic: $(tr '\n' '|' < "$work/ndf.err")"
fi
if grep -qx 'progress line one' "$work/ndf.out"; then
    pass "the program's own output survived, which a crash would have swallowed"
else
    fail "stdout before the failure was lost -- the crash signature"
fi
if grep -q MISMATCH "$work/ndf.out"; then
    fail "execution continued past the refusal"
fi

# ----------------------------------------------------------------- OFFSCREEN
printf 'TIER offscreen: real widgets, real properties, a real signal\n'
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 120 ./gbasic --line-buffered \
    tests/qt_window_test.bas > "$work/w.out" 2> "$work/w.err"
w_rc=$?
set -e
if [ "$w_rc" != "0" ]; then
    printf '    %s\n' "$(tail -5 "$work/w.err")"
    fail "the fixture exited $w_rc"
fi
checks=$(grep -c '^ok   ' "$work/w.out" || true)
if grep -q MISMATCH "$work/w.out"; then
    grep MISMATCH "$work/w.out" | while read -r line; do printf '    %s\n' "$line"; done
    fail "$(grep -c MISMATCH "$work/w.out") mismatches in the fixture"
else
    pass "$checks checks, 0 mismatches"
fi
# A COVERAGE FLOOR, because a fixture that stopped running its checks passes
# the line above by saying nothing.
if [ "${checks:-0}" -ge 12 ]; then
    pass "and it really ran them ($checks >= 12)"
else
    fail "only $checks checks ran -- the fixture stopped early"
fi
# THE SIGNAL IS THE LOAD-BEARING LINE and is asserted on its own: every check
# above passes on a backend that connected nothing, because nothing in the
# fixture's state depends on the handler having run.
if grep -qx 'SIGNAL read -> Ada Lovelace' "$work/w.out"; then
    pass "a real Qt signal reached a gBASIC handler, which read the live widget"
else
    fail "the handler did not run: $(grep SIGNAL "$work/w.out" || echo '(nothing)')"
fi
# AND IT RAN WHERE THE DECLARATION PUT IT, not at open: a handler fired during
# construction would be the registration-time firing `watch` has, which here
# would mean every button acting before anybody pressed one.
# ASSERTED ON THE HANDLER'S FIRST LINE, which needs nothing but the handler
# having run. Against the line that READS the widget this control could not
# fail: a handler fired at connect time raises inside `qt.read` -- the window
# is not registered yet -- so it prints nothing and the ordering still looks
# right. Measured, by the perturbation written for this check coming back green.
if [ "$(grep -n 'SIGNAL save.clicked' "$work/w.out" | head -1 | cut -d: -f1)" -gt \
     "$(grep -n '^opened' "$work/w.out" | cut -d: -f1)" ]; then
    pass "CONTROL: it fired when the button was clicked and not when the window opened"
else
    fail "the handler fired at the wrong moment"
fi
for want in "refused a part that does not exist" \
            "refused a method that does not exist" \
            "refused opening the same window twice"; do
    if grep -q "^$want" "$work/w.out"; then
        pass "$want"
    else
        fail "$want -- no refusal"
    fi
done

# ---------------------------------------------------------------- REFUSALS
printf 'TIER refusals: by name, and only for what is really wrong\n'
# A PROPERTY NO WIDGET CARRIES. This is the division of labour the window
# block's own validation leaves open ON PURPOSE (run_window.sh says so): a
# LEAF's options are widget properties and Qt exposes 75 on a button alone, so
# that set cannot be closed without Qt in the build. Here it can be, and is.
cat > "$work/badprop.bas" <<'EOF'
window w( title: "W" )
    column
        button go( texx: "Go" )
    end column
end window
program main()
    on error goto next
    x = qt.open(w)
    if error then
        print "REFUSED: " + error.message
    else
        print "MISMATCH an unknown property was accepted"
    end if
end program
EOF
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 60 ./gbasic "$work/badprop.bas" > "$work/bp.out" 2>&1
set -e
# THE WORDING IS THE ASSERTION, not merely that it refused. Qt's own
# `setProperty` returns false for a property the metaobject does not declare,
# so a backend WITHOUT the metaobject check still refuses -- and says "would
# not take that value", which is FALSE: the value was fine, the name was not.
# That is the reports-the-wrong-cause class, and it is the whole reason the
# check exists. Measured: with the metaobject lookup removed, every other
# check in this file stays green.
if grep -q 'REFUSED: .*has no `texx` property' "$work/bp.out"; then
    pass "a property the widget does not carry is refused, NAMED, and said to be absent"
else
    fail "the refusal does not name texx as absent: $(tr '\n' '|' < "$work/bp.out")"
fi
# THE CONTROL. Without it the rule above is satisfied by a backend that refuses
# every option there is, which would make the whole by-name route useless.
#
# AND IT USED TO ASSERT ONLY `opened: true`, WHICH WAS TOO WEAK AND SAID SO BY
# ITS OWN DESIGN: accepting an option and ACTING ON IT are two claims, and
# nothing here could see the second -- so a backend that took every option and
# applied none passed this tier, which is the one tier arguing that the by-name
# route works. `qt.read(win, part, property)` is the read half of that route and
# is what closes it; both properties are now read back.
cat > "$work/goodprop.bas" <<'EOF'
window w( title: "W" )
    column
        button go( text: "Go", enabled: false )
    end column
end window
program main()
    print "opened: " + string(qt.open(w))
    print "text took effect: " + qt.read(w, "go", "text")
    print "enabled took effect: " + string(qt.read(w, "go", "enabled"))
end program
EOF
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 60 ./gbasic "$work/goodprop.bas" > "$work/gp.out" 2>&1
set -e
if grep -qx 'opened: true' "$work/gp.out"; then
    pass "CONTROL: two real Qt properties on the same widget are accepted"
else
    fail "a legal option was refused: $(tr '\n' '|' < "$work/gp.out")"
fi
# THE TWO HALVES ARE SEPARATE CHECKS because they fail for different reasons: a
# text property that did not take leaves the button captionless, and a BOOLEAN
# that did not take leaves it enabled -- and `enabled` is the one that would be
# invisible, since the default is already true.
if grep -qx 'text took effect: Go' "$work/gp.out"; then
    pass "a text option reached the widget, read back by name"
else
    fail "text: did not reach the widget: $(tr '\n' '|' < "$work/gp.out")"
fi
if grep -qx 'enabled took effect: false' "$work/gp.out"; then
    pass "a boolean option reached the widget, read back by name"
else
    fail "enabled: false did not reach the widget: $(tr '\n' '|' < "$work/gp.out")"
fi

# ------------------------------------------------------------------ PROPERTIES

printf 'TIER properties: read back in the KIND Qt declares, not as text\n'
# THE KIND IS THE LOAD-BEARING PART and it is not a preference. Every non-empty
# string is TRUE in this language, so a boolean answered as the text "false"
# reads BACKWARDS in the one place a property is most likely to be used --
# `if qt.read(f, "readOnly")`. The kind therefore comes from the QVariant, and
# this tier asserts `type()` and not only the value, because `string(false)` and
# the text "false" are the same characters and a value check cannot tell them
# apart -- the same trap the finio OFX adapter's amounts hid behind.
cat > "$work/props.bas" <<'EOF'
window p( title: "P" )
    column
        field name( label: "Name", placeholderText: "type here", maxLength: 40 )
        check live( label: "Live", checked: true )
    end column
end window
program main()
    x = qt.open(p)
    print "text: " + qt.read(p, "name", "placeholderText")
    print "number: " + string(qt.read(p, "name", "maxLength")) + " / " + type(qt.read(p, "name", "maxLength"))
    print "bool: " + string(qt.read(p, "name", "readOnly")) + " / " + type(qt.read(p, "name", "readOnly"))
    print "checked: " + string(qt.read(p, "live", "checked")) + " / " + type(qt.read(p, "live", "checked"))
    on error goto next
    y = qt.read(p, "name", "texx")
    if error then
        print "BADPROP: " + error.message
        error.clear()
    end if
    z = qt.read(p, "nope", "text")
    if error then
        print "BADPART: " + error.message
        error.clear()
    end if
end program
EOF
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 60 ./gbasic "$work/props.bas" > "$work/pr.out" 2>&1
set -e
if grep -qx 'text: type here' "$work/pr.out"; then
    pass "a text property reads back as text"
else
    fail "text property: $(tr '\n' '|' < "$work/pr.out")"
fi
if grep -qx 'number: 40 / number' "$work/pr.out"; then
    pass "a numeric property reads back as a NUMBER, not as its digits"
else
    fail "numeric property: $(tr '\n' '|' < "$work/pr.out")"
fi
if grep -qx 'bool: false / boolean' "$work/pr.out"; then
    pass "a boolean property reads back as a BOOLEAN -- the kind that would read backwards as text"
else
    fail "boolean property: $(tr '\n' '|' < "$work/pr.out")"
fi
if grep -qx 'checked: true / boolean' "$work/pr.out"; then
    pass "a checkbox's own state reads back, so the two boolean answers are distinguished"
else
    fail "checked: $(tr '\n' '|' < "$work/pr.out")"
fi
# THE READ HALF MUST REFUSE WHAT THE WRITE HALF REFUSES. A reader answering
# `unknown` for a misspelled property would report a MISTAKE as an ABSENCE, and
# the two halves of one route would disagree about what a property name is.
if grep -q 'BADPROP: .*has no `texx` property' "$work/pr.out"; then
    pass "an unknown property is refused on the READ side too, named"
else
    fail "a misspelled property was not refused on read: $(tr '\n' '|' < "$work/pr.out")"
fi
# AND A MISSING PART IS A DIFFERENT SENTENCE from a missing property, or the
# message names the wrong one of two things the author can mistype.
if grep -q 'BADPART: .*has no `nope`' "$work/pr.out"; then
    pass "an absent PART is refused in its own words, not as a missing property"
else
    fail "an absent part was not distinguished: $(tr '\n' '|' < "$work/pr.out")"
fi

# --------------------------------------------------------------------- LOOP
printf 'TIER loop: Qt owns the thread and gBASIC is still serviced\n'
if ! command -v curl >/dev/null 2>&1; then
    printf '  SKIP (no curl -- direction A was NOT measured through the interpreter)\n'
else
    set +e
    QT_QPA_PLATFORM=offscreen PORT=0 timeout -k 5 90 ./gbasic --line-buffered \
        tests/qt_loop_fixture.bas > "$work/loop.out" 2> "$work/loop.err" &
    loop_pid=$!
    port=""
    for _ in $(seq 1 200); do
        port=$(sed -n 's/^listening on //p' "$work/loop.out" | head -1)
        [ -n "$port" ] && break
        sleep 0.1
    done
    set -e
    if [ -z "$port" ]; then
        fail "the fixture never announced a port: $(tail -3 "$work/loop.err")"
        kill "$loop_pid" 2>/dev/null || true
    else
        # THE WINDOW MUST BE OPEN AND THE LOOP RUNNING before the request, or
        # this tier would be measuring the ordinary post-main loop and nothing
        # about Qt at all.
        for _ in $(seq 1 200); do
            grep -qx 'window open' "$work/loop.out" && break
            sleep 0.1
        done
        set +e
        body=$(curl -s --max-time 10 "http://127.0.0.1:$port/hello")
        set -e
        if [ "$body" = "served while Qt owned the loop" ]; then
            pass "a webserver answered while QApplication::exec() held the thread"
        else
            fail "no answer from the server under Qt's loop (got '$body')"
        fi
        set +e
        curl -s --max-time 10 "http://127.0.0.1:$port/quit" >/dev/null
        wait "$loop_pid"
        loop_rc=$?
        set -e
        if [ "$loop_rc" = "0" ] && grep -qx 'loop ended' "$work/loop.out"; then
            pass "and qt.stop() from inside a handler ends the loop cleanly"
        else
            printf '    %s\n' "$(tail -3 "$work/loop.err")"
            fail "the loop did not end cleanly (exit $loop_rc)"
        fi
    fi
fi

# -------------------------------------------------------------------- BUILD
printf 'TIER degrading: `qt.can_display` ANSWERS where everything else refuses\n'
# THE SHAPE `has_builtin` ALREADY HAS, and the reason is the same: a program
# asking whether it may open a window is degrading gracefully, and "this build
# has no Qt" is one of the answers to that question rather than an error.
cat > "$work/ask.bas" <<'EOF'
program main()
    print "can_display: " + string(qt.can_display())
end program
EOF
set +e
nodisplay timeout -k 5 60 ./gbasic "$work/ask.bas" > "$work/ask.out" 2>&1
ask_rc=$?
set -e
if [ "$ask_rc" = "0" ] && grep -qx 'can_display: false' "$work/ask.out"; then
    pass "with no display it answers false without raising"
else
    fail "qt.can_display did not answer (exit $ask_rc): $(tr '\n' '|' < "$work/ask.out")"
fi
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 60 ./gbasic "$work/ask.bas" > "$work/ask2.out" 2>&1
set -e
if grep -qx 'can_display: true' "$work/ask2.out"; then
    pass "CONTROL: under the offscreen platform it answers true"
else
    fail "offscreen did not answer true: $(tr '\n' '|' < "$work/ask2.out")"
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_backend: all checks passed\n'
else
    printf 'run_qt_backend: FAILURES\n'
fi
exit "$status"
