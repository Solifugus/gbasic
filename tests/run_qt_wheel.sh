#!/usr/bin/env bash
# THE WHEEL (docs/qt_gui_design.md §0.21) -- `on <part>.wheel`, `qt.wheel`,
# `qt.swallow`, and `qt.scroll` to drive it.
#
# `set +e` BELOW WITHOUT A MATCHING `set -e`, DELIBERATELY: this file's mode is
# `set -uo pipefail` -- no `-e` -- so `set +e` is a no-op and a `set -e` after
# it would TURN THE MODE ON, which costs nothing until a suite goes red and
# then kills it mid-tier with no verdict. run_portable.sh tripwires it.
#
# DERIVED FROM gBASIC STUDIO'S OWN SOURCE, which uses a
# Gtk.EventControllerScroll for exactly ONE thing: Ctrl + wheel zooms the
# editor. Its handler reads `dy`, calls one function and returns TRUE to mean
# handled, and those three things are the whole requirement.
#
# FIVE THINGS MEASURED IN C++ FIRST AND THREE SHRANK THE INCREMENT:
#
#   a synthetic QWheelEvent IS        a plain one moved the scrollbar 92 -> 95,
#   delivered                         so unlike GTK this is DRIVABLE
#   QPlainTextEdit does NOT zoom      font stayed 9.00 through Ctrl+wheel, so
#   on Ctrl+wheel                     the gap is real
#   Ctrl+wheel scrolls FASTER         95 -> 111 against 92 -> 95 plain, so a
#   than a plain one                  handler that does not claim the event
#                                     leaves the editor jumping while it zooms
#   an event filter CAN swallow it    102 -> 102 eaten, 102 -> 105 declining
#   `font` is Q_PROPERTY 27 and       so the ZOOM NEEDS NO VERB: split the
#   reads back as TEXT                text, change the number, write it back
#
# SO THERE IS NO `qt.zoom`, and that is a measurement rather than a deferral:
# `zoomIn`/`zoomOut` take a defaulted argument so `qt.invoke` (no-argument form
# only) cannot reach them, and nothing needs them to -- the fixture does the
# zoom in gBASIC arithmetic and asserts the font moved. SIXTH TIME in this
# backend that measuring a gap before filling it found it closed.
#
# THE TIER TO READ FIRST IS CLAIMED, because it is the only thing that makes
# `qt.swallow` assertable at all: a QPlainTextEdit has NO scroll position a
# program can read (`verticalScrollBar` is a method, not a property), so
# `qt.scroll` answers whether anything CLAIMED the event -- the shape
# `qt.popup` already has. Asserted as a DIFFERENCE, with the control that a
# part which declared no handler claims nothing, without which "it swallows" is
# satisfied by a backend that swallows everywhere and costs every `source` and
# `table` in the language its ordinary scrolling.
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

# ------------------------------------------------------------------ GRAMMAR
# `on code.wheel` IS THE HANDLER PRODUCTION THAT ALREADY EXISTS, so this
# increment adds no grammar -- asserted as a standing tier, which is not
# decoration in a project that rejected `IDENT expression` as a statement form
# over FOUR measured conflicts.
printf 'TIER grammar: bison still reports zero conflicts\n'
if ! command -v bison >/dev/null 2>&1; then
    printf '  SKIP (no bison -- the grammar cost was NOT measured)\n'
else
    bison -d src/parser.y -o "$work/p.c" 2>"$work/bison.err" || true
    sr=$(grep -cE "shift/reduce" "$work/bison.err" || true)
    rr=$(grep -cE "reduce/reduce" "$work/bison.err" || true)
    if [ "${sr:-0}" = "0" ] && [ "${rr:-0}" = "0" ]; then
        pass "zero shift/reduce and zero reduce/reduce"
    else
        head -4 "$work/bison.err"
        fail "the grammar now reports conflicts"
    fi
fi

# ------------------------------------------------------------- NO ZOOM VERB
# A TRIPWIRE ON A DECISION, because the decision is invisible in behaviour: the
# zoom works today and a `qt.zoom` added later would ALSO work, so nothing
# would go red. What it would cost is a verb that only an editor wants, for an
# arithmetic a program can already do -- and `zoomIn`/`zoomOut` cannot be
# reached by `qt.invoke` anyway, so the verb would have to carry its own
# argument marshalling.
printf 'TIER no zoom verb: the font route is the only one\n'
zoomverbs=$(grep -c 'strcmp(verb, "zoom' src/modules/qt.c || true)
if [ "${zoomverbs:-0}" = "0" ]; then
    pass "no qt.zoom -- the zoom is gBASIC arithmetic over the font property"
else
    fail "a zoom verb appeared ($zoomverbs); §0.21 measured that the font property already does it, so this needs re-arguing rather than re-adding"
fi
# THE VACUITY CONTROL: a scanner that read nothing passes the same way.
printf 'strcmp(verb, "zoom_in") == 0\n' > "$work/planted.c"
if [ "$(grep -c 'strcmp(verb, "zoom' "$work/planted.c")" != "0" ]; then
    pass "CONTROL: a planted zoom verb is counted"
else
    fail "CONTROL: the scan cannot see a zoom verb, so it is measuring nothing"
fi

# -------------------------------------------------------------- DECLARATION
# RUNS WHETHER OR NOT QT IS IN THE BUILD: a claim about the DECLARATION.
printf 'TIER declaration: `wheel` is a handler like any other\n'
decl() {                        # name, window body, expected fragment ("" = accept)
    cat > "$work/d.bas" <<BAS
window app( title: "D", width: 300, height: 200 )
$2
end window
program main(args)
    print "RAN"
end program
BAS
    set +e
    ./gbasic "$work/d.bas" > "$work/d.out" 2>&1
    if [ -n "$3" ]; then
        if grep -qF "$3" "$work/d.out" && ! grep -q RAN "$work/d.out"; then
            pass "$1"
        else
            head -2 "$work/d.out"
            fail "$1 (wanted a refusal containing \"$3\")"
        fi
    else
        if grep -q RAN "$work/d.out"; then
            pass "$1"
        else
            head -2 "$work/d.out"
            fail "$1 (expected it to be accepted)"
        fi
    fi
}
decl 'a wheel handler on a real part is accepted' \
'    column
        source code( plainText: "x" )
    end column
    on code.wheel
        print "turned"
    end on' \
    ''
# THE REFUSAL LOAD-TIME VALIDATION EXISTS FOR: a handler that can NEVER fire,
# and nothing at run time would ever say so.
decl 'a wheel handler naming no part is refused' \
'    column
        source code( plainText: "x" )
    end column
    on nobody.wheel
        print "turned"
    end on' \
    'nobody'

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: direction, modifiers, the zoom and the claim\n'
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic \
    --line-buffered tests/qt_wheel_test.bas > "$work/sem.out" 2> "$work/sem.err"
rc=$?
if grep -q "SKIP no display" "$work/sem.out"; then
    printf '  SKIP (no display -- the wheel was NOT exercised)\n'
elif [ "$rc" != "0" ]; then
    grep MISMATCH "$work/sem.out" | head -4
    tail -3 "$work/sem.out"; tail -3 "$work/sem.err"
    fail "the fixture exited $rc"
else
    checks=$(grep -c '^ok' "$work/sem.out" || true)
    bad=$(grep -c 'MISMATCH' "$work/sem.out" || true)
    if [ "$bad" = "0" ] && [ "${checks:-0}" -ge 24 ]; then
        pass "$checks checks, 0 mismatches"
    else
        grep MISMATCH "$work/sem.out" | head -6
        fail "$checks checks, $bad mismatches (want >= 24 and none)"
    fi
    # Qt's offscreen plugin says these on every run and they are not ours.
    grep -v 'This plugin does not support' "$work/sem.err" > "$work/sem.err2" || true
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing of its own on stderr"
    fi
    # NAMED, so a failure says which claim went rather than only moving a count.
    if grep -q "^ok   and the event was claimed" "$work/sem.out" &&
       grep -q "^ok   and a declined wheel was NOT claimed" "$work/sem.out"; then
        pass "CLAIMED: swallowing and declining, asserted as a difference"
    else
        fail "qt.swallow was not measured both ways -- a backend that swallows everything costs every source and table its ordinary scrolling"
    fi
    if grep -q "^ok   a part with no handler claims nothing" "$work/sem.out" &&
       grep -q "^ok   an undeclared part in a window that has one claims nothing" "$work/sem.out"; then
        pass "CONTROL: the filter follows the declared handler, not the window"
    else
        fail "the filter's reach was not measured -- one installed per window would swallow on parts that declared nothing"
    fi
    if grep -q "^ok   the font grew by two points" "$work/sem.out" &&
       grep -q "^ok   and shrank back" "$work/sem.out"; then
        pass "ZOOM: done in gBASIC arithmetic, asserted on the font"
    else
        fail "the zoom was not measured on the font -- a handler that ran and set nothing reads identically to one that worked"
    fi
    if grep -q "^ok   one notch up is a positive dy" "$work/sem.out" &&
       grep -q "^ok   one notch down is a negative dy" "$work/sem.out"; then
        pass "DIRECTION: both signs, so a zoom cannot go the wrong way"
    else
        fail "the direction was not measured both ways -- a sign error zooms out when a person meant in"
    fi
    if grep -q "^ok   shift is reported" "$work/sem.out" &&
       grep -q "^ok   and shift is not ctrl" "$work/sem.out"; then
        pass "MODIFIERS: read rather than assumed"
    else
        fail "the modifiers were not separated -- a backend reporting ctrl for any modifier makes every wheel a Ctrl+wheel"
    fi
fi

# ------------------------------------------------------------------ VALGRIND
# ACCESS-ONLY, for the reason run_qt_backend.sh records. What is NEW here is an
# event filter that READS an event and a per-dispatch record of which event is
# in flight, which `qt.wheel` reads back from gBASIC.
printf 'TIER valgrind: no invalid access across the filter and the reads\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_wheel_test.bas \
        >/dev/null 2>"$work/vg.err"
    vg_rc=$?
    if [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    else
        pass "no invalid access across the wheel filter and the event reads"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_wheel: all checks passed\n'
else
    printf 'run_qt_wheel: FAILED\n'
fi
exit "$status"
