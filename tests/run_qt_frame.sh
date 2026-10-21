#!/bin/bash
# THE WINDOW'S OWN FURNITURE (§0.30) -- dock panes, a toolbar and a status bar.
#
# THE FINDING THAT MADE THIS ONE INCREMENT RATHER THAN THREE: after §0.28's
# `tree` and §0.29's margin closed the two BLOCKING asks,
# docs/qt_studio_requests.md had three left -- dock panes (§3), a status bar and
# a toolbar (§4), and `qt.theme` (§5, which already exists and is recorded there
# as a prediction rather than an ask). Every one of the first two is a
# QMainWindow feature, so the question to ask was §0.18's, of a different root:
# DOES EVERYTHING THIS BACKEND BUILDS WORK INSIDE ONE? Measured yes -- a form, a
# table, tabs, a splitter, a `source` and a menu bar all do -- so nothing below
# the root had to change and three asks became one increment.
#
# ONLY WHEN THE DECLARATION ASKS, which is §0.18's own rule for the modal root
# read again: making every window a QMainWindow would have worked and been a
# wider change for nothing. A window asks by holding a `docks`, a `toolbar` or a
# `status`, and the CONTROL is that a window holding none is untouched -- without
# which "a main window works" is satisfied by making every root one.
#
# THE GRAMMAR COST ONE PRODUCTION AND IT WAS PRICED FIRST: `dock branches
# "Branches"` is `IDENT IDENT STRING`, measured at ZERO shift/reduce and ZERO
# reduce/reduce before the word existed. It is the one shape in this vocabulary
# carrying a NAME AND A CAPTION, which §0.8's one-slot rule otherwise forbids --
# and the rule still holds, because that wall is between two heads which both
# end in `NEWLINE` (`IDENT IDENT NEWLINE` against `IDENT IDENT LPAREN ... RPAREN
# NEWLINE`), where this one has a STRING where the choice would be. A dock needs
# both for different reasons: the caption is what a person reads on its title
# bar, and the NAME is what `qt.set` addresses and what Qt keys a saved layout
# by -- measured, a dock with no `objectName` makes `saveState` WARN and omit
# it, so "restore my layout" would come back silently partial.
#
# EVERYTHING ELSE REUSED AN EXISTING SHAPE: `docks right` is `split across`'s
# (§0.19), `toolbar main` is `tabs settings`'s, and `status` is a bare `row`'s.
#
# THREE MEASUREMENTS DECIDED THE DESIGN RATHER THAN CONFIRMING IT.
#
#   A QStatusBar HAS NO PROPERTY FOR ITS TEXT -- `message` and `currentMessage`
#   are both absent and `showMessage(QString)` takes an argument `qt.invoke`
#   cannot pass -- while a QLabel INSIDE it carries `text` as an ordinary
#   Q_PROPERTY. So a status bar holds named `text` parts and `qt.set(win,
#   "msg", "text", ...)` reaches them through the route that already exists:
#   §1's by-name argument paying off instead of a verb of its own.
#
#   A QToolBar'S ENTRIES ARE QActions, the same objects §0.7 already builds for
#   a menu, with `checkable:` and `icon:` both working on one in a toolbar. So
#   `item` and `separator` are admitted inside a toolbar and ONE WALK serves
#   both -- which is why a toolbar needed no new part kind, and why an `item`
#   means the same thing in both places by construction rather than by
#   agreement.
#
#   A QMenuBar ADDED TO THE CENTRAL WIDGET'S LAYOUT RENDERS INSIDE IT -- a bar
#   in the middle of the window, under the toolbar and above the content -- so
#   a main window must place it with `setMenuBar`. That is the one existing
#   path this increment had to change, and the tier asserting the menu bar
#   survived is what says the two paths agree.
#
# AND `saveState` IS NOT VALID UTF-8, which decided how the layout is returned:
# measured, 95 of its 143 bytes are NUL and 16 are above 127, and the obvious
# thing a program does with it is STORE it -- through `encode` or `persist`,
# both of which write JSON, whose strings must be valid UTF-8. gBASIC strings
# are binary-safe (PLAT-NUL), so handing back the raw bytes would be defensible
# and useless. `qt.layout` answers BASE64, which round-trips exactly.
#
# THE TIER TO READ FIRST IS THE AREA, because a pane in the WRONG ONE is
# invisible from the language: every part inside it reads back by name whichever
# edge it ended up on, and `visible` is true either way. `qt.dock` is the oracle,
# built before the fixture (§0.8's lesson), and it asks THE WINDOW
# (`dockWidgetArea`) rather than anything we remembered -- `qt.at`'s and
# `qt.pane`'s own justification, since a remembered position agrees with a
# placement that was wrong. The SECOND `docks` block is what separates "the area
# was read" from "everything went to one edge".
. "$(dirname "$0")/portable.sh"
# NO `set -e`: this header omits it, so a bare `set -e` would ARM it rather than
# restore it -- run_portable.sh tripwires for that and caught run_qt_gutter.sh
# doing it within minutes of the suite existing.
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/build_has.sh
source tests/valgrind_tier.sh
# THE BUILD IS CHECKED, the lesson §0.29's perturbation round paid for: a failed
# `make` here would leave the suite running the PREVIOUSLY BUILT binary and
# reporting green, so a perturbation that did not compile would look like one
# the checks cannot see.
if ! make >/dev/null; then
    printf 'run_qt_frame: FAILED (the build failed -- nothing was measured)\n'
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# ------------------------------------------------------------------ GRAMMAR
printf 'TIER grammar: the one new production costs nothing\n'
if ! command -v bison >/dev/null 2>&1; then
    printf '  SKIP (no bison -- the grammar cost was NOT measured)\n'
else
    bison -d src/parser.y -o "$work/p.c" 2>"$work/bison.err" || true
    sr=$(grep -cE "shift/reduce" "$work/bison.err" || true)
    rr=$(grep -cE "reduce/reduce" "$work/bison.err" || true)
    if [ "${sr:-0}" = "0" ] && [ "${rr:-0}" = "0" ]; then
        pass "zero shift/reduce and zero reduce/reduce with IDENT IDENT STRING"
    else
        head -4 "$work/bison.err"
        fail "the grammar now reports conflicts"
    fi
fi

# -------------------------------------------------------------- DECLARATION
# RUNS WHETHER OR NOT QT IS IN THE BUILD: claims about the load-time pass.
printf 'TIER declaration: where the furniture may go, and what it must carry\n'
decl() {                        # label, window body, expected fragment ("" = accept)
    cat > "$work/d.bas" <<BAS
window app( title: "D", width: 400, height: 300 )
$2
end window
program main(args)
    print "RAN"
end program
BAS
    out="$(GBASIC_PATH=stdlib ./gbasic "$work/d.bas" 2>&1 || true)"
    if [ -z "$3" ]; then
        if printf '%s' "$out" | grep -q RAN; then
            pass "$1"
        else
            printf '     %s\n' "$(printf '%s' "$out" | head -2)"
            fail "$1 (should have been accepted)"
        fi
    elif printf '%s' "$out" | grep -qF -- "$3"; then
        pass "$1"
    else
        printf '     %s\n' "$(printf '%s' "$out" | head -2)"
        fail "$1"
    fi
}
decl "a docks block with panes is admitted" '    docks right
        dock a "A"
            text t( label: "x" )
        end dock
    end docks' ""
# THE AREA IS A WORD IN THE ONE SLOT, `split across`'s arrangement.
decl "a docks block needs an area" '    docks
        dock a "A"
        end dock
    end docks' "needs an area"
decl "a misspelled area names the right word" '    docks riight
        dock a "A"
        end dock
    end docks' "did you mean \`right\`?"
decl "an area as a string is refused" '    docks "right"
        dock a "A"
        end dock
    end docks' "not a string"
# THE WINDOW PLACES ITS OWN FURNITURE -- the `menu` rule, reached by the same
# measurement one widget along: a QDockWidget in a box layout is laid out like
# any other widget, a titled frame mid-screen with no dock behaviour.
decl "a docks block inside a layout is refused" '    column
        docks right
            dock a "A"
            end dock
        end docks
    end column' "belongs to the window"
decl "a toolbar inside a layout is refused" '    column
        toolbar main
            item o( label: "O" )
        end toolbar
    end column' "belongs to the window"
decl "a status bar inside a layout is refused" '    column
        status
            text m( label: "x" )
        end status
    end column' "belongs to the window"
# A `dock` IS A PANE OF A BLOCK: the area lives on the block, so one outside
# has no area to go in and nothing would place it.
decl "a dock outside a docks block is refused" '    column
        dock a "A"
        end dock
    end column' "pane of a \`docks\` block"
decl "a dock needs a caption" '    docks right
        dock a
        end dock
    end docks' "needs a caption"
decl "an empty docks block is refused" '    docks right
    end docks' "holds no panes"
decl "a docks block holding something else is refused" '    docks right
        column
        end column
    end docks' "holds \`dock\` panes"
# A TOOLBAR'S NAME IS WHAT Qt KEYS A SAVED LAYOUT BY.
decl "a toolbar needs a name" '    toolbar
        item o( label: "O" )
    end toolbar' "needs a name"
decl "a toolbar captioned is refused" '    toolbar "Main"
        item o( label: "O" )
    end toolbar' "carries its NAME"
decl "a widget in a toolbar is refused" '    toolbar main
        field f( label: "F" )
    end toolbar' "holds items and separators"
decl "but an item and a separator are admitted" '    toolbar main
        item o( label: "O" )
        separator()
        item s( label: "S" )
    end toolbar' ""
# A STATUS BAR TAKES NO NAME -- the `menu` rule, since there is one per window
# and what a program updates is the text IN it.
decl "a named status bar is refused" '    status thing
        text m( label: "x" )
    end status' "takes no name"
decl "a widget in a status bar is refused" '    status
        button b( label: "B" )
    end status' "holds \`text\` and \`stretch\`"
decl "but text and stretch are admitted" '    status
        text m( label: "Ready" )
        stretch()
        text p( label: "1:1" )
    end status' ""
# ONE STATUS BAR, which is Qt's own answer rather than a rule chosen here:
# `statusBar()` returns THE one, so a second block would add its parts to the
# first and the two declarations would silently merge.
decl "two status bars are refused" '    status
        text m( label: "x" )
    end status
    status
        text n( label: "y" )
    end status' "a window has one"

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: the areas, the tabbing, the bars and the saved layout\n'
if ! build_has qt; then
    printf '  SKIP (no Qt in this build -- the furniture was NOT measured)\n'
else
    set +e
    QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 90 \
        ./gbasic --line-buffered tests/qt_frame_test.bas \
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
        # THE OFFSCREEN PLATFORM COMPLAINS ABOUT raise() WHEN A PANE IS
        # FLOATED, which is a fact about the platform plugin and not about the
        # program -- the same class as the propagateSizeHints notice every
        # other Qt suite here filters.
        if [ -s "$work/sem.err" ] \
           && grep -vqE "This plugin does not support|^$" "$work/sem.err"; then
            head -3 "$work/sem.err"; fail "the fixture wrote to stderr"
        else
            pass "and nothing on stderr but the platform's own notices"
        fi
    fi
fi

# ------------------------------------------------------------- ROOT IS ASKED
# THE CONTROL ON THE WHOLE INCREMENT, and it is a SOURCE claim because it
# cannot be seen from the language: a plain window and a main window behave
# identically for everything the declaration below the root can express, so
# "an ordinary window still works" passes either way. What must be true is that
# the frame is built ONLY when the declaration asks.
printf 'TIER root: a main window is built only when the declaration asks\n'
asks=$(grep -c 'needs_frame = 1' src/modules/qt.c || true)
words=$(grep -c 'strcmp(iw, "docks") == 0' src/modules/qt.c || true)
if [ "${asks:-0}" -lt 1 ] || [ "${words:-0}" -lt 1 ]; then
    fail "nothing in src/modules/qt.c decides the root from the declaration"
else
    pass "the root kind is decided by the declaration's own words"
fi
# AND A MODAL WINDOW CANNOT HAVE THEM, measured: a QDialog is not a QMainWindow,
# so the two cannot be combined -- refused rather than silently downgraded,
# because the alternative is a modal window whose declared panes are not there.
if ! build_has qt; then
    printf '  SKIP (no Qt in this build -- the modal refusal was NOT measured)\n'
else
    cat > "$work/m.bas" <<'BAS'
window dlg( title: "M", width: 300, height: 200 )
    column
        field who( label: "Name" )
    end column
    status
        text m( label: "Ready" )
    end status
end window
program main(args)
    on error goto next
    qt.open_modal(dlg)
    if error then print "REFUSED " + error.message
end program
BAS
    set +e
    out="$(QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 30 \
            ./gbasic "$work/m.bas" 2>&1)"
    set -e
    if printf '%s' "$out" | grep -qF "cannot be shown modally"; then
        pass "a window with furniture is refused modally, naming why"
    else
        printf '     %s\n' "$(printf '%s' "$out" | head -2)"
        fail "a modal window with a status bar was not refused"
    fi
fi

# -------------------------------------------------------------- VALGRIND
printf 'TIER valgrind: no invalid access across docks, a float and a restore\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
elif ! build_has qt; then
    printf '  SKIP (no Qt in this build)\n'
else
    # THE PATHS THAT ALLOCATE: three docks across two areas, a tabify, a float
    # and a restoreState -- which rebuilds the whole dock layout under a live
    # window, the place a stale pointer would be a use-after-free a correct
    # answer would not reveal.
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_frame_test.bas \
        >/dev/null 2>"$work/vg.err" &
    vg_pid=$!
    vg_killed=0
    vg_deadline=$((SECONDS + 420))
    while kill -0 "$vg_pid" 2>/dev/null; do
        if [ "$SECONDS" -ge "$vg_deadline" ]; then
            # THE CHILDREN BY PID FIRST: `$!` is the SUBSHELL bash forks for a
            # background shell function, so killing it alone ORPHANS valgrind --
            # the leak run_qt_dialog.sh and run_qt_modal.sh each paid for.
            for vg_kid in $(pgrep -P "$vg_pid" 2>/dev/null); do
                kill -9 "$vg_kid" 2>/dev/null
            done
            kill -9 "$vg_pid" 2>/dev/null
            pkill -KILL -f "gbasic tests/qt_frame_test.bas" 2>/dev/null
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
        pass "no invalid access across docks, a float, a restore and teardown"
    fi
    # THE ABSENCE IS ASSERTED, because the leak is invisible from inside.
    if pgrep -f "gbasic tests/qt_frame_test.bas" >/dev/null 2>&1; then
        pkill -KILL -f "gbasic tests/qt_frame_test.bas" 2>/dev/null
        fail "the valgrind tier left a gbasic process behind"
    else
        pass "and nothing was left running afterwards"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_frame: all checks passed\n'
else
    printf 'run_qt_frame: FAILED\n'
fi
exit "$status"
