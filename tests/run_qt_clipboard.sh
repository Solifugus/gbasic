#!/bin/bash
# THE CLIPBOARD (§0.33) -- `qt.copy(text)` and `qt.clipboard()`.
#
# REPORTED FROM THE gBASIC STUDIO BENCH as one of two gaps that stopped STU-21
# being ported at all: its tab right-click menu offers `Close` and `Copy file
# path`, and the second is the item the user volunteered an opinion about
# (*"the right-click and copy file path works. And I really like it."*). There
# was no clipboard verb in this module at all, and Studio's GTK route -- a
# `Gtk.TextBuffer` filled, selected and `copy_clipboard`ed into the window's
# clipboard -- has no Qt counterpart, so the feature was left out rather than
# shipped as a worse one.
#
# IT TAKES NO WINDOW, which is measured rather than convenient:
# `QGuiApplication::clipboard()` is a class static and there is ONE clipboard
# per application, so a verb taking a window would be claiming otherwise. That
# claim is only checkable by NOT opening one, which is why every check in the
# fixture before its last tier runs before `qt.open` is ever called.
#
# THE MEASUREMENT STUDIO ASKED FOR IS THE TIER TO READ FIRST, and their warning
# is the reason it exists: under GTK `clipboard.set_text` HANGS, and the route
# that works installs a content provider that reads a buffer AT PASTE TIME --
# so a buffer that goes out of scope produces a successful-looking copy and an
# EMPTY PASTE, with no error at any layer, and Studio holds its buffer on a
# global for that reason alone. Their own words: *"worth one measurement either
# way -- copy, then read it back after the frame that copied has returned."*
# Measured, Qt copies EAGERLY (a QString local to a function that has already
# returned reads back intact, across a turn of the event loop, with no hang),
# so the trap does not exist here -- and the fixture is what keeps that true,
# because the failure it guards against is invisible from inside the program
# that copied.
#
# A NUL IS CONTENT, which is PLAT-NUL's rule applied to a new door by asking
# what the FAR SIDE can hold rather than by preference: measured, `setText`
# takes a counted QString and `a\0b` round-trips as exactly 61 00 62, so the
# answer at this door is CARRY. The naive `QString::fromUtf8(text)` would have
# measured the text with strlen, put one byte on the clipboard and reported
# success. WHAT IS NOT MEASURED IS SAID RATHER THAN IMPLIED: the offscreen
# clipboard is in-process, so whether a real compositor and the application
# pasting into preserve an interior NUL is their business; what this door
# promises is that it does not shorten the text itself.
#
# NOTHING AND EMPTY ARE DIFFERENT ANSWERS and the distinction is Qt's rather
# than a guess -- a fresh clipboard reports no text, one deliberately set to ""
# reports text of length 0 -- so `unknown` and "" are both honest, the §0.16
# rule where a cancelled chooser answers `unknown` and never "".
. "$(dirname "$0")/portable.sh"
# NO `set -e`: the `set +e` lines below are not paired with a restore, because
# this header omits -e and a bare `set -e` would ARM it (run_portable.sh
# tripwires for that).
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

# NO DISPLAY AT ALL, the recipe run_qt_loop.sh established: Qt's detection
# reaches further than DISPLAY, so every door has to be shut.
nodisplay() {
    env -u DISPLAY -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM -u XDG_RUNTIME_DIR \
        -u XDG_SESSION_TYPE "$@"
}

if ! build_has qt; then
    printf 'SKIP run_qt_clipboard (no Qt in this build -- NOTHING about the clipboard was measured)\n'
    exit 0
fi

# -------------------------------------------------------------- SEMANTICS
printf 'TIER semantics: the round trip, the dead frame, the NUL and the refusals\n'
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 60 \
    ./gbasic --line-buffered tests/qt_clipboard_test.bas \
    > "$work/sem.out" 2> "$work/sem.err"
rc=$?
if [ "$rc" = "124" ] || [ "$rc" = "137" ]; then
    # A HANG IS NOT A FAILURE, and it is the specific thing Studio warned
    # about: under GTK `set_text` never returns.
    fail "the fixture HUNG -- which is what a copy that never returns looks like"
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
    elif [ "${checks:-0}" -lt 20 ]; then
        fail "only ${checks:-0} checks ran -- the fixture stopped early"
    else
        pass "$checks checks, 0 mismatches"
    fi
    # THE DEAD-FRAME CHECK IS NAMED SEPARATELY because it is the one Studio
    # asked for, and a count alone would not say whether it ran.
    if grep -q '^ok   a copy survives the frame that made it' "$work/sem.out"; then
        pass "the copy outlived the frame that made it (Qt copies eagerly)"
    else
        fail "the dead-frame check did not run -- the measurement Studio asked for"
    fi
    # AND SO IS THE NUL, for the same reason.
    if grep -q '^ok   a copy carrying a NUL is not shortened' "$work/sem.out"; then
        pass "and a NUL inside the text is carried rather than truncated"
    else
        fail "the NUL check did not run"
    fi
    # `This plugin does not support ...` IS QT'S OWN NOISE under the offscreen
    # platform, not the program's.
    grep -v 'This plugin does not support' "$work/sem.err" > "$work/sem.err2"
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing on stderr"
    fi
fi
set -e 2>/dev/null || true

# ------------------------------------------------------------- NO DISPLAY
# THE CLIPBOARD BELONGS TO A QApplication, so there is none on a text-only
# machine -- and constructing one anyway aborts the process uncatchably, which
# is what this asserts does NOT happen.
printf 'TIER no display: a sentence naming the display, and the program survives\n'
set +e
nodisplay timeout -k 5 60 ./gbasic --line-buffered \
    tests/qt_clipboard_nodisplay_test.bas \
    > "$work/nd.out" 2> "$work/nd.err"
nd_rc=$?
set +e
if [ "$nd_rc" != "0" ]; then
    printf '    %s\n' "$(tail -3 "$work/nd.err")"
    fail "the program exited $nd_rc (134 = the abort this exists to prevent)"
else
    pass "a caught refusal leaves the program running and exiting 0"
fi
if grep -q '^COPY REFUSED: no display is available, so there is no clipboard to copy to' "$work/nd.out"; then
    pass "the copy names the DISPLAY, not a window it never took"
else
    grep '^COPY' "$work/nd.out" | head -2
    fail "the copy refusal does not name the display"
fi
if grep -q '^READ REFUSED: no display is available, so there is no clipboard to read' "$work/nd.out"; then
    pass "and so does the read"
else
    grep '^READ' "$work/nd.out" | head -2
    fail "the read refusal does not name the display"
fi
# THE HALF A CRASH TAKES AWAY: block-buffered stdout goes with an abort, so a
# program that printed its progress would print nothing at all.
if grep -q '^progress line one' "$work/nd.out" \
   && grep -q '^still here, and can say so' "$work/nd.out"; then
    pass "and the lines printed either side of it both arrived"
else
    fail "stdout was lost -- the signature of an abort rather than a refusal"
fi
if grep -q MISMATCH "$work/nd.out"; then
    grep MISMATCH "$work/nd.out" | head -2
    fail "something succeeded that should not have"
fi

# ------------------------------------------------------------------- DOORS
# WHICH DOOR SHORTENED THE TEXT -- the one thing the fixture cannot say.
#
# MEASURED: the perturbation that measures the WRITE with strlen and the one
# that hands the READ back as a C string produce IDENTICAL fixture output (`got
# 1, want 3`), because a round trip fails if either door loses the bytes and
# the two halves agree with each other perfectly. That is the shape that let
# `webclient` hide the same defect, and PLAT-NUL's standing lesson is that a
# truncation at one door is evidence about every door -- so each is exercised
# ALONE here, with Qt's own clipboard as the other side.
#
# IT LINKS THE REAL BACKEND OBJECT (tests/qt/gutter_probe.cpp's arrangement),
# so what is measured is the code the product runs rather than a second copy
# that could be right while the first is wrong.
#
# THERE ARE THREE PLACES A BYTE CAN BE LOST AND EACH IS NAMED BY A DIFFERENT
# CHECK, which is what the separation bought: `gb_qt_copy` measuring the text
# with strlen is caught by the WRITE check here, `gb_qt_clipboard` measuring
# its own copy is caught by the READ check, and the verb's own
# `value_string_n` becoming `value_string` is caught by the fixture's round
# trip and by nothing here, since the probe does not go through the verb. All
# three were perturbed and each reddened exactly its own.
printf 'TIER doors: which half carries the bytes, measured one at a time (C++)\n'
if ! command -v g++ >/dev/null 2>&1; then
    printf '  SKIP (no C++ compiler -- the two doors were NOT measured separately)\n'
elif ! g++ -std=c++17 -fPIC -Iinclude -Isrc/modules -o "$work/cprobe" \
        tests/qt/clipboard_probe.cpp src/modules/qt_backend.o \
        $(pkg-config --cflags --libs Qt6Widgets) \
        $(pkg-config --libs KF6SyntaxHighlighting 2>/dev/null \
          || echo -lKF6SyntaxHighlighting) 2>"$work/cxx.err"; then
    head -8 "$work/cxx.err"
    fail "the clipboard probe did not build"
else
    set +e
    QT_QPA_PLATFORM=offscreen timeout -k 5 60 "$work/cprobe" \
        > "$work/doors.out" 2>/dev/null
    drc=$?
    dfield() { sed -n "s/^$1=\(.*\)$/\1/p" "$work/doors.out" | head -1; }
    if [ "$drc" != "0" ]; then
        cat "$work/doors.out"; fail "the probe did not run (exit $drc)"
    elif [ "$(dfield display)" = "no" ]; then
        printf '  SKIP (the probe found no display)\n'
    elif ! grep -q '^done$' "$work/doors.out"; then
        cat "$work/doors.out"; fail "the probe stopped early"
    else
        # THE WRITE, JUDGED BY QT: three bytes on the clipboard, the middle one
        # a NUL. A strlen-measured copy puts one there and reports success.
        if [ "$(dfield write_len)" = "3" ] \
           && [ "$(dfield write_bytes)" = "610062" ]; then
            pass "the WRITE puts all three bytes on the clipboard (61 00 62)"
        else
            printf '    write_len=%s write_bytes=%s\n' \
                "$(dfield write_len)" "$(dfield write_bytes)"
            fail "the write door shortened the text"
        fi
        # THE READ, FROM A CLIPBOARD QT FILLED, so nothing of ours is on the
        # writing side and this cannot pass by agreeing with our own writer.
        if [ "$(dfield read_len)" = "3" ] \
           && [ "$(dfield read_bytes)" = "780079" ]; then
            pass "and the READ hands back all three (78 00 79)"
        else
            printf '    read_rc=%s read_len=%s read_bytes=%s\n' \
                "$(dfield read_rc)" "$(dfield read_len)" "$(dfield read_bytes)"
            fail "the read door shortened the text"
        fi
        # AND THE ABSENT/EMPTY DISTINCTION AT THE SEAM, both halves: either
        # alone is satisfied by a reader that always says one thing.
        if [ "$(dfield cleared_rc)" = "0" ] \
           && [ "$(dfield empty_rc)" = "1" ] \
           && [ "$(dfield empty_len)" = "0" ]; then
            pass "a cleared clipboard answers nothing, an empty string answers 0 bytes"
        else
            printf '    cleared_rc=%s empty_rc=%s empty_len=%s\n' \
                "$(dfield cleared_rc)" "$(dfield empty_rc)" "$(dfield empty_len)"
            fail "nothing and empty are not told apart at the seam"
        fi
    fi
fi

# --------------------------------------------------------------- VALGRIND
printf 'TIER valgrind: no invalid access across a copy and a read\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    # EVERY READ ALLOCATES: the clipboard's text is copied out of a QByteArray
    # into a malloc'd buffer the C side owns and the gBASIC side frees, which
    # is a new allocation path per call and the one place a count and a buffer
    # could disagree.
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen \
        vg_run_access_only ./gbasic tests/qt_clipboard_test.bas \
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
            pkill -KILL -f "gbasic tests/qt_clipboard_test.bas" 2>/dev/null
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
        pass "no invalid access across a copy, a read and teardown"
    fi
    # THE ABSENCE IS ASSERTED, the lesson run_qt_dialog.sh and run_qt_modal.sh
    # both paid for: a bound that kills the waiter orphans the child.
    if pgrep -f "gbasic tests/qt_clipboard_test.bas" >/dev/null 2>&1; then
        pkill -KILL -f "gbasic tests/qt_clipboard_test.bas" 2>/dev/null
        fail "the valgrind tier left a gbasic process behind"
    else
        pass "and nothing was left running afterwards"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_clipboard: all checks passed\n'
else
    printf 'run_qt_clipboard: FAILED\n'
fi
exit "$status"
