#!/usr/bin/env bash
# THE QT EVENT-LOOP PROBE -- the one thing docs/qt_gui_design.md §5 names as
# unproven before any code is written, kept measurable rather than written down
# once.
#
# WHAT THE QUESTION IS. gBASIC's own event loop is ONE `poll()` over the
# webserver's descriptors, libcurl's descriptors and the root mailbox, with
# timers riding the poll timeout (`webserver_run_event_loop`, src/eval.c). Qt
# has a loop of its own and needs to be the one waiting, because it watches the
# keyboard and the mouse -- and because Qt opens NESTED loops: a modal dialog,
# `QMenu::exec()`, drag-and-drop and `QProgressDialog` each run an event loop
# inside the first. So the question is whose loop waits, and it has exactly two
# answers. BOTH ARE MEASURED HERE, because the one that fails does not fail in
# ordinary operation -- it fails only once something opens a dialog, which is
# precisely when nobody is watching a webserver.
#
#   DIRECTION A  Qt owns the loop; a gBASIC source is a QSocketNotifier.
#   DIRECTION B  gBASIC owns the loop; Qt is pumped with processEvents().
#
# MEASURED 2026-10-17, Qt 6.10.2, offscreen AND a real Wayland display, the same
# answer on both:
#
#   B, ordinary        30 polls, 15 Qt ticks, worst gBASIC gap 52ms   -- fine
#   B, dialog open     18 polls, worst gap 651ms over a 600ms dialog  -- STARVED
#   A, ordinary        4 of 4 source events served, 15 Qt ticks       -- fine
#   A, dialog open     4 of 4 served, three of them DURING the dialog -- fine
#
# SO DIRECTION A IS THE ARCHITECTURE AND B IS RULED OUT BY MEASUREMENT, not by
# taste: under B a gBASIC webserver stops answering and every timer misses for
# as long as a message box is up, and a `form` that validates its input will
# want a message box.
#
# AND A FINDING §5 DID NOT PREDICT, which is the reason the drain is in the
# probe: a QSocketNotifier is LEVEL-TRIGGERED, so the first version -- which
# never read the pipe -- fired 545,066 times for ONE byte. That is the same
# defect class PLAT-HTTP already recorded ("the first delivery loop re-reported
# undrained bytes EVERY iteration ... a loop that never went idle"), so the
# contract for a gBASIC source registered with Qt is that its handler DRAINS,
# and this suite asserts the count stays small rather than merely non-zero.
#
# IT LINKS NO gBASIC CODE. See the header in tests/qt/qt_loop.cpp for why that
# is deliberate rather than lazy.
#
# WHAT IT DOES NOT ANSWER, and says so rather than implying otherwise:
#   * re-entry into the EVALUATOR. The probe proves a Qt signal reaches a C
#     callback through a moc translation unit; calling `invoke_function` from
#     there is inferred from `gi_signal_marshal`, which ships it for GTK.
#   * Windows. `QSocketNotifier` there works on SOCKETS ONLY, not arbitrary
#     handles, so direction A will need `QWinEventNotifier` for any source that
#     is not a socket -- named now rather than discovered during the port.
#   * threads, actors, and more than one window.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

MOC=""
for c in /usr/lib/qt6/libexec/moc /usr/lib/x86_64-linux-gnu/qt6/libexec/moc \
         /usr/lib/qt6/bin/moc moc moc-qt6; do
    if [ -x "$c" ] || command -v "$c" >/dev/null 2>&1; then MOC="$c"; break; fi
done
if ! pkg-config --exists Qt6Widgets 2>/dev/null; then
    printf 'SKIP run_qt_loop (no Qt6Widgets development files -- the loop question was NOT measured)\n'
    exit 0
fi
if [ -z "$MOC" ]; then
    printf 'SKIP run_qt_loop (Qt6 is present but `moc` was not found -- the probe needs it, and the loop question was NOT measured)\n'
    exit 0
fi
if ! command -v g++ >/dev/null 2>&1; then
    printf 'SKIP run_qt_loop (no C++ compiler -- the loop question was NOT measured)\n'
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

printf 'TIER build: a C++/moc translation unit, reachable from C\n'
if "$MOC" tests/qt/qt_loop.cpp -o "$work/qt_loop.moc" 2>"$work/moc.err" \
   && g++ -std=c++17 -I"$work" -Itests/qt -c tests/qt/qt_loop.cpp -o "$work/qt_loop.o" -fPIC \
        $(pkg-config --cflags Qt6Widgets) 2>"$work/cxx.err" \
   && gcc -std=c11 -Itests/qt -c tests/qt/harness.c -o "$work/harness.o" 2>"$work/cc.err" \
   && g++ -o "$work/probe" "$work/harness.o" "$work/qt_loop.o" \
        $(pkg-config --libs Qt6Widgets) 2>"$work/link.err"; then
    pass "moc, g++ and gcc produce one binary ($(pkg-config --modversion Qt6Widgets))"
else
    cat "$work"/*.err 2>/dev/null | head -20
    fail "the probe did not build"
    exit $status
fi

# THE PROBE'S OWN EXIT CODE IS NOT THIS SUITE'S VERDICT and must not be allowed
# to become it. The harness asserts its own expectations and exits nonzero when
# they fail -- which is EXACTLY what the modal run is supposed to do -- and under
# `set -euo pipefail` that status propagated out of the command substitution and
# ended this suite SILENTLY, mid-tier, with no FAIL line. Seventh instance of
# that trap in this tree and I wrote it again; the suite does the asserting, so
# the status is dropped deliberately and by name.
run_probe() {   # run_probe MODE [modal] -> the probe's one line
    set +e
    QT_QPA_PLATFORM=offscreen timeout -k 5 60 "$work/probe" "$@" 2>/dev/null | tail -1
    set -e
}
field() { printf '%s\n' "$1" | tr ' ' '\n' | sed -n "s/^$2=//p"; }

printf 'TIER direction B: gBASIC owns the loop, Qt is pumped inside it\n'
b_out="$(run_probe B)"
b_clicked=$(field "$b_out" clicked)
b_ticks=$(field "$b_out" qt_ticks)
b_polls=$(field "$b_out" gbasic_polls)
b_gap=$(field "$b_out" worst_gbasic_gap_ms)
if [ "${b_clicked:-0}" = "1" ] && [ "${b_ticks:-0}" -ge 10 ] && [ "${b_polls:-0}" -ge 20 ]; then
    pass "ordinary operation is fine: ${b_polls} gBASIC polls, ${b_ticks} Qt ticks, worst gap ${b_gap}ms"
else
    printf '    %s\n' "$b_out"; fail "direction B did not even work in ordinary operation"
fi

printf 'TIER direction B with a dialog open: the case that rules it out\n'
bm_out="$(run_probe B modal)"
bm_gap=$(field "$bm_out" worst_gbasic_gap_ms)
bm_blocked=$(field "$bm_out" modal_blocked_ms)
# THE ASSERTION IS THAT IT STARVES, which is the finding -- so this tier goes red
# if a future Qt makes the nested loop cooperative, and that is worth knowing
# too, because it would reopen a decision taken on this measurement.
if [ "${bm_blocked:-0}" -ge 300 ] && [ "${bm_gap:-0}" -ge 300 ]; then
    pass "gBASIC's loop STARVES for the dialog's life: worst gap ${bm_gap}ms over a ${bm_blocked}ms dialog"
elif [ "${bm_blocked:-0}" -lt 300 ]; then
    printf '    %s\n' "$bm_out"; fail "the dialog did not stay up long enough to measure anything"
else
    printf '    %s\n' "$bm_out"
    fail "gBASIC's loop was NOT starved (gap ${bm_gap}ms) -- the nested loop has become cooperative, so §5's decision rests on a measurement that no longer holds"
fi

printf 'TIER direction A: Qt owns the loop, a gBASIC source is a QSocketNotifier\n'
a_out="$(run_probe A)"
a_served=$(field "$a_out" gbasic_source_served)
a_ticks=$(field "$a_out" qt_ticks)
if [ "$(field "$a_out" clicked)" = "1" ] && [ "${a_served:-0}" -ge 3 ] && [ "${a_ticks:-0}" -ge 5 ]; then
    pass "gBASIC's source is served ${a_served} times and Qt's own clock ticks ${a_ticks} times"
else
    printf '    %s\n' "$a_out"; fail "direction A did not serve the gBASIC source"
fi
# THE DRAIN, asserted as a CEILING rather than a presence. Without it the
# notifier re-fires while the descriptor stays readable: 545,066 activations for
# one byte, measured, which is a loop that never goes idle rather than a wrong
# answer -- so "it was served" alone passes on the broken version.
if [ "${a_served:-0}" -lt 50 ]; then
    pass "and it does not SPIN: ${a_served} activations for 4 writes (unDRAINED it was 545,066 for one)"
else
    printf '    %s\n' "$a_out"
    fail "the notifier fired ${a_served} times for 4 writes -- the handler is not draining"
fi

printf 'TIER direction A with a dialog open: the comparison that decides it\n'
am_out="$(run_probe A modal)"
am_served=$(field "$am_out" gbasic_source_served)
am_blocked=$(field "$am_out" modal_blocked_ms)
if [ "${am_blocked:-0}" -ge 300 ] && [ "${am_served:-0}" -ge 3 ] && [ "${am_served:-0}" -lt 50 ]; then
    pass "served ${am_served} times THROUGH a ${am_blocked}ms dialog -- the same as with no dialog, which is the whole argument for A"
else
    printf '    %s\n' "$am_out"; fail "direction A did not survive a nested loop"
fi
# THE CONTROL that makes the pair a difference rather than two numbers: A must
# serve its source in BOTH runs while B's poll count collapses in one of them.
if [ "${am_served:-0}" = "${a_served:-x}" ]; then
    pass "CONTROL: A serves the same count with and without the dialog; B's polls fell ${b_polls} -> $(field "$bm_out" gbasic_polls)"
else
    fail "A served ${a_served} without a dialog and ${am_served} with one -- the claim is that these are equal"
fi

printf 'TIER display: the answer is not an artefact of the offscreen platform\n'
if [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    set +e
    d_out="$(timeout -k 5 60 "$work/probe" A modal 2>/dev/null | tail -1)"
    set -e
    if [ "$(field "$d_out" gbasic_source_served)" -ge 3 ]; then
        pass "the same on a real display: $(field "$d_out" gbasic_source_served) served through a $(field "$d_out" modal_blocked_ms)ms dialog"
    else
        printf '    %s\n' "$d_out"; fail "direction A behaves differently on a real display"
    fi
else
    pass "SKIP: no display, so only the offscreen platform was measured"
fi

exit $status
