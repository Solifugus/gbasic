#!/usr/bin/env bash
# PANE SIZES (docs/qt_gui_design.md §0.22) -- `qt.pane(...).sizes` and
# `qt.resize_panes`, which is how "restore my layout" is written.
#
# `set +e` BELOW WITHOUT A MATCHING `set -e`, DELIBERATELY: this file's mode is
# `set -uo pipefail` -- no `-e` -- so `set +e` is a no-op and a `set -e` after
# it would TURN THE MODE ON, which costs nothing until a suite goes red and
# then kills it mid-tier with no verdict. run_portable.sh tripwires it.
#
# §0.19 LEFT THIS OPEN FOR TWO REASONS AND BOTH STILL HOLD: `setSizes` takes a
# QList<int>, so measured, `sizes` is NOT a property (-1) and `setSizes` is NOT
# invokable by name (-1) -- the by-name route this backend reaches everything
# else through cannot see either -- and a `split` CANNOT BE NAMED, its one slot
# being the direction, so there is nothing to look up. It is therefore
# addressed by WALKING UP FROM A PART, the rule `qt.pane` already follows, so
# `sizes` joins what that answers and needed no new spelling.
#
# STUDIO NEEDS IT, which is why this came before icons and completion: its
# shell reads `split.position` for THREE splits out of a saved session and
# writes them back on exit, so a converted Studio without this forgets its
# layout every time -- a feature it already has. GTK's `position` is ONE number
# because a Gtk.Paned has exactly two children; a QSplitter holds any number,
# so this is a LIST.
#
# THREE MEASUREMENTS SHAPED IT, and the first is why NOTHING here asserts an
# exact size:
#
#   setSizes IS NOT EXACT          asked 260/540 of an 800-wide splitter, got
#                                  252/522 -- the handle and the margins
#   a WRONG COUNT fails silently   [100] on a two-pane split gave [774, 0],
#                                  COLLAPSING the second; [200,300,400] gave
#                                  [310,464], honouring none of the three
#   a window resize RE-PROPORTIONS 194/580 became 244/730
#
# So a caller stores numbers whose MEANING is their proportion, the checks are
# ratios and differences, and a count mismatch is REFUSED rather than handed to
# Qt -- both of those silent outcomes being perfectly ordinary-looking layouts
# that are not what was asked for.
#
# THE TIER TO READ FIRST IS INNER, inside the semantics fixture: resizing the
# INNER split must leave the OUTER alone. Every size check in the file passes on
# a walk that resized whichever splitter it met first, and that is the one that
# does not.
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
# No grammar at all: `sizes` joins a record and the writer is a verb. Asserted
# as a standing tier, which is not decoration in a project that rejected
# `IDENT expression` as a statement form over FOUR measured conflicts.
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

# -------------------------------------------------------------- ONE WALK UP
# `qt.pane` and `qt.resize_panes` must find the SAME splitter, or a program
# would read one split's sizes and write another's. One walk, used by both --
# the rule `would`/`execute` follow in `automation`, and the reason the INNER
# tier below can be trusted to mean what it says.
printf 'TIER one walk: the reader and the writer find the same splitter\n'
walks=$(grep -c 'static QSplitter \*splitter_of' src/modules/qt_backend.cpp || true)
users=$(grep -c 'splitter_of(window, name, err, err_size)' src/modules/qt_backend.cpp || true)
if [ "${walks:-0}" = "1" ] && [ "${users:-0}" = "2" ]; then
    pass "one walk, two callers"
else
    fail "expected 1 walk / 2 callers, got ${walks:-0}/${users:-0} -- two walks can disagree about which split a part is in"
fi
# THE VACUITY CONTROL, counting OCCURRENCES rather than lines, because a second
# call written by hand goes on one line -- the way the equivalent control in
# run_qt_modal.sh was found unable to fail.
sed 's/splitter_of(window, name, err, err_size)/splitter_of(window, name, err, err_size) \/* and again *\/ splitter_of(window, name, err, err_size)/' \
    src/modules/qt_backend.cpp > "$work/more.cpp"
planted=$(grep -o 'splitter_of(window, name, err, err_size)' "$work/more.cpp" | wc -l)
if [ "${planted:-0}" -gt 2 ]; then
    pass "CONTROL: planted extra calls are counted ($planted)"
else
    fail "CONTROL: the scan counts lines rather than occurrences"
fi

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: the sizes, the ratio, the inner split and the refusals\n'
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic \
    --line-buffered tests/qt_panes_test.bas > "$work/sem.out" 2> "$work/sem.err"
rc=$?
if grep -q "SKIP no display" "$work/sem.out"; then
    printf '  SKIP (no display -- pane sizes were NOT exercised)\n'
elif [ "$rc" != "0" ]; then
    grep MISMATCH "$work/sem.out" | head -4
    tail -3 "$work/sem.out"; tail -3 "$work/sem.err"
    fail "the fixture exited $rc"
else
    checks=$(grep -c '^ok' "$work/sem.out" || true)
    bad=$(grep -c 'MISMATCH' "$work/sem.out" || true)
    if [ "$bad" = "0" ] && [ "${checks:-0}" -ge 30 ]; then
        pass "$checks checks, 0 mismatches"
    else
        grep MISMATCH "$work/sem.out" | head -6
        fail "$checks checks, $bad mismatches (want >= 30 and none)"
    fi
    grep -v 'This plugin does not support' "$work/sem.err" > "$work/sem.err2" || true
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing of its own on stderr"
    fi
    # NAMED, so a failure says which claim went rather than only moving a count.
    if grep -q "^ok   resizing the inner split left the outer alone" "$work/sem.out" &&
       grep -q "^ok   and the inner one moved" "$work/sem.out"; then
        pass "INNER: the walk found the right splitter, not the first one"
    else
        fail "the nesting was not measured -- every size check passes on a walk that resized whichever splitter it met first"
    fi
    if grep -q "^ok   so the two settings really differ" "$work/sem.out" &&
       grep -q "^ok   and about a quarter of the width" "$work/sem.out"; then
        pass "RATIO: asserted as a proportion and a difference, never a value"
    else
        fail "the sizes were not measured as a ratio -- setSizes is NOT exact (260/540 gave 252/522), so a value check fails on a correct build"
    fi
    if grep -q "^ok   too few sizes is refused, naming both counts" "$work/sem.out" &&
       grep -q "^ok   and too many" "$work/sem.out"; then
        pass "COUNT: refused both ways, naming both numbers"
    else
        fail "a count mismatch was not refused -- measured, one size COLLAPSES a pane and three honour none of them, both silently"
    fi
    if grep -q "^ok   a three-pane split says three" "$work/sem.out" &&
       grep -q "^ok   the three are in increasing order" "$work/sem.out"; then
        pass "THREE: more than two panes, which a Gtk.Paned cannot even have"
    else
        fail "only two panes were measured -- a two-pane-only implementation passes everything else"
    fi
    if grep -q "^ok   a pane can be collapsed" "$work/sem.out" &&
       grep -q "^ok   and brought back" "$work/sem.out"; then
        pass "COLLAPSE: 0 is legal, which is how a panel hides"
    else
        fail "collapsing was not measured -- 0 is how a shell hides a panel without forgetting it"
    fi
    # THE SCROLL BOUNDARY, both ways. A part inside a `scroll` that is itself a
    # pane reports the SCROLL, because the walk stops at the nearest region --
    # and the control is that a `table` which IS a direct pane is still found,
    # without which the refusal is satisfied by a walk that refuses everything
    # that is not a plain widget.
    if grep -q "^ok   a part whose nearest region is a scroll is refused" "$work/sem.out" &&
       grep -q "^ok   a table as a direct pane reports its split" "$work/sem.out"; then
        pass "BOUNDARY: the walk stops at the nearest region, and still finds a real pane"
    else
        fail "the nearest-region boundary was not measured both ways"
    fi
fi

# ------------------------------------------------------------------ VALGRIND
# ACCESS-ONLY, for the reason run_qt_backend.sh records. What is NEW here is an
# int array crossing the seam in both directions, and a two-call protocol whose
# first call reports a count the second trusts.
printf 'TIER valgrind: no invalid access across the size arrays\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_panes_test.bas \
        >/dev/null 2>"$work/vg.err"
    vg_rc=$?
    if [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    else
        pass "no invalid access across the size arrays and the walks"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_panes: all checks passed\n'
else
    printf 'run_qt_panes: FAILED\n'
fi
exit "$status"
