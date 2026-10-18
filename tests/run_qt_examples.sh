#!/usr/bin/env bash
# examples/qt -- RUN, not merely parsed.
#
# WHY THIS SUITE EXISTS. `run_gui_parse.sh` parses every program in that
# directory on every gate run, which catches a syntax error and nothing else;
# behaviour was gated only by the FIXTURES in tests/, written by the hands that
# wrote the backend. An example can therefore rot while every fixture passes --
# and one had: `examples/qt/README.md` listed one of four examples for a day,
# because the parse walk globs the directory and the page describing it was
# checked by nothing.
#
# AND THE EXAMPLES ARE WHERE THE GAPS CAME FROM, which is the stronger
# argument. Setting out to write one per aspect of this backend is what found
# that the leaf vocabulary constructed QLineEdit, QPushButton and QCheckBox AND
# NO MORE -- so a heading, an instruction or a description had nowhere to go,
# and a GUI toolkit that cannot display a sentence is not fully functional.
# Nothing in the tree said so, because every fixture happened to need only an
# entry, a box and a button. §0.10 of docs/qt_gui_design.md records it.
#
# EVERY EXAMPLE TAKES `check`, which is not a test seam bolted on: it is how a
# person verifies a Qt install without looking at a screen, and it is the only
# way a program that hands the thread to `qt.run()` can be gated at all.
#
# Tiers:
#   ARGS      every example accepts `check`, asserted STRUCTURALLY -- because
#             one that does not would HANG rather than fail, and a hang is not
#             a failure (run_string_nul.sh's recorded trap).
#   RUN       each one offscreen in check mode: exit 0, output, and no
#             diagnostic on stderr.
#   DEGRADE   each one with NO DISPLAY AT ALL must say something useful and
#             exit 0 -- the §0.3 claim made by every example rather than by one
#             fixture, since with no display Qt aborts uncatchably and a crash
#             takes block-buffered stdout with it.
#   COVER     the README names every example and no example that is gone. Moved
#             here from run_qt_builder.sh, which is about the builder; this is
#             the suite that owns the directory.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."
make >/dev/null

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

examples=$(ls examples/qt/*.bas | sort)
count=$(printf '%s\n' $examples | grep -c . || true)
# A FLOOR, because every tier below loops over this list and a glob that
# matched nothing would report a clean sweep of nothing -- the vacuous-pass
# shape this tree defends against everywhere else.
if [ "${count:-0}" -ge 5 ]; then
    pass "found $count Qt examples"
else
    fail "found only ${count:-0} Qt examples -- the glob is broken, not the tree"
fi

printf 'TIER args: every example accepts `check`\n'
# STRUCTURAL, because the consequence of getting this wrong is a HANG: an
# example with no check mode reaches `qt.run()` and waits for a person who is
# not there, and the RUN tier below would sit on its timeout rather than
# reporting anything useful.
missing=""
for f in $examples; do
    hits=$(grep -c 'contains(args, "check")' "$f" || true)
    if [ "$hits" -eq 0 ]; then
        missing="$missing$(basename "$f") "
    fi
done
if [ -z "$missing" ]; then
    pass "all $count take `printf '%s' check` and end themselves"
else
    fail "these would HANG under the RUN tier: $missing"
fi

printf 'TIER run: each one offscreen, in check mode\n'
for f in $examples; do
    b=$(basename "$f")
    set +e
    QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 60 \
        ./gbasic --line-buffered "$f" check > "$work/$b.out" 2> "$work/$b.err"
    rc=$?
    set -e
    # Qt's offscreen plugin says this on every run and it is not ours.
    grep -v 'propagateSizeHints' "$work/$b.err" > "$work/$b.err2" || true
    lines=$(grep -c . "$work/$b.out" || true)
    if [ "$rc" = "124" ] || [ "$rc" = "137" ]; then
        fail "$b HUNG -- it reached qt.run() with nobody to close it"
    elif [ "$rc" != "0" ]; then
        printf '    %s\n' "$(tail -3 "$work/$b.err2")"
        fail "$b exited $rc"
    elif [ "${lines:-0}" -eq 0 ]; then
        # A CHECK RUN THAT PRINTS NOTHING is indistinguishable from one that
        # did nothing, which is the whole reason the mode exists.
        fail "$b printed nothing in check mode"
    elif [ -s "$work/$b.err2" ]; then
        printf '    %s\n' "$(head -2 "$work/$b.err2")"
        fail "$b wrote to stderr"
    else
        pass "$b: $lines lines, clean"
    fi
done

printf 'TIER degrade: each one with no display at all\n'
# §0.3 MADE BY EVERY EXAMPLE. With no display Qt calls qFatal and ABORTS --
# SIGABRT, exit 134, core dumped, uncatchable -- and a crash takes
# block-buffered stdout with it, so a program printing its progress prints
# NOTHING and the failure reads as line 1. An example that asks
# `qt.can_display()` first says a sentence instead.
for f in $examples; do
    b=$(basename "$f")
    set +e
    env -u DISPLAY -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR -u QT_QPA_PLATFORM \
        GBASIC_PATH=stdlib timeout -k 5 60 \
        ./gbasic --line-buffered "$f" check > "$work/d_$b.out" 2>&1
    rc=$?
    set -e
    dlines=$(grep -vc 'propagateSizeHints' "$work/d_$b.out" || true)
    if [ "$rc" -ge 128 ]; then
        fail "$b died by signal $((rc - 128)) with no display -- the §0.3 abort"
    elif [ "$rc" != "0" ]; then
        printf '    %s\n' "$(tail -2 "$work/d_$b.out")"
        fail "$b exited $rc with no display"
    elif [ "${dlines:-0}" -eq 0 ]; then
        fail "$b said NOTHING with no display, which is what a crash looks like"
    else
        pass "$b degrades, saying $dlines line(s)"
    fi
done

printf 'TIER cover: the README names every example, and only real ones\n'
# MOVED HERE FROM run_qt_builder.sh, which is about the builder. It caught two
# on the run that added it: `orders.bas` and `notes.bas` had been in the
# directory for a day with nothing naming them, because the parse walk globs
# and the page was checked by nothing.
undoc=""
for f in $examples; do
    b=$(basename "$f")
    hits=$(grep -c "\`$b\`" examples/qt/README.md || true)
    if [ "$hits" -eq 0 ]; then
        undoc="$undoc$b "
    fi
done
if [ -z "$undoc" ]; then
    pass "every examples/qt/*.bas is named in the README"
else
    fail "undocumented Qt examples: $undoc"
fi
# AND THE OTHER DIRECTION, or "everything is documented" is satisfied by a
# README naming files that are gone.
ghosts=""
for b in $(grep -oE '^\| `[a-z_]+\.bas`' examples/qt/README.md | tr -d '|` '); do
    [ -f "examples/qt/$b" ] || ghosts="$ghosts$b "
done
if [ -z "$ghosts" ]; then
    pass "and the README names no example that is gone"
else
    fail "the README names missing examples: $ghosts"
fi

if [ "$status" -eq 0 ]; then
    printf 'run_qt_examples: all checks passed\n'
else
    printf 'run_qt_examples: FAILURES\n'
fi
exit $status
