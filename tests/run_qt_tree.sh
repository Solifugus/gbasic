#!/bin/bash
# A `tree` (§0.28) -- rows with a DEPTH, shown as a tree, plus `qt.depth`.
#
# THE USER ASKED FOR THIS BY NAME (*"a better looking tree view on the left"*),
# relayed in docs/qt_studio_requests.md as the first of two asks that BLOCK a
# feature Studio already has: its file browser is a `Gtk.ListBox` of hand-built
# rows under GTK -- a glyph label pinned to one character, a margin per depth
# level, an ellipsizing name -- and in this vocabulary it had to become a
# `table`, the only widget here whose row count can change without rebuilding
# the window. That works and looks like a spreadsheet pretending to be a file
# tree.
#
# MEASURED FIRST, AND IT MOVED THE WORK RATHER THAN THE ESTIMATE: ten of the
# things that request calls missing chrome are already Q_PROPERTYs on a
# QTreeWidget, and `showGrid` does not exist on one at all, so there are no
# grid lines to turn off. The request's own note that header visibility "is not
# a Q_PROPERTY" is true of `headerVisible` and NOT of `headerHidden`. So what
# this adds is THE WIDGET AND THE DEPTH; the look was always one `qt.set` away,
# which the fixture asserts rather than assuming.
#
# THE SPELLING COSTS NO GRAMMAR: `tree files to show rows` is the production
# `table orders to show lines` already uses (§0.6, `TO` being a hard keyword so
# one token of lookahead separates it from a named leaf's `LPAREN`), and the
# standing zero-conflict claim is asserted here as usual.
#
# AN INDEX MEANS THE DECLARED ROWS, NEVER WHAT IS ON SCREEN, and that is
# measured rather than chosen: a collapsed child is NOT `isHidden()`, so Qt's
# own NotHidden iterator still counts every row and there is no free "visible
# index". An index into the rows the program handed over is the one number that
# stays meaningful however the tree is expanded.
#
# `qt.depth` IS THE ORACLE AND IT HAD TO EXIST BEFORE THE CLAIM COULD BE
# CHECKED -- the fourth time in this backend that asking "what would be
# observable if this did nothing?" produced a verb. A populate that IGNORED the
# depth builds a flat tree whose every name, index, selection and refresh reads
# back correctly, so nothing else in the fixture can see it. It answers the
# depth THE WIDGET HAS, walked up the item's parents, rather than the number
# the record carried.
. "$(dirname "$0")/portable.sh"
# NO `set -e`: the `set +e` lines below are not paired with a restore, because
# this header omits -e and a bare `set -e` would ARM it (run_portable.sh
# tripwires for that, and it caught run_qt_children.sh within minutes).
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

# -------------------------------------------------------------- DECLARATION
# RUNS WHETHER OR NOT QT IS IN THE BUILD: claims about the load-time pass.
printf 'TIER declaration: a tree shows, is named, and holds fields\n'
decl() {                        # label, window body, expected fragment ("" = accept)
    cat > "$work/d.bas" <<BAS
window app( title: "D", width: 300, height: 200 )
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
decl "a tree to show its rows is admitted" '    tree files to show rows
        field name(  label: "File" )
        field depth( depth: true )
    end tree' ""
# A TREE IS READ, NOT EDITED, and the refusal names what it has rather than
# listing what it lacks. `ask` is a TABLE'S third direction (§0.25) and gets
# the same treatment, because a tree's rows come from a list it can see.
decl "a tree to EDIT is refused" '    tree files to edit rows
        field name( label: "File" )
    end tree' "a tree is \`to show\`"
decl "a tree to ASK is refused" '    tree files to ask cells
        field name( label: "File" )
    end tree' "a tree is \`to show\`"
# ITS NAME IS REQUIRED, for the reason a table's is: the rows are many, so the
# TREE is what a handler and `qt.selected` address.
decl "a nameless tree is refused" '    tree to show rows
        field name( label: "File" )
    end tree' "a tree needs a name"
# ITS PARTS ARE COLUMNS, like a table's -- a button among them would be a
# widget in a place that holds values.
decl "a button inside a tree is refused" '    tree files to show rows
        field name( label: "File" )
        button go( label: "Go" )
    end tree' "button"
# TWO DEPTH COLUMNS IS A DECLARATION WITH TWO ANSWERS, so it is refused rather
# than one of them silently winning.
decl "two depth fields are refused" '    tree files to show rows
        field name(  label: "File" )
        field depth( depth: true )
        field level( depth: true )
    end tree' "one depth"
# AND NO DEPTH FIELD IS LEGAL: a flat tree is an ordinary list, which is what a
# program that owns its own expansion hands over.
decl "a tree with no depth field is admitted (a flat one)" '    tree files to show rows
        field name( label: "File" )
    end tree' ""

# -------------------------------------------------------------- SEMANTICS
printf 'TIER semantics: the rows, the depth, the selection and the refresh\n'
if ! build_has qt; then
    printf '  SKIP (no Qt in this build -- the tree was NOT measured)\n'
else
    set +e
    QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 60 \
        ./gbasic --line-buffered tests/qt_tree_test.bas \
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
        elif [ "${checks:-0}" -lt 15 ]; then
            fail "only ${checks:-0} checks ran -- the fixture stopped early"
        else
            pass "$checks checks, 0 mismatches"
        fi
        if [ -s "$work/sem.err" ] \
           && grep -vqE "This plugin does not support|^$" "$work/sem.err"; then
            head -3 "$work/sem.err"; fail "the fixture wrote to stderr"
        else
            pass "and nothing on stderr"
        fi
    fi
fi

# -------------------------------------------------------------- VALGRIND
printf 'TIER valgrind: no invalid access across a populate and a refresh\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
elif ! build_has qt; then
    printf '  SKIP (no Qt in this build)\n'
else
    # A REFRESH REBUILDS EVERY ITEM, so the old ones are deleted while the
    # window is open -- the path where a stale pointer would be a
    # use-after-free that a correct answer would not reveal.
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_tree_test.bas \
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
            pkill -KILL -f "gbasic tests/qt_tree_test.bas" 2>/dev/null
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
        pass "no invalid access across a populate, a refresh and teardown"
    fi
    # THE ABSENCE IS ASSERTED, the lesson run_qt_dialog.sh and run_qt_modal.sh
    # both paid for: a bound that kills the waiter orphans the child.
    if pgrep -f "gbasic tests/qt_tree_test.bas" >/dev/null 2>&1; then
        pkill -KILL -f "gbasic tests/qt_tree_test.bas" 2>/dev/null
        fail "the valgrind tier left a gbasic process behind"
    else
        pass "and nothing was left running afterwards"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_tree: all checks passed\n'
else
    printf 'run_qt_tree: FAILED\n'
fi
exit "$status"
