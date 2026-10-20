#!/bin/bash
# A MARGIN ON `source` CARRYING A MARK PER LINE (§0.29), and the click that
# makes a breakpoint real.
#
# THE SECOND OF THE TWO ASKS docs/qt_studio_requests.md NAMES AS BLOCKING a
# feature gBASIC Studio already has (`tree`, §0.28, was the first -- that
# document's own words: *"Two of these are gaps that block a feature Studio
# already has"*). Studio's gutter is NOT a ruler: it carries a `▶` at the start
# line of every runnable SECTION, which is the whole run-a-cell affordance, and
# an icon at every parse DIAGNOSTIC, which is the only thing in that window
# saying WHERE a syntax error is. The user widened it on reading the word:
# *"I was only thinking line numbers and forgot about break points and other
# things."*
#
# SO THE CATEGORY SET IS DERIVED RATHER THAN CHOSEN, which is what keeps it
# closed: two were asked for (`section`, `break`) and the other three ARE
# gBASIC's own diagnostic severities -- `gb_severity` is exactly {error,
# warning, note} -- so a program showing diagnostics has one category per
# severity instead of flattening three into one. Nothing here is invented, and
# the set CAN be closed because a category decides a GLYPH and a PRIORITY and
# the backend has to know both; a leaf's properties cannot be (75 on a button),
# which is why that set is open and this one is refused by name.
#
# THE PRIORITY IS THE ENUM ORDER so there is no second table to drift, and it
# is two rules: a mark the PERSON placed outranks one the program COMPUTED
# (`break` wins everything, because a breakpoint is the one mark somebody set
# deliberately and must see in order to remove), and among computed marks the
# more urgent wins -- error, warning, note, with `section` lowest but above
# `note`, which preserves the order Studio's own request states (section 1,
# error 2 "wins") and keeps the run affordance from being hidden by the least
# urgent thing there is.
#
# LINE 1 IS THE FIRST LINE, MEASURED RATHER THAN CHOSEN: `source_outline`
# reports `start_line = 1` and a parse diagnostic carries a 1-based line, so
# BOTH of the consumer's own sources are 1-based and the gutter displays
# 1-based -- which means there is NO CONVERSION ANYWHERE, and a conversion is
# exactly where the off-by-one would live. Line 0 is refused rather than
# dropped, because a 0-based caller is the plausible mistake and a silently
# dropped mark reports success while drawing nothing.
#
# IT COSTS NO GRAMMAR AT ALL -- `line_numbers:` and `marks:` are options and
# the four verbs are verbs -- so the zero-conflict tier here is the standing
# claim rather than a measurement of this increment, and says so.
#
# THE TIER TO READ FIRST IS `paint`, because it is the claim gBASIC CANNOT
# MAKE: `qt.marks` answers WHAT WAS SET, deliberately (a read filtered to the
# drawable lines would disagree with the write for a reason the caller cannot
# see), so a backend that stored five lists and painted nothing satisfies every
# check in the fixture. A QWidget's pixels are not reachable from gBASIC at
# all. tests/qt/gutter_probe.cpp links the REAL backend object and drives the
# same `gb_qt_*` seam `src/modules/qt.c` uses -- which is the difference from
# tests/qt/source_probe.cpp and is strictly stronger, since what it measures is
# the painter the product runs rather than a second copy that could be right
# while the first is wrong. Three things make its numbers evidence: the count
# is ZERO before any turn of the loop (§0.25's lesson measured once more --
# nothing paints offscreen without one), it paints only the VISIBLE lines of a
# 200-line document (so it walked real block geometry rather than looping), and
# six marked lines of which one carries TWO categories paint SIX glyphs, which
# is the priority rule and would be seven without it.
#
# WHAT IS DELIBERATELY NOT HERE: the glyph is not configurable. §0.23's three
# icon sources were measured and a theme name resolves to NOTHING on a machine
# with no icon theme -- measured, this one has none -- which is exactly why
# Studio ships its own PNG for the error marker. Four of the five glyphs come
# from QStyle standard pixmaps, which are part of Qt Widgets and need no theme
# (all five measured non-null 16x16 here), and `break` is PAINTED, needing
# neither style nor font. That removes the reason the ask mentioned icons at
# all; if a real need to change one appears, that need is the argument.
. "$(dirname "$0")/portable.sh"
# NO `set -e`: the `set +e` lines below are not paired with a restore, because
# this header omits -e and a bare `set -e` would ARM it (run_portable.sh
# tripwires for exactly that).
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/build_has.sh
source tests/valgrind_tier.sh
# THE BUILD IS CHECKED, which this suite's own perturbation round forced: a
# failed `make` here used to leave the suite running the PREVIOUSLY BUILT
# binary and reporting green, so a perturbation that did not compile looked
# like one the checks could not see. run_all.sh guards its own up-front build
# (line 166) and a suite run BY HAND had nothing -- which is exactly the way
# these are exercised.
if ! make >/dev/null; then
    printf 'run_qt_gutter: FAILED (the build failed -- nothing was measured)\n'
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# ------------------------------------------------------------------ GRAMMAR
printf 'TIER grammar: still zero conflicts (this increment adds none)\n'
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

# ------------------------------------------------------------- ONE VOCABULARY
# RUNS WITH NO QT: a claim about the source. The five category names and their
# priority live in ONE place, and the module ASKS rather than spelling them
# again -- two copies of a closed vocabulary is the drift this tree tripwires
# against, and this very vocabulary was written out three times before
# `win_nearest` was derived from the tables, with the newest word missing from
# the third.
printf 'TIER one vocabulary: the categories are named in one place and asked for\n'
# THE SCAN TAKES A PATH, which the GATE'S OWN LANE CHECK FORCED rather than
# taste: the first version planted its control copy in `src/modules/qt.c`
# itself and restored it, and `src/` IS A BUILD INPUT -- so the scan reported
# this suite as one that rebuilds the tree, which in a parallel gate means
# another suite's plain `make` compiles the planted file. Correct, and caught
# within minutes of the suite existing, the way run_portable.sh caught
# run_qt_children.sh arming `-e`. A scan reads a file, so it can read a COPY.
spells_a_category() {                   # path -> prints each name it spells
    for name in section warning note break; do
        n=$(grep -c "\"$name\"" "$1" || true)
        [ "${n:-0}" != "0" ] && printf '%s\n' "$name"
    done
}
cat_dupes=$(spells_a_category src/modules/qt.c | wc -l)
asks=0
for fn in gb_qt_mark_kind gb_qt_mark_name gb_qt_mark_count; do
    grep -q "$fn" src/modules/qt.c && asks=$((asks + 1))
done
if [ "${cat_dupes:-1}" != "0" ]; then
    spells_a_category src/modules/qt.c | sed 's/^/     src\/modules\/qt.c spells /'
    fail "the module holds its own copy of the category names"
elif [ "$asks" != "3" ]; then
    fail "the module does not ask the backend for the categories ($asks of 3)"
else
    pass "the module asks (3 accessors) and spells no category itself"
fi
# THE CONTROL, because "no duplicates" is equally satisfied by a scan that read
# nothing -- planted in a COPY, so nothing under src/ is written.
cp src/modules/qt.c "$work/planted.c"
printf 'static const char *dupe[] = { "section", "warning", "note", "break" };\n' \
    >> "$work/planted.c"
planted=$(spells_a_category "$work/planted.c" | wc -l)
if [ "${planted:-0}" = "4" ]; then
    pass "and the scan finds a planted copy (so it is reading the file)"
else
    fail "the scan found ${planted:-0} of 4 planted names -- it is reading nothing"
fi

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: the margin, the marks, the click and the refusals\n'
if ! build_has qt; then
    printf '  SKIP (no Qt in this build -- the margin was NOT measured)\n'
else
    set +e
    QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        GBASIC_SYNTAX_PATH=editors/kate timeout -k 5 90 \
        ./gbasic --line-buffered tests/qt_gutter_test.bas \
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
        elif [ "${checks:-0}" -lt 25 ]; then
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

# ---------------------------------------------------------------- PAINT
# THE CLAIM gBASIC CANNOT MAKE. Links the real backend object, so what is
# measured is the painter the product runs.
printf 'TIER paint: the margin really draws, and only what is visible (C++)\n'
if ! build_has qt; then
    printf '  SKIP (no Qt in this build -- the painting was NOT measured)\n'
elif ! command -v g++ >/dev/null 2>&1; then
    printf '  SKIP (no C++ compiler -- the painting was NOT measured)\n'
elif ! g++ -std=c++17 -fPIC -Iinclude -Isrc/modules -o "$work/gprobe" \
        tests/qt/gutter_probe.cpp src/modules/qt_backend.o \
        $(pkg-config --cflags --libs Qt6Widgets) \
        $(pkg-config --libs KF6SyntaxHighlighting 2>/dev/null \
          || echo -lKF6SyntaxHighlighting) 2>"$work/cxx.err"; then
    head -8 "$work/cxx.err"
    fail "the paint probe did not build"
else
    set +e
    QT_QPA_PLATFORM=offscreen timeout -k 5 60 "$work/gprobe" \
        > "$work/paint.out" 2>/dev/null
    prc=$?
    # NO `set -e` RESTORE: this header omits -e, so that line would ARM it
    # rather than restore it -- and the cost falls EXACTLY on a red path, where
    # an unguarded `grep` finding nothing kills the suite mid-tier with no
    # verdict. run_portable.sh's tripwire caught this one within minutes of the
    # suite existing, which is the fourth time a tripwire here has caught my
    # own brand-new work.
    pfield() { sed -n "s/.*[[:space:]]*$1=\([^ ]*\).*/\1/p" "$work/paint.out" \
                   | head -1; }
    if [ "$prc" = "2" ]; then
        printf '  SKIP (the probe found no display -- painting was NOT measured)\n'
    elif [ "$prc" != "0" ]; then
        cat "$work/paint.out"; fail "the paint probe did not run (exit $prc)"
    else
        before=$(pfield before_lines)
        plines=$(pfield painted_lines)
        pmarks=$(pfield painted_marks)
        blines=$(pfield bare_lines)
        bmarks=$(pfield bare_marks)
        total=$(pfield total)
        # NOTHING PAINTS WITHOUT A TURN OF THE LOOP -- §0.25's lesson, and the
        # check that makes every number after it evidence rather than a
        # constant somebody set.
        if [ "${before:-1}" != "0" ]; then
            cat "$work/paint.out"
            fail "the counters were already $before before any turn of the loop"
        else
            pass "nothing was painted before the loop turned (counters are real)"
        fi
        if [ "${plines:-0}" -gt 0 ] && [ "${plines:-0}" -lt "${total:-200}" ]; then
            pass "painted $plines of $total lines -- only what is visible"
        else
            cat "$work/paint.out"
            fail "painted ${plines:-0} of ${total:-200}: it drew nothing, or the whole document"
        fi
        # SIX MARKED LINES, ONE CARRYING TWO CATEGORIES: six glyphs is the
        # priority rule, seven is both drawn, and anything else is neither.
        if [ "${pmarks:-0}" = "6" ]; then
            pass "six marked lines painted six glyphs (the priority rule holds)"
        else
            cat "$work/paint.out"
            fail "painted ${pmarks:-0} glyphs for six marked lines (7 = the priority rule is not applied)"
        fi
        # THE FIRST LINE IS NUMBERED 1, asserted on the number the painter
        # actually DREW rather than on a recomputation of it -- which is what
        # makes it a check on the 1-based display rather than a tautology.
        pfirst=$(pfield first_line)
        if [ "${pfirst:-0}" = "1" ]; then
            pass "the top of the document is numbered 1, not 0"
        else
            cat "$work/paint.out"
            fail "the first line drawn was numbered ${pfirst:-0}"
        fi
        # AND THE MARGIN FOLLOWS A SCROLL, which is what `updateRequest` is
        # for. PRODUCED BY A PERTURBATION THAT WENT GREEN: with that connect
        # removed the numbers sit still while the text moves under them, and
        # NOTHING in this suite could see it -- every other check is taken at
        # the top of the document, so they all stayed green. The first painted
        # line is what changes when the view moves.
        sfirst=$(pfield scrolled_first)
        if [ "${sfirst:-0}" -gt 100 ]; then
            pass "scrolled to the bottom, the margin repainted from line $sfirst"
        else
            cat "$work/paint.out"
            fail "after scrolling the margin still started at line ${sfirst:-0} -- it does not follow the view"
        fi
        # THE CONTROL, in the same process: numbers and NO marks must paint
        # numbers and no glyphs, without which "six glyphs" is satisfied by a
        # painter that draws one on every line.
        if [ "${blines:-0}" -gt 0 ] && [ "${bmarks:-1}" = "0" ]; then
            pass "a margin with numbers and no marks paints $blines lines and no glyphs"
        else
            cat "$work/paint.out"
            fail "the control painted ${blines:-0} lines and ${bmarks:-1} glyphs"
        fi
    fi
fi

# -------------------------------------------------------------- VALGRIND
printf 'TIER valgrind: no invalid access across a paint, a mark and a click\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
elif ! build_has qt; then
    printf '  SKIP (no Qt in this build)\n'
else
    # THE PATHS THAT ALLOCATE: a paint per scroll, five mark lists replaced
    # several times, and an event filter living on a child widget that the
    # window's teardown deletes.
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        GBASIC_SYNTAX_PATH=editors/kate \
        vg_run_access_only ./gbasic tests/qt_gutter_test.bas \
        >/dev/null 2>"$work/vg.err" &
    vg_pid=$!
    vg_killed=0
    vg_deadline=$((SECONDS + 420))
    while kill -0 "$vg_pid" 2>/dev/null; do
        if [ "$SECONDS" -ge "$vg_deadline" ]; then
            # THE CHILDREN BY PID FIRST: `$!` is the SUBSHELL bash forks for a
            # background shell function, so killing it alone ORPHANS valgrind
            # -- the leak run_qt_dialog.sh and run_qt_modal.sh each paid for.
            for vg_kid in $(pgrep -P "$vg_pid" 2>/dev/null); do
                kill -9 "$vg_kid" 2>/dev/null
            done
            kill -9 "$vg_pid" 2>/dev/null
            pkill -KILL -f "gbasic tests/qt_gutter_test.bas" 2>/dev/null
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
        pass "no invalid access across a paint, a mark, a click and teardown"
    fi
    # THE ABSENCE IS ASSERTED, because the leak is invisible from inside.
    if pgrep -f "gbasic tests/qt_gutter_test.bas" >/dev/null 2>&1; then
        pkill -KILL -f "gbasic tests/qt_gutter_test.bas" 2>/dev/null
        fail "the valgrind tier left a gbasic process behind"
    else
        pass "and nothing was left running afterwards"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_gutter: all checks passed\n'
else
    printf 'run_qt_gutter: FAILED\n'
fi
exit "$status"
