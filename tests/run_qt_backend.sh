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
#   VALGRIND     ACCESS ONLY, and the split is the whole reason there is a tier
#                here at all. Qt's platform plugins, font configuration and
#                QApplication statics are not freed by design, so a LEAK claim
#                over an opened window would be a suppression file describing
#                Qt rather than a check on us -- which is run_odbc's position
#                on its driver, reached for the same reason. An ACCESS claim
#                costs nothing and is worth making, and run_valgrind_policy.sh
#                pins the one axis the two policies differ on, so access-only
#                cannot quietly become "valgrind is off".
#
# A NOTE HERE USED TO SAY the gBASIC-side allocations were covered by
# run_window.sh's valgrind tier, "which parses every window fixture without
# opening one". THAT WAS FALSE TWICE OVER: that tier parses ONE fixture it
# generates itself, not the Qt ones -- and the allocations binding adds (a
# form's captured record, its subject and direction) happen at OPEN, which no
# parse-only tier can reach however many fixtures it reads. Measured before
# writing the tier: zero invalid accesses and zero uninitialised reads over the
# binding fixture under real Qt, so the claim is available rather than assumed.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."
source tests/build_has.sh
source tests/valgrind_tier.sh
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

# --------------------------------------------------------------------- BINDING

printf 'TIER binding: `form to show` and `form to edit` differ, and the record follows\n'
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 120 ./gbasic --line-buffered \
    tests/qt_binding_test.bas > "$work/b.out" 2> "$work/b.err"
b_rc=$?
set -e
if [ "$b_rc" != "0" ]; then
    printf '    %s\n' "$(tail -5 "$work/b.err")"
    fail "the binding fixture exited $b_rc"
fi
bchecks=$(grep -c '^ok   ' "$work/b.out" || true)
if grep -q MISMATCH "$work/b.out"; then
    grep MISMATCH "$work/b.out" | while read -r line; do printf '    %s\n' "$line"; done
    fail "$(grep -c MISMATCH "$work/b.out") mismatches in the binding fixture"
elif [ "${bchecks:-0}" = "0" ]; then
    # NEVER "0 mismatches" OVER A FIXTURE THAT RAN NOTHING. A perturbation that
    # makes the fixture DIE leaves no MISMATCH lines at all, so the plain
    # report printed `ok 0 checks, 0 mismatches` -- a green line above the two
    # FAILs that actually named the problem. Caught by the floor below either
    # way; the point is that the line itself must not read as a pass.
    fail "no binding checks ran at all, so '0 mismatches' would mean nothing"
else
    pass "$bchecks checks, 0 mismatches"
fi
# A COVERAGE FLOOR. The fixture asserts most of its own claims, so the one
# thing the runner must add is that it really ran them -- a fixture that
# stopped early satisfies "0 mismatches" by saying nothing.
if [ "${bchecks:-0}" -ge 30 ]; then
    pass "and it really ran them ($bchecks >= 30)"
else
    fail "only $bchecks binding checks ran -- the fixture stopped early"
fi
# THE FOUR REFUSALS ARE PINNED HERE RATHER THAN IN THE FIXTURE, because each is
# a SENTENCE and the fixture can only say whether something raised. A refusal
# that fired with the wrong words is the reports-the-wrong-cause class, which
# this backend has already produced once.
#
# THE SUBJECT MUST EXIST, and the message has to name the FORM: `env_get` would
# say `undefined variable: nowhere`, which is true and says nothing about the
# declaration that wanted it or about where qt.open looks.
if grep -q 'NOSUBJECT: window `orphan`: `form to edit nowhere` found no `nowhere`' "$work/b.out"; then
    pass "a form bound to a name that is not in scope is refused, naming the form"
else
    fail "the absent-subject refusal: $(grep NOSUBJECT "$work/b.out" || echo 'never fired')"
fi
if grep -q 'NOTARECORD: .*needs a record, and `astring` holds string' "$work/b.out"; then
    pass "a subject that is not a record is refused, naming what it holds instead"
else
    fail "the wrong-kind refusal: $(grep NOTARECORD "$work/b.out" || echo 'never fired')"
fi
if grep -q 'NOSUCHFORM: window `editor` has no form bound to `nosuchsubject`' "$work/b.out"; then
    pass "qt.subject on a name no form binds is refused"
else
    fail "the unbound-name refusal: $(grep NOSUCHFORM "$work/b.out" || echo 'never fired')"
fi
# A `type:` MUST REFUSE WHAT IS NOT OF THAT TYPE. The sentence is the
# MODIFIER'S OWN and is the cause, so it propagates untouched -- PLAT-ERR's
# first-raise-wins rule exists because replacing a cause with a symptom is how
# a diagnostic comes to name the one thing that is not at fault.
#
# WHAT IT DOES NOT SAY IS ASSERTED AS A KNOWN COST rather than left to be
# discovered: it does not name the PART, so with several `type:` fields in one
# form the author must work out which. Naming it means adding context to a
# raise already in flight, which nothing here does and which can make an error
# VANISH if done wrongly, so it waits for a helper with its own tier.
if grep -q 'BADVALUE: money text is not a number' "$work/b.out"; then
    pass "text that is not money is refused by the modifier's own sentence"
else
    fail "the bad-value refusal: $(grep BADVALUE "$work/b.out" || echo 'never fired')"
fi

# ----------------------------------------------------------------------- TABLE

printf 'TIER table: many rows, columns from the declaration, and a chosen row\n'
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 120 ./gbasic --line-buffered \
    tests/qt_table_test.bas > "$work/t.out" 2> "$work/t.err"
t_rc=$?
set -e
if [ "$t_rc" != "0" ]; then
    printf '    %s\n' "$(tail -5 "$work/t.err")"
    fail "the table fixture exited $t_rc"
fi
tchecks=$(grep -c '^ok   ' "$work/t.out" || true)
if grep -q MISMATCH "$work/t.out"; then
    grep MISMATCH "$work/t.out" | while read -r line; do printf '    %s\n' "$line"; done
    fail "$(grep -c MISMATCH "$work/t.out") mismatches in the table fixture"
elif [ "${tchecks:-0}" = "0" ]; then
    # NEVER "0 mismatches" OVER A FIXTURE THAT RAN NOTHING. A perturbation that
    # makes the fixture DIE leaves no MISMATCH lines at all, so the plain
    # report printed `ok 0 checks, 0 mismatches` -- a green line above the two
    # FAILs that actually named the problem. Caught by the floor below either
    # way; the point is that the line itself must not read as a pass.
    fail "no table checks ran at all, so '0 mismatches' would mean nothing"
else
    pass "$tchecks checks, 0 mismatches"
fi
if [ "${tchecks:-0}" -ge 37 ]; then
    pass "and it really ran them ($tchecks >= 37)"
else
    fail "only $tchecks table checks ran -- the fixture stopped early"
fi
# A HANDLER WHOSE TARGET IS A CONTAINER. Only LEAVES registered a name before
# this, so `on orders.<signal>` on a perfectly real table was refused at load
# time as naming nothing in the window -- the refusal that exists to catch a
# handler which can never fire, firing on one that can.
#
# PROVEN BY FIRING IT, not by the window opening: `itemSelectionChanged` is a
# no-arg signal `qt.select` emits, which is why the fixture uses it rather than
# `activated` (a double-click no fixture can produce). A handler connected and
# never fired looks identical to one that worked.
if grep -qx 'SIGNAL orders.itemSelectionChanged' "$work/t.out"; then
    pass "a handler whose target is a TABLE connects and fires"
else
    fail "the table handler never fired: $(tr '\n' '|' < "$work/t.out" | tail -c 200)"
fi
# THE FIVE REFUSALS, pinned here because each is a SENTENCE and the fixture can
# only say whether something raised.
if grep -q 'NOTALIST: .*needs a list of records, and `notalist` holds record' "$work/t.out"; then
    pass "a table bound to a record is refused, naming what it holds"
else
    fail "the wrong-subject-kind refusal: $(grep NOTALIST "$work/t.out" || echo 'never fired')"
fi
# AN ELEMENT THAT IS NOT A RECORD NAMES WHICH ONE, matching what a projection
# does for the same mistake -- a table of numbers would otherwise render as a
# grid of blanks, which looks exactly like no data.
if grep -q 'BADELEMENT: .*element 1 of `mixed` is number' "$work/t.out"; then
    pass "a non-record element is refused and the INDEX is named"
else
    fail "the bad-element refusal: $(grep BADELEMENT "$work/t.out" || echo 'never fired')"
fi
if grep -q 'BADCOLUMN: qt.cell: table `orders` has no `nosuch` column' "$work/t.out"; then
    pass "a column name the table does not declare is refused"
else
    fail "the bad-column refusal: $(grep BADCOLUMN "$work/t.out" || echo 'never fired')"
fi
# A PART THAT IS NOT A TABLE IS ITS OWN SENTENCE. Answering `unknown` there
# would read as "nothing is selected", which is true of a button and is not
# what the author asked.
if grep -q 'NOTATABLE: qt.selected: `close` is not a table' "$work/t.out"; then
    pass "qt.selected on something that is not a table says so"
else
    fail "the not-a-table refusal: $(grep NOTATABLE "$work/t.out" || echo 'never fired')"
fi
if grep -q 'EMPTYNUMBER: number modifier expects a valid number string' "$work/t.out"; then
    pass "emptying a number column is refused rather than folded to zero"
else
    fail "the emptied-number refusal: $(grep EMPTYNUMBER "$work/t.out" || echo 'never fired')"
fi

# ----------------------------------------------------------------------- CELLS

printf 'TIER cells: tabular placement, asserted by asking the LAYOUT where things are\n'
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 120 ./gbasic --line-buffered \
    tests/qt_cells_test.bas > "$work/c.out" 2> "$work/c.err"
c_rc=$?
set -e
if [ "$c_rc" != "0" ]; then
    printf '    %s\n' "$(tail -5 "$work/c.err")"
    fail "the cells fixture exited $c_rc"
fi
cchecks=$(grep -c '^ok   ' "$work/c.out" || true)
if grep -q MISMATCH "$work/c.out"; then
    grep MISMATCH "$work/c.out" | while read -r line; do printf '    %s\n' "$line"; done
    fail "$(grep -c MISMATCH "$work/c.out") mismatches in the cells fixture"
elif [ "${cchecks:-0}" = "0" ]; then
    fail "no cells checks ran at all, so '0 mismatches' would mean nothing"
else
    pass "$cchecks checks, 0 mismatches"
fi
if [ "${cchecks:-0}" -ge 18 ]; then
    pass "and it really ran them ($cchecks >= 18)"
else
    fail "only $cchecks cells checks ran -- the fixture stopped early"
fi
# `qt.at` ASKS THE LAYOUT, walking up from the widget rather than from any
# record of where we put it -- a remembered position would agree with a
# placement that was wrong, which is no oracle. A part in no grid is refused
# rather than answering a position, since every number would be a lie.
if grep -q "NOTINGRID: qt.at: \`ok\` is not in a \`cells\`" "$work/c.out"; then
    pass "a part outside any cells is refused rather than given a position"
else
    fail "the not-in-a-grid refusal: $(grep NOTINGRID "$work/c.out" || echo 'never fired')"
fi

# ------------------------------------------------------------------------ TABS

printf 'TIER tabs: a book of pages, and qt.set -- the write half of the route\n'
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 120 ./gbasic --line-buffered \
    tests/qt_tabs_test.bas > "$work/tb.out" 2> "$work/tb.err"
tb_rc=$?
set -e
if [ "$tb_rc" != "0" ]; then
    printf '    %s\n' "$(tail -5 "$work/tb.err")"
    fail "the tabs fixture exited $tb_rc"
fi
tbchecks=$(grep -c '^ok   ' "$work/tb.out" || true)
if grep -q MISMATCH "$work/tb.out"; then
    grep MISMATCH "$work/tb.out" | while read -r line; do printf '    %s\n' "$line"; done
    fail "$(grep -c MISMATCH "$work/tb.out") mismatches in the tabs fixture"
elif [ "${tbchecks:-0}" = "0" ]; then
    fail "no tabs checks ran at all, so '0 mismatches' would mean nothing"
else
    pass "$tbchecks checks, 0 mismatches"
fi
if [ "${tbchecks:-0}" -ge 17 ]; then
    pass "and it really ran them ($tbchecks >= 17)"
else
    fail "only $tbchecks tabs checks ran -- the fixture stopped early"
fi
# A NAMED CONTAINER'S OWN SIGNAL, fired by SETTING A PROPERTY -- so the handler
# path for a container that is neither a leaf nor a table is driven with no
# human in front of it.
if grep -qx 'SIGNAL settings.currentChanged' "$work/tb.out"; then
    # SINGLE-QUOTED, because a backtick inside a double-quoted bash string is
    # COMMAND SUBSTITUTION: this line read `tabs` and the shell RAN the `tabs`
    # command, printing its escape sequences into the middle of a pass message.
    # Harmless here and the same trap tools/build-release-tarball.sh has
    # recorded four times -- there it would have run `make install` as a side
    # effect of a COMMENT. There is a tripwire for that file; this is a suite
    # message, which nothing scans.
    pass 'a handler on a named `tabs` connects and fires'
else
    fail "the tabs handler never fired: $(grep SIGNAL "$work/tb.out" || echo 'nothing')"
fi
# THE TWO HALVES OF ONE ROUTE MUST AGREE ABOUT WHAT A PROPERTY NAME IS. A
# setter that accepted an unknown name would store a dynamic property nothing
# reads -- an option silently doing nothing, which is the defect the closed
# head vocabulary exists to prevent one level up.
if grep -q 'BADPROP: qt.set: a QPushButton has no `texx` property' "$work/tb.out"; then
    pass "qt.set refuses an unknown property BY NAME, as the read half does"
else
    fail "the bad-property refusal: $(grep BADPROP "$work/tb.out" || echo 'never fired')"
fi
if grep -q 'BADPART: qt.set: this window has no `nope`' "$work/tb.out"; then
    pass "and an absent part in its own words"
else
    fail "the bad-part refusal: $(grep BADPART "$work/tb.out" || echo 'never fired')"
fi

# ---------------------------------------------------------------------- CHOICE

printf 'TIER choice: one of a list, through either of its two doors\n'
# DECIDED BY A REAL PROGRAM rather than Qt's catalogue: BAG's area editor gives
# every area six exits and each NAMES ANOTHER AREA, which a `field` can only
# answer by typing an id and a `table` is a grid rather than a dropdown. The
# same measurement said NOT to add a `list`, a one-column `table` already being
# a picker that `qt.selected` addresses and `qt.refresh` keeps current.
#
# AND IT CAUGHT TWO GAPS OF MINE: `qt_bindable_word` admitted only `field` and
# `check`, so a `choice` AND a `memo` rendered perfectly and bound NOT AT ALL --
# the widget plainly showed a value and the fold answered an empty string.
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 120 ./gbasic --line-buffered \
    tests/qt_choice_test.bas > "$work/ch.out" 2> "$work/ch.err"
ch_rc=$?
set -e
if [ "$ch_rc" != "0" ]; then
    printf '    %s\n' "$(tail -5 "$work/ch.err")"
    fail "the choice fixture exited $ch_rc"
fi
chchecks=$(grep -c '^ok   ' "$work/ch.out" || true)
if grep -q MISMATCH "$work/ch.out"; then
    grep MISMATCH "$work/ch.out" | while read -r line; do printf '    %s\n' "$line"; done
    fail "$(grep -c MISMATCH "$work/ch.out") mismatches in the choice fixture"
elif [ "${chchecks:-0}" = "0" ]; then
    fail "no choice checks ran at all, so '0 mismatches' would mean nothing"
else
    pass "$chchecks checks, 0 mismatches"
fi
if [ "${chchecks:-0}" -ge 24 ]; then
    pass "the choice fixture ran its whole battery ($chchecks)"
else
    fail "the choice fixture ran only $chchecks checks, want at least 24"
fi

# --------------------------------------------------------------------- REFRESH

printf 'TIER refresh: a table shows the list as it is NOW\n'
# MEASURED WHILE BUILDING A WHOLE APPLICATION on this surface, and it was a GAP
# rather than a limit: a `table to show` captured its subject at open and NEVER
# FOLLOWED IT, so appending to the list left `rowCount` where it was and a
# program had no way to say otherwise. Awkward for a game; fatal for anything
# list-driven, which is most business software and is Studio's file tree.
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 120 ./gbasic --line-buffered \
    tests/qt_refresh_test.bas > "$work/rf.out" 2> "$work/rf.err"
rf_rc=$?
set -e
if [ "$rf_rc" != "0" ]; then
    printf '    %s\n' "$(tail -5 "$work/rf.err")"
    fail "the refresh fixture exited $rf_rc"
fi
rfchecks=$(grep -c '^ok   ' "$work/rf.out" || true)
if grep -q MISMATCH "$work/rf.out"; then
    grep MISMATCH "$work/rf.out" | while read -r line; do printf '    %s\n' "$line"; done
    fail "$(grep -c MISMATCH "$work/rf.out") mismatches in the refresh fixture"
elif [ "${rfchecks:-0}" = "0" ]; then
    fail "no refresh checks ran at all, so '0 mismatches' would mean nothing"
else
    pass "$rfchecks checks, 0 mismatches"
fi
if [ "${rfchecks:-0}" -ge 21 ]; then
    pass "the refresh fixture ran its whole battery ($rfchecks)"
else
    fail "the refresh fixture ran only $rfchecks checks, want at least 21"
fi

# ------------------------------------------------------------------- TEXT/MEMO

printf 'TIER text: a sentence on a window, and many lines of one\n'
# WHY THIS TIER EXISTS AT ALL is the finding: the leaf vocabulary reached
# QLineEdit, QPushButton and QCheckBox AND NO MORE, so until 2026-10-18 a
# heading, an instruction or a room description had NOWHERE TO GO -- and
# nothing in the tree said so, because every fixture and every example happened
# to need only an entry, a box and a button. Found by setting out to write one
# example per aspect of this backend, which is the argument for writing them.
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 120 ./gbasic --line-buffered \
    tests/qt_text_test.bas > "$work/tx.out" 2> "$work/tx.err"
tx_rc=$?
set -e
if [ "$tx_rc" != "0" ]; then
    printf '    %s\n' "$(tail -5 "$work/tx.err")"
    fail "the text fixture exited $tx_rc"
fi
txchecks=$(grep -c '^ok   ' "$work/tx.out" || true)
if grep -q MISMATCH "$work/tx.out"; then
    grep MISMATCH "$work/tx.out" | while read -r line; do printf '    %s\n' "$line"; done
    fail "$(grep -c MISMATCH "$work/tx.out") mismatches in the text fixture"
elif [ "${txchecks:-0}" = "0" ]; then
    fail "no text checks ran at all, so '0 mismatches' would mean nothing"
else
    pass "$txchecks checks, 0 mismatches"
fi
if [ "${txchecks:-0}" -ge 17 ]; then
    pass "the text fixture ran its whole battery ($txchecks)"
else
    fail "the text fixture ran only $txchecks checks, want at least 17"
fi

# ------------------------------------------------------------------------ MENU

printf 'TIER menu: a bar, submenus, separators, and properties on a QAction\n'
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 120 ./gbasic --line-buffered \
    tests/qt_menu_test.bas > "$work/m.out" 2> "$work/m.err"
m_rc=$?
set -e
if [ "$m_rc" != "0" ]; then
    printf '    %s\n' "$(tail -5 "$work/m.err")"
    fail "the menu fixture exited $m_rc"
fi
mchecks=$(grep -c '^ok   ' "$work/m.out" || true)
if grep -q MISMATCH "$work/m.out"; then
    grep MISMATCH "$work/m.out" | while read -r line; do printf '    %s\n' "$line"; done
    fail "$(grep -c MISMATCH "$work/m.out") mismatches in the menu fixture"
elif [ "${mchecks:-0}" = "0" ]; then
    # NEVER "0 mismatches" OVER A FIXTURE THAT RAN NOTHING. A perturbation that
    # makes the fixture DIE leaves no MISMATCH lines at all, so the plain
    # report printed `ok 0 checks, 0 mismatches` -- a green line above the two
    # FAILs that actually named the problem. Caught by the floor below either
    # way; the point is that the line itself must not read as a pass.
    fail "no menu checks ran at all, so '0 mismatches' would mean nothing"
else
    pass "$mchecks checks, 0 mismatches"
fi
if [ "${mchecks:-0}" -ge 27 ]; then
    pass "and it really ran them ($mchecks >= 27)"
else
    fail "only $mchecks menu checks ran -- the fixture stopped early"
fi
# TWO DIFFERENT SIGNALS ON THE SAME CLASS, proven by FIRING rather than by the
# window opening: `trigger` and `toggle` are no-arg QAction slots, so choosing
# a menu entry is driven with no human in front of it. One signal alone would
# be satisfied by a backend where that one happened to be wired.
if grep -qx 'SIGNAL open_file.triggered' "$work/m.out"; then
    pass "choosing a menu item reaches a gBASIC handler"
else
    fail "the item handler never fired: $(grep SIGNAL "$work/m.out" || echo 'nothing')"
fi
if grep -qx 'SIGNAL wrap_it.toggled' "$work/m.out"; then
    pass "and a checkable item's own signal is a separate connection"
else
    fail "the toggle handler never fired: $(grep SIGNAL "$work/m.out" || echo 'nothing')"
fi
# A MENU IS NOT A PART. Its CAPTION is what a person reads and its ITEMS are
# what a handler addresses, so the caption must not answer as a name -- which
# would let `qt.read(win, "File")` look like it worked.
if grep -q 'NOTADDRESSABLE: qt.read: this window has no `Recent`' "$work/m.out"; then
    pass "a menu's caption is not a part name"
else
    fail "a menu answered to its caption: $(grep NOTADDRESS "$work/m.out" || echo 'never fired')"
fi

# ----------------------------------------------------------------------- THEME

printf 'TIER theme: one stylesheet for the application, and it needs no display\n'
# PER-WIDGET STYLING ALREADY WORKED BEFORE THIS, and the tier says so with a
# check rather than a comment: `styleSheet` is a Q_PROPERTY of QWidget, so
# `field a( styleSheet: "..." )` has been reachable since the by-name property
# route shipped -- that route (§1) paying off in a way nobody designed. What it
# cannot reach is ONE sheet for the whole application, which is what anybody
# means by a theme, and that is what `qt.theme` adds.
cat > "$work/theme.bas" <<'EOF'
window w( title: "W" )
    column
        field a( label: "A", styleSheet: "color: red;" )
    end column
end window
program main()
    print "EMPTY:[" + qt.theme() + "]"
    print "SET:" + string(qt.theme("QLineEdit { background: #222; }"))
    print "BEFORE:" + qt.theme()
    print "DISPLAY:" + string(qt.can_display())
    x = qt.open(w)
    print "AFTER:" + qt.theme()
    print "WIDGET:" + qt.read(w, "a", "styleSheet")
    print "CHANGED:" + string(qt.theme("QLineEdit { background: #fff; }"))
    print "NOW:" + qt.theme()
    on error goto next
    y = qt.theme(42)
    if error then
        print "REFUSED:" + error.message
        error.clear()
    end if
end program
EOF
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 60 ./gbasic "$work/theme.bas" > "$work/th.out" 2>&1
set -e
# ASSERTED AS A DIFFERENCE between before and after opening, because the whole
# design decision is that the sheet is REMEMBERED and applied when the
# QApplication is constructed -- so a program themes first and opens second,
# and the first window is built already themed rather than appearing plain and
# then changing. A check only after opening passes on an implementation that
# refused until a window existed.
if grep -qx 'EMPTY:\[\]' "$work/th.out"; then
    pass "with nothing set, the theme is empty rather than absent"
else
    fail "the empty theme: $(grep EMPTY "$work/th.out" || echo 'no line')"
fi
if grep -qx 'BEFORE:QLineEdit { background: #222; }' "$work/th.out"; then
    pass "a theme set BEFORE any window is remembered and readable"
else
    fail "set-before-open: $(grep BEFORE "$work/th.out" || echo 'no line')"
fi
if grep -qx 'AFTER:QLineEdit { background: #222; }' "$work/th.out"; then
    pass "and it survives the QApplication being constructed"
else
    fail "survives-open: $(grep AFTER "$work/th.out" || echo 'no line')"
fi
# THE CASCADE IS QT'S OWN and so there is no precedence rule here to invent --
# but a backend that applied the app sheet by WRITING it onto every widget
# would pass the two checks above and destroy this one.
if grep -qx 'WIDGET:color: red;' "$work/th.out"; then
    pass "a widget keeps its OWN sheet, so the app theme did not overwrite it"
else
    fail "the per-widget sheet: $(grep WIDGET "$work/th.out" || echo 'no line')"
fi
if grep -qx 'NOW:QLineEdit { background: #fff; }' "$work/th.out"; then
    pass "and the theme can be changed once a window is up"
else
    fail "changing it: $(grep NOW "$work/th.out" || echo 'no line')"
fi
# THE MESSAGE NAMES THE REMEDY, because a theme is almost always a FILE and the
# platform/policy split means this verb takes TEXT -- so the error has to spell
# the composition rather than leave the author to find it.
if grep -q 'REFUSED:.*read({file}"dark.qss")' "$work/th.out"; then
    pass "a theme that is not text is refused, naming how to load a file"
else
    fail "the refusal: $(grep REFUSED "$work/th.out" || echo 'never fired')"
fi
# AND IT WORKS WITH NO DISPLAY AT ALL, which is the half that could not be
# asserted offscreen: setting a theme is harmless metadata, so refusing it on a
# text-only machine would make a program that themes before asking
# `qt.can_display()` fail for no reason.
cat > "$work/themenodisp.bas" <<'EOF'
program main()
    print "SET:" + string(qt.theme("QWidget { color: #abc; }"))
    print "BACK:" + qt.theme()
    print "DISPLAY:" + string(qt.can_display())
end program
EOF
set +e
env -u DISPLAY -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR QT_QPA_PLATFORM= \
    timeout -k 5 60 ./gbasic "$work/themenodisp.bas" > "$work/thn.out" 2>&1
nd_rc=$?
set -e
if [ "$nd_rc" = "0" ] && grep -qx 'BACK:QWidget { color: #abc; }' "$work/thn.out" \
   && grep -qx 'DISPLAY:false' "$work/thn.out"; then
    pass "a theme is set and read with NO display, where every other verb refuses"
else
    printf '    %s\n' "$(tr '\n' '|' < "$work/thn.out")"
    fail "theming required a display (exit $nd_rc)"
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
printf 'TIER valgrind: no invalid access while a real window is bound and folded\n'
# ACCESS ONLY. The leak half is unavailable -- Qt does not free its platform
# plugin, its font configuration or its QApplication statics -- so what is
# claimed here is exactly what can be claimed: nothing in this increment reads
# or writes memory it does not own. That is not a weak claim for the code under
# test: a form's capture is a `Value` COPY held in a registry across the whole
# life of the window and freed in two places (a failed open, and teardown), and
# the fold reads it again on every `qt.subject` -- which is a refcounted value
# surviving calls, the shape PLAT-RECIDX's two real defects had and neither of
# which produced a wrong answer.
if ! vg_available; then
    printf '  SKIP (valgrind unavailable -- memory access while a window is bound was NOT measured)\n'
else
    set +e
    # NO `timeout` IN FRONT OF IT, which the first draft had and which cannot
    # work: `vg_run_access_only` is a shell FUNCTION and timeout execs a
    # program, so it reported exit 127 -- the same trap run_odbc.sh records
    # from its own first draft, where `sh -c "exec vg_run ..."` printed `ok`
    # over `vg_run: not found`. Here it FAILED LOUDLY instead, because this
    # tier compares against $VG_EXIT rather than against zero. The hang guard
    # is run_all.sh's per-suite cap, which is what every other valgrind tier in
    # this tree relies on.
    QT_QPA_PLATFORM=offscreen vg_run_access_only ./gbasic \
        tests/qt_binding_test.bas > "$work/vgb.out" 2> "$work/vgb.err"
    vg_bind_rc=$?
    QT_QPA_PLATFORM=offscreen vg_run_access_only ./gbasic \
        tests/qt_table_test.bas > "$work/vgt.out" 2> "$work/vgt.err"
    vg_table_only_rc=$?
    QT_QPA_PLATFORM=offscreen vg_run_access_only ./gbasic \
        tests/qt_menu_test.bas > "$work/vgm.out" 2> "$work/vgm.err"
    vg_menu_only_rc=$?
    QT_QPA_PLATFORM=offscreen vg_run_access_only ./gbasic \
        tests/qt_tabs_test.bas > "$work/vgb2.out" 2> "$work/vgtb.err"
    vg_tabs_only_rc=$?
    QT_QPA_PLATFORM=offscreen vg_run_access_only ./gbasic \
        tests/qt_cells_test.bas > "$work/vgc.out" 2> "$work/vgc.err"
    vg_cells_rc=$?
    set -e
    vg_menu_rc=$vg_menu_only_rc
    vg_tabs_rc=$vg_tabs_only_rc
    # THE WORST OF THE THREE, so a clean run of one fixture cannot hide a
    # defect in another -- a form, a table and a menu allocate differently.
    vg_rc=$vg_bind_rc
    if [ "$vg_table_only_rc" != "0" ]; then
        vg_rc=$vg_table_only_rc
        cp "$work/vgt.err" "$work/vgb.err"
    fi
    if [ "$vg_menu_rc" != "0" ]; then
        vg_rc=$vg_menu_rc
        cp "$work/vgm.err" "$work/vgb.err"
    fi
    if [ "$vg_tabs_rc" != "0" ]; then
        vg_rc=$vg_tabs_rc
        cp "$work/vgtb.err" "$work/vgb.err"
    fi
    if [ "$vg_cells_rc" != "0" ]; then
        vg_rc=$vg_cells_rc
        cp "$work/vgc.err" "$work/vgb.err"
    fi
    # COMPARED AGAINST $VG_EXIT, which the shared policy exposes for exactly
    # this: the fixture itself can fail for its own reasons, and reading that
    # as a valgrind objection prints FAIL over a report saying zero errors --
    # the reports-the-wrong-cause class run_repl.sh's own tier produced.
    if [ "$vg_rc" = "$VG_EXIT" ]; then
        printf '    %s\n' "$(grep -E 'Invalid|uninitialised' "$work/vgb.err" | head -3)"
        fail "valgrind reported an invalid access while a form was bound"
    elif [ "$vg_rc" != "0" ]; then
        printf '    %s\n' "$(tail -3 "$work/vgb.err")"
        fail "the fixture exited $vg_rc under valgrind, which is not a valgrind objection"
    else
        pass "no invalid access or uninitialised read across open, fold and teardown -- form, table, menu, tabs and cells"
    fi
fi

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
