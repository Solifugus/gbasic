#!/usr/bin/env bash
# THE `source` WIDGET -- a code editor in the `window` vocabulary
# (docs/qt_gui_design.md §0.14), and the single biggest piece gBASIC Studio
# needs from this backend.
#
# WHY IT IS A WORD RATHER THAN A PROPERTY, which is what made it an increment
# at all: everything else a widget carries goes through `QMetaObject` by name,
# and that is what keeps this backend small -- eight construction words against
# the 75 properties QPushButton alone exposes. A HIGHLIGHTER IS NOT A PROPERTY.
# It is an object attached to a document, so `indexOfProperty` has nothing to
# find and the by-name route cannot reach it however it is spelled. The same is
# true of the completer Studio will want next, which is why the shape here is
# "attach a thing, then ask what was attached".
#
# THE DEFECT THIS SUITE EXISTS FOR IS INVISIBLE, NOT PLAUSIBLE, and that is
# unusual even for this backend. MEASURED 2026-10-18: an INVALID syntax
# definition still applies the theme's normal-text format to every line, so an
# editor highlighting NOTHING is not blank, not plain and not obviously wrong
# -- it is an ordinary editor with ordinary text in it. Every assertion of the
# shape "formats were applied" passes on it (5 formatted lines either way); the
# number of RANGES is what separates them, 15 against 5 on five lines of
# gBASIC. So "it highlights" had to become two different claims, measured in
# two different places, and neither is sufficient alone:
#
#   * WHICH DEFINITION WAS APPLIED -- gBASIC can ask, through
#     `qt.highlighting`, and tests/qt_source_test.bas does.
#   * WHETHER THE TEXT IS REALLY COLOURED DIFFERENTLY -- gBASIC cannot ask,
#     having no way to read a QTextLayout's format ranges, so
#     tests/qt/source_probe.cpp does, in C++, with the control built in.
#
# AND THE TIER TO READ FIRST IS `definition source`, because it is the one that
# would have caught the trap this increment nearly shipped with. The probe that
# designed the widget reported `gBASIC` as FOUND with no search path set at
# all, which looked like the path plumbing being unnecessary. IT WAS A COPY
# HAND-INSTALLED INTO ~/.local/share/org.kde.syntax-highlighting IN JUNE, for
# Kate. A fresh machine answers nothing, highlights nothing, and looks fine.
# That is the class of defect this tree has recorded three times from the other
# direction -- the libcurl floor, mawk's missing interval expressions, LDAP's
# GnuTLS diagnostic -- each invisible precisely where it was written. So the
# tier runs with XDG_DATA_HOME pointed somewhere empty and requires the
# definition to come from THE FILE THIS REPO SHIPS.
#
# A `source` IS A `memo` WITH FOUR DIFFERENCES and the fixture asserts each
# against a `memo` in the same window rather than as a value, since every
# value check also passes on a backend that built a plain memo and called it a
# source: it does not wrap (a wrapped line of code makes a line number stop
# matching the file -- the opposite default from `text`, where wrapping is what
# stops a paragraph truncating), its tab is four columns rather than eight, its
# font is the system's monospaced one (asked for rather than named, a hardcoded
# family being how a port acquires a fallback nobody chose), and it carries a
# highlighter.
#
# WHAT IS DELIBERATELY NOT HERE, so nobody reads the absence as an oversight:
# a LINE-NUMBER GUTTER, which Qt has no widget for -- it is a painted side
# area, real work, and a separate increment; and COMPLETION, whose data already
# exists (`src/lsp/`, the `source_outline` builtin) while the popup is a
# QCompleter, an object attached to a widget, which lands in exactly the same
# place as the highlighter and for the same reason.
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

nodisplay() {
    env -u DISPLAY -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM -u XDG_RUNTIME_DIR \
        -u XDG_SESSION_TYPE "$@"
}

if ! build_has qt; then
    printf 'SKIP run_qt_source (no Qt in this build -- NOTHING about the editor was measured)\n'
    exit 0
fi

# THE SEARCH PATH THE SOURCE TREE NEEDS, which `make` builds: the engine wants
# `<dir>/syntax/*.xml` and `editors/kate/gbasic.xml` is not in that shape, so
# the one definition this repo ships is copied into the layout by a make rule.
# Asserted rather than assumed, because every tier below sets this variable and
# a missing directory would make them all report "no definition installed" --
# a suite agreeing with itself about nothing.
printf 'TIER layout: the definition this repo ships, in the shape the engine wants\n'
if [ -f editors/kate/syntax/gbasic.xml ]; then
    if cmp -s editors/kate/gbasic.xml editors/kate/syntax/gbasic.xml; then
        pass "editors/kate/syntax/gbasic.xml is make's copy of the one definition"
    else
        fail "the generated copy has drifted from editors/kate/gbasic.xml"
    fi
else
    fail "make did not build editors/kate/syntax/gbasic.xml (the engine's layout)"
fi
export GBASIC_SYNTAX_PATH=editors/kate

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: the vocabulary, the oracle, binding, the builder\n'
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 120 ./gbasic --line-buffered \
    tests/qt_source_test.bas > "$work/sem.out" 2> "$work/sem.err"
sem_rc=$?
set -e
if grep -q "No display is available\|SKIP no display" "$work/sem.out"; then
    printf '  SKIP (no display -- the editor was NOT rendered)\n'
elif [ "$sem_rc" != "0" ]; then
    tail -5 "$work/sem.out"; tail -3 "$work/sem.err"
    fail "the fixture exited $sem_rc"
else
    checks=$(grep -c '^ok' "$work/sem.out" || true)
    bad=$(grep -c 'MISMATCH' "$work/sem.out" || true)
    # A FLOOR, because a fixture that DIES early leaves no MISMATCH lines and
    # would otherwise report `ok 0 checks, 0 mismatches` -- a green line above
    # the failure that named the problem. All three mismatch reports in this
    # backend's suites needed this; it is not hypothetical.
    if [ "$bad" = "0" ] && [ "${checks:-0}" -ge 25 ]; then
        pass "$checks checks, 0 mismatches"
    else
        grep 'MISMATCH' "$work/sem.out" | head -8
        fail "$checks checks, $bad mismatches (want >= 25 and none)"
    fi
    if grep -q "warning:" "$work/sem.err"; then
        grep "warning:" "$work/sem.err" | head -3
        fail "the fixture warned, and a correct run has nothing to warn about"
    else
        pass "and clean stderr"
    fi
fi

# ------------------------------------------------------------- COLOUR
# THE CLAIM gBASIC CANNOT MAKE. Both halves in one process, so the comparison
# does not rest on two environments being otherwise identical.
printf 'TIER colour: the text really is formatted differently (C++, not gBASIC)\n'
if ! command -v g++ >/dev/null 2>&1; then
    printf '  SKIP (no C++ compiler -- whether the text is COLOURED was NOT measured)\n'
elif [ ! -d /usr/include/KF6/KSyntaxHighlighting ]; then
    printf '  SKIP (KSyntaxHighlighting headers absent -- colouring was NOT measured)\n'
elif ! g++ -std=c++17 -fPIC -o "$work/sprobe" tests/qt/source_probe.cpp \
        -I/usr/include/KF6/KSyntaxHighlighting \
        $(pkg-config --cflags Qt6Widgets) -lKF6SyntaxHighlighting \
        $(pkg-config --libs Qt6Widgets) 2>"$work/cxx.err"; then
    head -10 "$work/cxx.err"
    fail "the colour probe did not build"
else
    set +e
    QT_QPA_PLATFORM=offscreen timeout -k 5 60 "$work/sprobe" editors/kate \
        > "$work/col.out" 2>/dev/null
    col_rc=$?
    set -e
    field() { sed -n "s/.*\b$1=\([^ ]*\).*/\1/p" "$work/col.out" | head -1; }
    if [ "$col_rc" != "0" ]; then
        cat "$work/col.out"; fail "the colour probe did not run (exit $col_rc)"
    elif [ "$(field definition)" != "found" ]; then
        cat "$work/col.out"; fail "the probe could not load the definition"
    else
        with=$(field with_ranges)
        without=$(field without_ranges)
        wl=$(field with_lines)
        wol=$(field without_lines)
        # THE DISCRIMINATOR IS THE COUNT, NOT THE PRESENCE, and the second
        # check is what says so: both runs format every line, so "formats were
        # applied" cannot tell them apart.
        if [ "${with:-0}" -gt "${without:-0}" ]; then
            pass "a classified document carries more ranges: $with against $without"
        else
            cat "$work/col.out"
            fail "the definition changed nothing: $with ranges against $without"
        fi
        if [ "${wl:-0}" = "${wol:-0}" ] && [ "${wl:-0}" -gt 0 ]; then
            pass "while BOTH format every line ($wl) -- which is why a presence check is vacuous"
        else
            cat "$work/col.out"
            fail "the control did not hold: $wl formatted lines against $wol"
        fi
        if [ "$(field without_valid)" = "0" ]; then
            pass "and the control really is an absent definition"
        else
            fail "the control's definition was VALID, so it measured nothing"
        fi
    fi
fi

# ------------------------------------------------- WHERE THE DEFINITION CAME FROM
# THE TIER THAT WOULD HAVE CAUGHT THE TRAP. With the user's own data directory
# moved aside, the definition must come from the file THIS REPO ships -- which
# is the only thing a machine other than this one has.
printf 'TIER definition source: it comes from the repo, not from this developer\n'
cat > "$work/whence.bas" <<'BAS'
window w( title: "x", width: 400, height: 300 )
    column
        source code( language: "gBASIC" )
    end column
end window
program main()
    if not qt.can_display() then
        print "nodisplay"
        return
    end if
    qt.open(w)
    h = qt.highlighting(w, "code")
    if is_unknown(h) then
        print "applied=none"
    else
        print "applied=" + h.language
        print "from=" + h.file
    end if
    qt.stop()
end program
BAS
set +e
XDG_DATA_HOME="$work/empty-home" QT_QPA_PLATFORM=offscreen \
    timeout -k 5 60 ./gbasic --line-buffered "$work/whence.bas" \
    > "$work/whence.out" 2> "$work/whence.err"
wh_rc=$?
set -e
if grep -q nodisplay "$work/whence.out"; then
    printf '  SKIP (no display -- where the definition comes from was NOT measured)\n'
elif [ "$wh_rc" != "0" ]; then
    cat "$work/whence.out"; fail "the fixture exited $wh_rc"
else
    from=$(sed -n 's/^from=//p' "$work/whence.out" | head -1)
    if [ "$(sed -n 's/^applied=//p' "$work/whence.out" | head -1)" != "gBASIC" ]; then
        cat "$work/whence.out"
        fail "with no copy in the user's home, nothing was applied -- which is what every machine but this one sees"
    elif [ "$from" = "editors/kate/syntax/gbasic.xml" ]; then
        pass "the repo's own definition was used: $from"
    else
        fail "the definition came from $from, which is not the one this repo ships"
    fi
fi

# --------------------------------------------------------------- RELOCATED
# A DOWNLOAD IS NOT AN INSTALL, and the obvious implementation gets that wrong
# in a way nothing else here could see. tools/build-release-tarball.sh says in
# as many words that the tree is "relocatable by construction" and runs where
# it is unpacked -- which works because the stdlib is found RELATIVE TO THE
# BINARY rather than at the absolute path baked in at build time. A baked-in
# path alone pointed a download at /usr/local/share/gbasic/syntax-highlighting,
# a directory that does not exist where a tarball is extracted, so the
# definition was IN THE ARCHIVE and unreachable by the binary beside it. That
# exact shape has been reported from OUTSIDE this project three times, which
# run_library_tiers.sh records; it was looked for here rather than discovered,
# and it was real -- measured both ways before this tier was written.
#
# THE ENVIRONMENT IS STRIPPED, which is the whole point: no
# GBASIC_SYNTAX_PATH, since this suite exports one and a download has none; and
# an empty XDG_DATA_HOME, since a developer's own hand-installed copy is
# exactly what made the original trap invisible.
printf 'TIER relocated: a moved tree finds its own definition, with no environment\n'
relo="$work/relo"
if ! make install PREFIX=/usr/local DESTDIR="$relo" >/dev/null 2>&1; then
    fail "make install into a scratch DESTDIR did not work"
else
    moved="$work/moved"
    mv "$relo/usr/local" "$moved"
    if [ ! -f "$moved/share/gbasic/syntax-highlighting/syntax/gbasic.xml" ]; then
        fail "install did not carry the syntax definition at all"
    else
        pass "install carries the definition into share/gbasic/syntax-highlighting"
        set +e
        ( cd "$work" && env -u GBASIC_SYNTAX_PATH \
            XDG_DATA_HOME="$work/empty-home" QT_QPA_PLATFORM=offscreen \
            timeout -k 5 60 "$moved/bin/gbasic" --line-buffered \
            "$work/whence.bas" > "$work/relo.out" 2>"$work/relo.err" )
        relo_rc=$?
        set -e
        got=$(sed -n 's/^from=//p' "$work/relo.out" | head -1)
        want="$moved/share/gbasic/syntax-highlighting/syntax/gbasic.xml"
        if grep -q nodisplay "$work/relo.out"; then
            printf '  SKIP (no display -- relocation was NOT measured)\n'
        elif [ "$relo_rc" != "0" ]; then
            cat "$work/relo.err"; fail "the moved binary exited $relo_rc"
        elif [ "$got" = "$want" ]; then
            pass "and the moved binary found it, relative to itself"
        else
            cat "$work/relo.out"
            fail "a moved tree reported '$got' -- a download would highlight nothing"
        fi
    fi
fi

# ------------------------------------------------------------------ WARNING
# A MISSING DEFINITION IS A WARNING AND NOT A RAISE, which is the decision
# that makes the whole feature affordable: the editor is built, editable and
# perfectly usable without highlighting, so stopping the program would be a
# worse answer than saying so -- the position `odbc.connect` takes on a driver
# that needs ClientCharset, and for the identical reason, that the failure is
# SILENT and so there is nothing on screen to attach a hint to.
printf 'TIER warning: an absent definition is said out loud, and the editor still works\n'
set +e
XDG_DATA_HOME="$work/empty-home" GBASIC_SYNTAX_PATH="$work/no-such-dir" \
    QT_QPA_PLATFORM=offscreen timeout -k 5 60 ./gbasic --line-buffered \
    "$work/whence.bas" > "$work/warn.out" 2> "$work/warn.err"
wr_rc=$?
set -e
if grep -q nodisplay "$work/warn.out"; then
    printf '  SKIP (no display -- the warning was NOT measured)\n'
else
    if [ "$wr_rc" = "0" ]; then
        pass "the program still exits 0 -- an editor without highlighting is an editor"
    else
        cat "$work/warn.err"; fail "the program exited $wr_rc instead of warning"
    fi
    if grep -q '\[2115\]' "$work/warn.err"; then
        pass "warning 2115 fired"
    else
        cat "$work/warn.err"; fail "nothing warned, and there is nothing on screen to notice"
    fi
    # IT MUST NAME THE PART, because it cannot name a POSITION: a window record
    # holds no line, so the warning is reported at `qt.open` rather than at the
    # declaration that asked for the language. Naming `source code` is what
    # makes the site not matter.
    if grep -q 'source code' "$work/warn.err"; then
        pass "and names the part, which is what it has instead of a position"
    else
        cat "$work/warn.err"; fail "the warning did not name the part"
    fi
    if grep -q '^applied=none' "$work/warn.out"; then
        pass "and qt.highlighting agrees that nothing was applied"
    else
        fail "the warning and qt.highlighting disagree about what happened"
    fi
    # THE CONTROL, without which "it warns" is satisfied by a build that warns
    # about every source widget there is.
    if grep -q '\[2115\]' "$work/sem.err" 2>/dev/null; then
        fail "it also warns when the definition IS there, so the warning says nothing"
    else
        pass "while a run that found its definition is silent"
    fi
fi

# ------------------------------------------------------------------- DEGRADE
printf 'TIER degrade: no display at all\n'
set +e
nodisplay timeout -k 5 60 ./gbasic --line-buffered "$work/whence.bas" \
    > "$work/deg.out" 2> "$work/deg.err"
deg_rc=$?
set -e
if [ "$deg_rc" = "0" ] && grep -q nodisplay "$work/deg.out"; then
    pass "a source window degrades to a sentence and exit 0"
else
    tail -3 "$work/deg.err"
    fail "no display gave exit $deg_rc (§0.3: it must be a sentence, never an abort)"
fi

# ------------------------------------------------------------------ VALGRIND
# ACCESS-ONLY, the policy run_qt_backend established and for the reason it
# gives: Qt does not free its platform plugin, font configuration or
# QApplication statics, so a LEAK claim would be a suppression file describing
# Qt. What is new here is a QSyntaxHighlighter parented to a widget and a
# Repository that lives for the process, so the ACCESS claim is worth making.
#
# AND IT RUNS WITH QT'S REGEX JIT TURNED OFF, which is a measurement rather
# than a convenience. A highlighted document reports three objections --
# `Invalid read of size 16` on a QString from `QTextBlock::text()`, and two
# uninitialised conditionals -- in frames valgrind cannot attribute to any
# object at all (`obj:*`, `???`). ISOLATED THE WAY run_odbc.sh isolates a
# driver defect, by reproducing it with NO gBASIC IN THE STACK: the pure C++
# tests/qt/source_probe.cpp reports the identical three from its own
# `ranges_with`. Narrowed from there by measurement, not by reading:
#
#   a QPlainTextEdit with NO highlighter               0 objections
#   gBASIC's pre-existing `memo` fixture               0 objections
#   a highlighted document                             3 objections
#   the same, QT_ENABLE_REGEXP_JIT=0                   0 objections
#
# So it is PCRE2's JIT, reached through the definition's rules by
# QRegularExpression: the JIT compiles to ANONYMOUS executable memory, which
# is why no object can be named and why valgrind's own glibc suppressions do
# not match, and its matcher over-reads the subject buffer in 16-byte steps by
# design -- which is why PCRE2 ships a --enable-valgrind build of its own.
#
# TURNING THE JIT OFF RATHER THAN SUPPRESSING IT is the choice, and the
# argument is tests/odbc.supp's own: a blanket entry "anything in an object we
# cannot name" would also hide uninitialised data WE passed in, which is
# exactly what that file refuses to do with narrow per-driver entries. There is
# nothing narrow available here, since there is no name to anchor to. The
# variable is Qt's own and changes how a regex is EXECUTED, never what it
# matches -- and the tier asserts that, by requiring the fixture to produce the
# same checks either way, so a build where the setting changed the program
# would fail rather than pass quietly.
printf 'TIER valgrind: no invalid access while an editor is highlighted\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access in the editor was NOT measured)\n'
else
    # THE PREMISE FIRST: disabling the JIT must not change what the program
    # does, or the tier below measures a different program from the one that
    # ships. Asserted as an EQUALITY of the check counts.
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen timeout -k 5 120 ./gbasic \
        --line-buffered tests/qt_source_test.bas > "$work/nojit.out" 2>/dev/null
    set -e
    nojit_ok=$(grep -c '^ok' "$work/nojit.out" || true)
    nojit_bad=$(grep -c 'MISMATCH' "$work/nojit.out" || true)
    jit_ok=$(grep -c '^ok' "$work/sem.out" 2>/dev/null || true)
    jit_bad=$(grep -c 'MISMATCH' "$work/sem.out" 2>/dev/null || true)
    # AN EQUIVALENCE BETWEEN THE TWO RUNS, not a claim that either is clean --
    # a correction found by perturbing. Written as "the counts agree AND the
    # no-JIT run has no mismatches", a BROKEN FIXTURE failed here too, saying
    # "with the regex JIT off the fixture behaves differently" when both runs
    # behaved identically badly: the semantics tier had already named the real
    # defect and this one blamed a setting that had nothing to do with it,
    # which is the reports-the-wrong-cause class this tree keeps recording.
    # Comparing BOTH counts leaves this tier failing only for its own reason.
    if [ "${nojit_ok:-0}" = "${jit_ok:-0}" ] && [ "${nojit_bad:-0}" = "${jit_bad:-0}" ]; then
        pass "the JIT setting changes nothing the program does ($nojit_ok checks either way)"
    else
        fail "with the regex JIT off the fixture behaves differently ($nojit_ok ok/$nojit_bad bad against $jit_ok ok/$jit_bad bad) -- the tier below would measure another program"
    fi
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen vg_run_access_only ./gbasic \
        tests/qt_source_test.bas >/dev/null 2>"$work/vg.err"
    vg_rc=$?
    set -e
    # COMPARED AGAINST $VG_EXIT, never zero: the fixture can exit nonzero for
    # reasons that are not valgrind's, and a bare `if vg_run ...` reads that as
    # an objection -- printing FAIL over a report saying 0 errors, the trap
    # run_repl.sh records and which this backend's own suite hit.
    if [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -5
        fail "valgrind objected"
    else
        pass "no invalid access across construction, highlighting and teardown"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_source: all checks passed\n'
else
    printf 'run_qt_source: FAILED\n'
fi
exit $status
