#!/usr/bin/env bash
# A CONTEXT MENU (docs/qt_gui_design.md §0.20) -- `popup`, `qt.popup`,
# `qt.right_click`, and the policy a declared handler asks for.
#
# `set +e` BELOW WITHOUT A MATCHING `set -e`, DELIBERATELY: this file's mode is
# `set -uo pipefail` -- no `-e` -- so `set +e` is a no-op and a `set -e` after
# it would TURN THE MODE ON, which costs nothing until a suite goes red and
# then kills it mid-tier with no verdict. run_portable.sh tripwires it.
#
# DERIVED FROM gBASIC STUDIO'S OWN SOURCE, like `split` before it, and reading
# it changed the increment twice over. Studio's browser context menu (STU-13)
# is built ONCE holding every item there is and HIDES the ones a row does not
# offer -- so what it needs from a declaration is a menu that is DECLARED,
# which is what a `window` block already is, and `visible` is Q_PROPERTY 14 of
# QAction so `qt.set` already reaches it: no new capability. And its
# `context_actions(rows, index)` takes an INDEX, so the one genuine gap looked
# like "which row was right-clicked".
#
# SEVEN THINGS WERE MEASURED IN C++ FIRST AND FOUR CHANGED WHAT GOT BUILT:
#
#   the signal DOES fire from a      an earlier probe reported 0 and was
#   synthetic QContextMenuEvent      wrong; the path is drivable
#   a table/memo/source answers      direct=0, via-viewport=1 -- a real
#   only through its VIEWPORT        right-click lands on the viewport, and
#                                    gBASIC cannot name one
#   QMenu::popup and ::exec are      both -1 against `show` 18, `hide` 19 --
#   NOT invokable by name            so `qt.invoke` cannot raise a menu
#   Qt MOVES THE SELECTION on a      so `qt.selected` already answers "which
#   right-press (currentRow 3)       row", and the gap closed itself
#   a QMenu parented to a widget     layout count 2 before, 2 after -- a popup
#   WITH a layout is not taken       is placed by nothing, like the bar
#   `visible` is Q_PROPERTY 14       of QAction, settable by name
#   an ALL-HIDDEN menu answers       isEmpty() true, and popup() leaves it
#                                    INVISIBLE, so the honest answer here
#                                    is a boolean rather than a refusal
#
# THE TIER TO READ FIRST IS POLICY, because it is the silent class this backend
# is tested against arriving from a Qt DEFAULT: every widget kind starts at
# `DefaultContextMenu`, and under that policy `customContextMenuRequested` is
# NEVER EMITTED -- so a handler declared for it without a second line setting
# the policy is connected to a signal nothing emits, the right-click does
# nothing, and nothing is raised. Declaring the handler is therefore what asks
# for the custom menu. Asserted as a DIFFERENCE, with the CONTROL that a part
# with NO handler keeps Qt's own cut/copy/paste -- without which "the policy is
# set" is satisfied by setting it on every widget in the language.
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
# `popup row_actions` IS THE PRODUCTION `tabs settings` ALREADY USES, so this
# increment adds no grammar at all -- which was measured before the word was
# chosen rather than hoped for. Asserted as a standing tier, which is not
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

# -------------------------------------------------------------- ONE WALK
# A POPUP'S ENTRIES AND A BAR MENU'S ARE WALKED BY ONE FUNCTION. Two copies
# would drift in exactly the way that matters -- a popup's items stopping
# agreeing with a bar menu's about what `shortcut:` or a submenu means -- which
# is the rule `would`/`execute` already follow in `automation`. The three
# constructors differ only in how the QMenu itself is made.
printf 'TIER one walk: a popup and a bar menu share one entry walk\n'
walks=$(grep -c 'static int qt_fill_menu' src/modules/qt.c || true)
callers=$(grep -c 'return qt_fill_menu(root, node, menu);' src/modules/qt.c || true)
additem=$(grep -c 'gb_qt_menu_item(root, node' src/modules/qt.c || true)
if [ "${walks:-0}" = "1" ] && [ "${callers:-0}" = "2" ] && [ "${additem:-0}" = "1" ]; then
    pass "one walk, two constructors, one place that adds an item"
else
    fail "expected 1 walk / 2 constructors / 1 item site, got ${walks:-0}/${callers:-0}/${additem:-0} -- a second walk means a popup's items can stop agreeing with a menu's"
fi
# THE VACUITY CONTROL: counting OCCURRENCES and not LINES, because a second
# call site written by hand goes on one line (`popup ? a : b`) -- which is how
# the equivalent control in run_qt_modal.sh was found to be unable to fail.
sed 's/return qt_fill_menu(root, node, menu);/return qt_fill_menu(root, node, menu); \/* and again *\/ return qt_fill_menu(root, node, menu);/' \
    src/modules/qt.c > "$work/two.c"
planted=$(grep -o 'return qt_fill_menu(root, node, menu);' "$work/two.c" | wc -l)
if [ "${planted:-0}" -gt 2 ]; then
    pass "CONTROL: a planted extra call site is counted ($planted)"
else
    fail "CONTROL: the scan counts lines rather than occurrences, so it cannot see a second call site"
fi

# -------------------------------------------------------------- DECLARATION
# RUNS WHETHER OR NOT QT IS IN THE BUILD, because these are claims about the
# DECLARATION -- the property that let the whole window vocabulary ship before
# any backend existed.
printf 'TIER declaration: a popup is NAMED where a menu is CAPTIONED\n'
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

# THE ONE SLOT, SPENT THE OTHER WAY ROUND FROM `menu`. Both halves, so neither
# shape can be written the other's way and quietly mean something else.
decl 'a popup with no name is refused' \
'    popup
        item a( label: "A" )
    end popup' \
    'a popup menu needs a name'
decl 'a popup with a caption is refused' \
'    popup "Row actions"
        item a( label: "A" )
    end popup' \
    'takes no caption'
decl 'and a NAMED popup is accepted' \
'    popup row_actions
        item a( label: "A" )
    end popup' \
    ''
# A MENU STILL NEEDS ITS CAPTION AND STILL REFUSES A NAME -- the control that
# says the two words have not quietly become one.
decl 'CONTROL: a menu still needs a caption' \
'    menu
        item a( label: "A" )
    end menu' \
    'a menu needs a caption'
decl 'CONTROL: a menu still takes no name' \
'    menu mymenu
        item a( label: "A" )
    end menu' \
    'a menu takes no name'

# PLACEMENT. A QMenu is placed by NOTHING -- measured, parenting one to a
# widget that has a layout left that layout's count unchanged -- so a popup
# written inside a `row` is a declaration the layout silently ignores, which is
# the class the closed vocabulary exists to prevent.
decl 'a popup inside a layout is refused' \
'    column
        popup row_actions
            item a( label: "A" )
        end popup
    end column' \
    'belongs to the window'
decl 'a popup holds only menu words' \
'    popup row_actions
        button go( label: "Go" )
    end popup' \
    'is none of those'
# THE PLACES AN `item` MAY LIVE GREW BY ONE in §0.30 -- a `toolbar`, whose
# entries are MEASURED to be the same QActions a menu is built from -- so the
# refusal now names three rather than two. The claim this tier makes is
# unchanged: a `column` is still none of them.
decl 'an item outside a menu, a popup or a toolbar is still refused' \
'    column
        item a( label: "A" )
    end column' \
    "belongs in a menu, a popup or a toolbar"
decl 'a submenu inside a popup is accepted' \
'    popup row_actions
        item a( label: "A" )
        menu "More"
            item b( label: "B" )
        end menu
    end popup' \
    ''
decl 'two popups sharing a name are refused' \
'    popup row_actions
        item a( label: "A" )
    end popup
    popup row_actions
        item b( label: "B" )
    end popup' \
    'is declared twice'
# THE NEAR MISS. A third hardcoded copy of this vocabulary was found in
# `win_nearest` when `table` was added, so the suggestion is derived from the
# two tables -- which means a new word is reachable as a suggestion for free,
# and this asserts it for `popup`.
decl 'a misspelled popup gets the near miss' \
'    poppup row_actions
        item a( label: "A" )
    end poppup' \
    "did you mean 'popup'?"
# AND THE HANDLER REFUSAL STILL BITES ON A POPUP'S ITEMS, which is the refusal
# load-time validation exists for: a handler naming nothing can NEVER fire and
# nothing at run time would ever say so.
decl 'a handler naming no item is refused' \
'    popup row_actions
        item a( label: "A" )
    end popup
    on nobody.triggered
        print "x"
    end on' \
    'nobody'

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: the browser shape, the oracle, the policy and the row\n'
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic \
    --line-buffered tests/qt_context_test.bas > "$work/sem.out" 2> "$work/sem.err"
rc=$?
if grep -q "SKIP no display" "$work/sem.out"; then
    printf '  SKIP (no display -- the context menu was NOT exercised)\n'
elif [ "$rc" != "0" ]; then
    grep MISMATCH "$work/sem.out" | head -4
    tail -3 "$work/sem.out"; tail -3 "$work/sem.err"
    fail "the fixture exited $rc"
else
    checks=$(grep -c '^ok' "$work/sem.out" || true)
    bad=$(grep -c 'MISMATCH' "$work/sem.out" || true)
    if [ "$bad" = "0" ] && [ "${checks:-0}" -ge 35 ]; then
        pass "$checks checks, 0 mismatches"
    else
        grep MISMATCH "$work/sem.out" | head -6
        fail "$checks checks, $bad mismatches (want >= 35 and none)"
    fi
    # QT'S OFFSCREEN PLUGIN SAYS THESE ON EVERY RUN AND THEY ARE NOT OURS:
    # `propagateSizeHints` on any window, and `raise` and `grabbing the
    # keyboard` the moment a menu pops. Filtered on the whole sentence rather
    # than on the word "plugin", so a real plugin failure still reddens this.
    grep -v 'This plugin does not support' "$work/sem.err" > "$work/sem.err2" || true
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing of its own on stderr"
    fi
    # NAMED, so a failure says which claim went rather than only moving a count.
    if grep -q "^ok   a part with a handler got the policy" "$work/sem.out" &&
       grep -q "^ok   a memo with no handler keeps Qt's own menu" "$work/sem.out"; then
        pass "POLICY: set where a handler was declared, and nowhere else"
    else
        fail "the policy difference was not measured -- without it a declared handler is connected to a signal Qt never emits"
    fi
    if grep -q "^ok   every kind answered a right-click" "$work/sem.out" &&
       grep -q "^ok   and each one is the part that was clicked" "$work/sem.out"; then
        pass "VIEWPORT: all five kinds answered, each the part that was clicked"
    else
        fail "the viewport routing was not measured -- a table, memo and source answer NOTHING without it"
    fi
    if grep -q "^ok   the selection followed the click" "$work/sem.out" &&
       grep -q "^ok   a second click moved the row" "$work/sem.out"; then
        pass "ROW: a right-click moves the selection, so qt.selected answers it"
    else
        fail "the row question was not measured -- a handler would read a stale selection"
    fi
    if grep -q "^ok   editor_actions entries" "$work/sem.out" &&
       grep -q "^ok   the bar holds only the declared menu" "$work/sem.out"; then
        pass "ORACLE: two popups report distinct contents, and neither is on the bar"
    else
        fail "the popup oracle did not run -- an item built into the wrong popup keeps its name and is invisible"
    fi
    if grep -q "^ok   an all-hidden popup shows nothing" "$work/sem.out" &&
       grep -q "^ok   and shows again once an item is back" "$work/sem.out"; then
        pass "SHOWN: both halves of the boolean"
    else
        fail "qt.popup's answer was not measured both ways -- a bare true alone passes on a verb that always says yes"
    fi
    # THE SUBMENU HALF, found by examples/qt/browser.bas rather than reasoned:
    # hiding every item INSIDE a submenu does not hide the submenu, and
    # measured, Qt then reports isEmpty() FALSE and SHOWS a 120x25 menu holding
    # nothing but an arrow to nowhere. BOTH directions, because a count that
    # stopped treating a submenu as anything at all satisfies the first.
    if grep -q "^ok   a popup holding only an empty submenu shows nothing" "$work/sem.out" &&
       grep -q "^ok   a directory offers only what the submenu leads to" "$work/sem.out"; then
        pass "SUBMENU: the count recurses, asserted both ways"
    else
        fail "the submenu was not counted for what it leads to -- a popup showing only an arrow to nowhere would report that it shows something"
    fi
    # UNDER A PART rather than at the pointer, which §0.20 left open as its own
    # limitation and which Studio's toolbar menu button is the caller for: a
    # menu button's menu belongs under the button, not wherever the mouse is.
    # THE POSITION, AND IT IS A DIFFERENCE. `qt.popup`'s boolean says only that
    # a menu appeared, so a perturbation that IGNORED the third argument and
    # popped at the pointer left every check green -- the fifth time in these
    # increments that a green perturbation meant a missing observable. A menu's
    # own `x` is in a different coordinate space from a button's, so the honest
    # check is that popping under one button and under another puts it in
    # DIFFERENT places, with the pointer as a third.
    if grep -q "^ok   a menu popped under a button appeared" "$work/sem.out" &&
       grep -q "^ok   popping under a different button moved the menu" "$work/sem.out" &&
       grep -q "^ok   and at the pointer it is in neither place" "$work/sem.out" &&
       grep -q "^ok   popping under something that is not there is refused" "$work/sem.out"; then
        pass "UNDER: the menu follows the part named, three positions apart"
    else
        fail "where the menu popped was not measured -- the boolean is true whether or not the third argument was honoured"
    fi
    # AND THE SAME WORD IN A LAYOUT. A `separator` means a line that divides,
    # which is as true of a rule between two sections as of a line in a menu --
    # one word, two widgets. A row is divided by a VERTICAL line, which is the
    # half a menu separator cannot show.
    if grep -q "^ok   a toolbar divider is vertical" "$work/sem.out"; then
        pass "RULE: a separator in a row, beside the menu separator above"
    else
        fail "the layout separator was not measured here -- its orientation comes from the parent and is the one thing that can be built wrong"
    fi
fi

# ------------------------------------------------------------------ VALGRIND
# ACCESS-ONLY, for the reason run_qt_backend.sh records: Qt does not free its
# platform plugin, font configuration or QApplication statics, so a leak claim
# would be a suppression file describing Qt. What is NEW here is a QMenu owned
# by the window rather than by the bar, a popped menu outliving the call that
# raised it, and synthetic QMouseEvents delivered into a viewport.
printf 'TIER valgrind: no invalid access across the popup and the clicks\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_context_test.bas \
        >/dev/null 2>"$work/vg.err"
    vg_rc=$?
    if [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    else
        pass "no invalid access across three windows, a popped menu and five clicks"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_context: all checks passed\n'
else
    printf 'run_qt_context: FAILED\n'
fi
exit "$status"
