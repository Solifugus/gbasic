#!/usr/bin/env bash
# `split` AND `scroll` (docs/qt_gui_design.md §0.19) -- a row or column you can
# DRAG, and a region that SCROLLS.
#
# `set +e` BELOW WITHOUT A MATCHING `set -e`, DELIBERATELY: this file's mode is
# `set -uo pipefail` -- no `-e` -- so `set +e` is a no-op and a `set -e` after
# it would TURN THE MODE ON, which costs nothing until a suite goes red and
# then kills it mid-tier with no verdict. run_portable.sh tripwires it -- AND
# CAUGHT THIS FILE, hours after that tripwire was added: the five `set -e`
# lines came in with the template this suite was written from, with the comment
# above them explaining why they must not be there. A note is not a guard.
#
# DERIVED FROM gBASIC STUDIO'S OWN SOURCE rather than guessed, which is the
# reason these two came first: its shell is `gtk.paned` THREE TIMES NESTED and
# `gtk.scrolled` FIVE TIMES, and nothing in this vocabulary could express
# either. `row` and `column` are boxes nobody can drag; `source`, `memo` and
# `table` scroll themselves while an ordinary column of widgets does not. One
# increment because they are one layout.
#
# FOUR THINGS WERE MEASURED IN C++ FIRST AND THREE SHAPED THE RESULT:
#
#   QSplitter has NO layout        like a QTabWidget -- so the layout guards
#                                  had to learn about it
#   PARENTING IS ENOUGH            count() went to 2 from two
#                                  `new QLabel(..., sp)` calls, no addWidget
#   orientation is a Q_PROPERTY    Horizontal=1 Vertical=2, and it accepts the
#                                  string "Vertical" too
#   widgetResizable DEFAULTS FALSE the trap: left alone the inner widget keeps
#                                  its original size for ever, so content never
#                                  reflows and a resized window shows a stripe
#                                  of widgets beside a grey expanse
#   ONE PANE HAS ZERO HANDLES      n panes give n-1, so a one-pane split is a
#                                  container nobody can drag
#
# AND `sizes` IS NEITHER A PROPERTY NOR INVOKABLE (it takes a QList<int>), so
# saving and restoring pane widths needs its own verb and is NOT in this
# increment -- stated rather than discovered.
#
# THE TIER TO READ FIRST IS NESTING, inside the semantics fixture. A `split`
# spends its one free slot on the DIRECTION (§0.8 measured that a container
# carries a name OR options, never both), so it cannot be named -- nothing can
# address it, and nothing could say whether it was built, which way round it is
# or how many panes it got. `qt.pane` is the oracle and was built BEFORE the
# fixture, asked of the LAYOUT by walking up from a part that CAN be named,
# which is the rule `qt.at` already follows for a grid.
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
# A NAMED LEAF WITH NO OPTIONS became legal in this increment -- `source code()`
# -- and it is here because this increment's own natural shape hit it: a PANE
# needs no label, so the thing an author writes has nothing to put in the
# parentheses, and `field who()` was a parse error. MEASURED AT ZERO
# CONFLICTS before being kept, which is not decoration in a project that
# rejected `IDENT expression` as a statement form over FOUR.
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
        cat "$work/bison.err" | head -4
        fail "the grammar now reports conflicts"
    fi
fi

# ------------------------------------------------------------- THREE GUARDS
# A SPLITTER HAS NO LAYOUT, so every door that asks a parent for one had to
# learn about it -- `gb_qt_container`, `gb_qt_part` and `gb_qt_table`, found one
# at a time by RUNNING the thing (a table as a direct pane was refused with
# "that container cannot hold a table", a true sentence about the wrong thing).
# A FOURTH DOOR WOULD REFUSE PANES IN SILENCE, so the two counts are held equal:
# every `!p->layout` guard must also admit a splitter. Reads the source, because
# no behavioural check can see a door nobody has walked through yet.
printf 'TIER guards: every layout guard admits a splitter parent\n'
# CHECKED BY CONDITION, NOT BY LINE, which a failure forced: counting the two
# greps made the pane-label refusal below -- which legitimately tests
# `!p->layout` in order to FIRE on a splitter -- read as a fourth door that had
# not learned. The scanner balances the parentheses of each `if` that mentions
# `!p->layout` and asks whether THAT condition also names a splitter.
cat > "$work/guards.py" <<'PYEOF'
import re, sys
src = open(sys.argv[1]).read()
bad, total = [], 0
for m in re.finditer(r'!p->layout', src):
    # walk back to the opening `if (`, then forward to its matching `)`
    start = src.rfind('if (', 0, m.start())
    if start < 0:
        bad.append('a `!p->layout` outside any if'); continue
    i, depth = start + 3, 0
    while i < len(src):
        if src[i] == '(': depth += 1
        elif src[i] == ')':
            depth -= 1
            if depth == 0: break
        i += 1
    cond = src[start:i + 1]
    total += 1
    # EITHER SPELLING NAMES A SPLITTER: an inline `qobject_cast<QSplitter *>`
    # or the local the function already made (`psplit`). Asking for only the
    # first reported two conditions that were perfectly well guarded.
    if 'QSplitter' not in cond and 'psplit' not in cond:
        bad.append(cond.replace('\n', ' ')[:110])
print('conditions=%d' % total)
for b in bad:
    print('UNGUARDED %s' % b)
sys.exit(1 if bad else 0)
PYEOF
set +e
python3 "$work/guards.py" src/modules/qt_backend.cpp > "$work/guards.out" 2>&1
g_rc=$?
nconds=$(sed -n 's/^conditions=//p' "$work/guards.out")
if [ "$g_rc" = "0" ] && [ "${nconds:-0}" -ge 4 ]; then
    pass "all $nconds layout conditions name a splitter"
else
    cat "$work/guards.out"
    # TWO WAYS TO FAIL AND THE MESSAGE NAMES BOTH, because a perturbation that
    # DELETED a condition reported the other one: fewer conditions than the
    # four that exist means a door was removed or renamed, which is as worth
    # knowing as one that forgot about splitters.
    fail "expected >=4 layout conditions all admitting a splitter, got ${nconds:-0} with $(grep -c UNGUARDED "$work/guards.out" || true) unguarded -- a pane would be refused in silence, or a guard has moved"
fi
# THE VACUITY CONTROL: "all of them" is equally satisfied by a scanner that
# found none, so a copy with one admission removed must break it.
sed '0,/!qobject_cast<QSplitter \*>(p->widget)/{s/!qobject_cast<QSplitter \*>(p->widget)/1/}' \
    src/modules/qt_backend.cpp > "$work/one_less.cpp"
set +e
python3 "$work/guards.py" "$work/one_less.cpp" > "$work/one_less.out" 2>&1
l_rc=$?
if [ "$l_rc" != "0" ]; then
    pass "CONTROL: and a copy with one admission removed is reported"
else
    fail "CONTROL: the scanner passed a copy with an admission removed, so it is measuring nothing"
fi

# -------------------------------------------------------------- DECLARATION
# RUNS WHETHER OR NOT QT IS IN THE BUILD, because these are claims about the
# DECLARATION -- the property that let the whole window vocabulary ship before
# any backend existed.
printf 'TIER declaration: a split is `across` or `down`, and needs two panes\n'
decl() {                        # name, window body, expected fragment
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

decl 'a split with no direction is refused' \
'    split
        field a( label: "a" )
        field b( label: "b" )
    end split' \
    'a split needs a direction'
decl 'a misspelled direction gets the near miss' \
'    split acros
        field a( label: "a" )
        field b( label: "b" )
    end split' \
    'did you mean `across`?'
decl 'and one nothing is close to gets both words' \
'    split sideways
        field a( label: "a" )
        field b( label: "b" )
    end split' \
    'is `across` or `down`'
decl 'the direction is a word, not a string' \
'    split "across"
        field a( label: "a" )
        field b( label: "b" )
    end split' \
    'not a string'
# ONE PANE HAS NOTHING TO DRAG, measured: n panes give n-1 handles.
decl 'a split with one pane is refused, naming the remedy' \
'    split across
        field a( label: "a" )
    end split' \
    'you want a `row` or a `column`'
# AND THE CONTROLS, which outnumber nothing here on purpose: a rule that fires
# on correct declarations is worse than no rule.
decl 'CONTROL: a correct split is accepted' \
'    split across
        field a()
        field b()
    end split' \
    ''
decl 'CONTROL: and so is the other direction, nested' \
'    split down
        field a()
        split across
            field b()
            field c()
        end split
    end split' \
    ''
decl 'CONTROL: a check keeps its own caption as a pane' \
'    split across
        check a( label: "On hold" )
        field b()
    end split' \
    ''
decl 'CONTROL: a scroll may be named, since it needs no direction' \
'    scroll files
        field a()
    end scroll' \
    ''
decl 'CONTROL: and a scroll needs no name either' \
'    scroll
        field a()
    end scroll' \
    ''
# THE DIRECTION IS NOT A HANDLER TARGET, which is what says the slot is a
# direction rather than a name that happens to be spelled `across`.
decl 'a handler on the direction word names nothing' \
'    split across
        field a()
        field b()
    end split
    on across.clicked
        print "never"
    end on' \
    'names nothing in this window'
# AND A BUILT RECORD GETS THE SAME RULES, one checker and two adapters (§0.9).
cat > "$work/built.bas" <<'BAS'
program main(args)
    w = { kind: "window", name: "app", options: { title: "B" },
          items: [ { kind: "container", word: "split", name: "sideways",
                     items: [] } ] }
    print "CHECK " + qt.check(w)
end program
BAS
set +e
./gbasic "$work/built.bas" > "$work/built.out" 2>&1
if grep -q 'is `across` or `down`' "$work/built.out"; then
    pass "a BUILT split gets the same rules"
else
    head -2 "$work/built.out"
    fail "the record adapter missed the direction rule"
fi

if ! build_has qt; then
    printf 'SKIP run_qt_split (no Qt in this build -- NOTHING about dragging or scrolling was measured)\n'
    exit 0
fi

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: Studio shell shape, the oracle, and the resizable trap\n'
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic \
    --line-buffered tests/qt_split_test.bas > "$work/sem.out" 2> "$work/sem.err"
rc=$?
if grep -q "SKIP no display" "$work/sem.out"; then
    printf '  SKIP (no display -- split and scroll were NOT exercised)\n'
elif [ "$rc" != "0" ]; then
    grep MISMATCH "$work/sem.out" | head -4
    tail -3 "$work/sem.out"; tail -3 "$work/sem.err"
    fail "the fixture exited $rc"
else
    checks=$(grep -c '^ok' "$work/sem.out" || true)
    bad=$(grep -c 'MISMATCH' "$work/sem.out" || true)
    if [ "$bad" = "0" ] && [ "${checks:-0}" -ge 27 ]; then
        pass "$checks checks, 0 mismatches"
    else
        grep MISMATCH "$work/sem.out" | head -6
        fail "$checks checks, $bad mismatches (want >= 27 and none)"
    fi
    grep -v 'propagateSizeHints' "$work/sem.err" > "$work/sem.err2" || true
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing of its own on stderr"
    fi
    # NAMED, so a failure says which claim went rather than only moving a count.
    if grep -q "^ok   and the outer split holds two panes" "$work/sem.out"; then
        pass "NESTING: an inner split really is a pane of the outer one"
    else
        fail "the nesting check did not run -- a split hung off the window instead of its parent would pass everything else"
    fi
    if grep -q "^ok   the oracle says it was set" "$work/sem.out" &&
       grep -q "^ok   and so does the by-name read" "$work/sem.out"; then
        pass "widgetResizable was set, asserted through both doors"
    else
        fail "the resizable trap was not measured -- Qt defaults it FALSE and content then never reflows"
    fi
fi

# --------------------------------------------------------------- PANE LABEL
# THE SILENT-DISCARD GUARD, and it is HERE rather than in the declaration tier
# for a reason worth keeping: it was written there first and COULD NOT WORK.
# The validator's `label()` reads a TOP-LEVEL label -- what a `group "Credit"`
# carries -- while A LEAF'S `label:` IS AN OPTION, so it read NULL for every
# leaf and the check silently never fired. That is §0.8's own recorded defect
# reproduced: the table heading extraction read the top-level label and fell
# back to the column's name. Deciding it needs a leaf's OPTIONS, which only the
# Qt side has.
printf 'TIER pane label: a caption the parent would have placed is refused\n'
cat > "$work/pl.bas" <<'BAS'
window app( title: "PL", width: 300, height: 160 )
    split across
        field a( label: "Name" )
        field b()
    end split
end window
program main(args)
    if not qt.can_display() then
        print "NO DISPLAY"
        return
    end if
    on error goto refused
    qt.open(app)
    print "ACCEPTED"
    return
refused:
    print "REFUSED " + error.message
    error.clear()
end program
BAS
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 40 ./gbasic \
    --line-buffered "$work/pl.bas" > "$work/pl.out" 2>&1
if grep -q "NO DISPLAY" "$work/pl.out"; then
    printf '  SKIP (no display)\n'
elif grep -q "nowhere to put a label" "$work/pl.out"; then
    pass "a field carrying a label as a direct pane is refused, naming the remedy"
else
    head -2 "$work/pl.out"
    fail "a pane label was accepted and would have been dropped in silence"
fi
# ITS CONTROL IS THE DECLARATION TIER ABOVE -- a `check` consumes its own
# caption at construction, so it must still be allowed one as a pane. Without
# that the rule could widen into one that fires on correct declarations, which
# is the failure that got the blind-shadow warning reverted at 287 false
# positives.

# ------------------------------------------------------------------ VALGRIND
# ACCESS-ONLY, for the reason run_qt_backend.sh records: Qt does not free its
# platform plugin, font configuration or QApplication statics, so a leak claim
# would be a suppression file describing Qt. What is new here is a widget tree
# whose parents have NO LAYOUT, so a child is enrolled by reparenting rather
# than by a layout call -- and `qt.pane` walks that tree afterwards.
printf 'TIER valgrind: no invalid access across three windows and the walk\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_split_test.bas \
        >/dev/null 2>"$work/vg.err"
    vg_rc=$?
    if [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    else
        pass "no invalid access across a splitter, a scroll area and the pane walk"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_split: all checks passed\n'
else
    printf 'run_qt_split: FAILED\n'
fi
exit "$status"
