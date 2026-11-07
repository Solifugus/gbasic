#!/bin/bash
# A WINDOW'S PIXELS (docs/qt_gui_design.md §0.37): `qt.capture(win, file,
# { part, scale })` writes a window, or one named part of it, to a PNG at a
# chosen scale, offscreen. Asked for by the books (volume 3's gap ledger, S1):
# a figure of Studio has to be something a build retakes, not a screenshot.
#
# THREE ORACLES, none of them the record the call answers. The fixture reads
# the PNG header's own bytes and compares them with the part's size as Qt
# reports it; the pixel tier decodes every image with Python's PIL, which
# shares no code with Qt's PNG writer, and checks colours the fixture set with
# a stylesheet; and the steady tier is twelve captures of a focused editor
# while Qt's loop runs, where the caret blinks -- before the fix they came out
# as two different pictures. The editor's focus arrives through that loop, so
# a capture counts only once it has arrived; under valgrind in the parallel
# gate the first one used to come before it, without a caret.
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

if ! build_has qt; then
    printf 'SKIP run_qt_capture semantics (no Qt in this build -- capturing was NOT measured)\n'
    printf 'run_qt_capture: all checks passed\n'
    exit 0
fi

printf 'TIER semantics: sizes from the PNG header, scale, parts, sameness, refusals, the caret\n'
mkdir -p "$work/shots"
OUT="$work/shots" QT_QPA_PLATFORM=offscreen timeout -k 5 60 \
    ./gbasic --line-buffered tests/qt_capture_test.bas \
    > "$work/sem.out" 2> "$work/sem.err"
rc=$?
ran=0
if [ "$rc" = "124" ] || [ "$rc" = "137" ]; then
    fail "the fixture HUNG"
elif grep -q NODISPLAY "$work/sem.out"; then
    printf '  SKIP (no display and no offscreen platform)\n'
elif [ "$rc" != "0" ]; then
    tail -3 "$work/sem.out"; printf '     %s\n' "$(head -2 "$work/sem.err")"
    fail "the fixture exited $rc"
else
    ran=1
    bad=$(grep -c '^MISMATCH' "$work/sem.out" || true)
    checks=$(sed -n 's/^ok  *\([0-9]*\) checks.*/\1/p' "$work/sem.out")
    if [ "${bad:-1}" != "0" ]; then
        grep '^MISMATCH' "$work/sem.out" | head -5
        fail "$bad mismatches"
    elif [ "${checks:-0}" -lt 37 ]; then
        fail "only ${checks:-0} checks ran -- the fixture stopped early"
    else
        pass "$checks checks, 0 mismatches"
    fi
    grep -v 'This plugin does not support' "$work/sem.err" > "$work/sem.err2"
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing on stderr"
    fi
fi

printf 'TIER pixels: what the pictures show, decoded by PIL\n'
if [ "$ran" != "1" ]; then
    printf '  SKIP (the fixture did not run, so there are no pictures)\n'
elif ! python3 -c 'import PIL' 2>/dev/null; then
    printf '  SKIP (python3 PIL unavailable -- the pixels were NOT measured)\n'
else
    python3 -I - "$work/shots" > "$work/pix.out" 2>&1 <<'EOF'
import sys
from PIL import Image
d = sys.argv[1]

def load(name):
    im = Image.open(f"{d}/{name}")
    im.load()                      # decodes every row: a broken IDAT raises
    return im

def frac(im, colour):
    px = im.convert("RGB").tobytes()
    n = len(px) // 3
    c = bytes(colour)
    hit = sum(1 for i in range(n) if px[3 * i:3 * i + 3] == c)
    return hit, hit / n

def say(ok, what):
    print(("ok   " if ok else "FAIL ") + what)

RED, BLUE = (255, 0, 0), (0, 0, 255)
whole = load("whole.png")
say(whole.mode == "RGB", f"the window is RGB with no alpha to show as black ({whole.mode})")
say(len(set(whole.convert("RGB").tobytes()[i:i + 3]
            for i in range(0, whole.width * whole.height * 3, 3))) > 50,
    "the window is not blank: more than 50 colours")
h_hit, h_frac = frac(load("heading.png"), RED)
say(h_frac > 0.9, f"the heading, set red, is red ({h_frac:.3f})")
b_hit, b_frac = frac(load("note.png"), BLUE)
say(b_frac > 0.9, f"the note, set blue, is blue ({b_frac:.3f})")
say(frac(load("note.png"), RED)[1] < 0.01, "and the note is not red (the control)")
w_red, _ = frac(whole, RED)
w_blue, _ = frac(whole, BLUE)
say(abs(w_red - h_hit) <= 0.05 * h_hit,
    f"the window holds the heading's red pixels ({w_red} against {h_hit})")
say(abs(w_blue - b_hit) <= 0.05 * b_hit,
    f"and the note's blue ones ({w_blue} against {b_hit})")
h2 = load("heading2.png")
_, h2_frac = frac(h2, RED)
say(abs(h2_frac - h_frac) < 0.03,
    f"at scale 2 the heading is the same picture, larger ({h2_frac:.3f} red)")
say(h2.size == (2 * load("heading.png").width, 2 * load("heading.png").height),
    f"and PIL reads it as twice the size ({h2.size})")
# THE TEXT'S EXTENT DOUBLES, which is what the scale is for. The red fraction
# cannot see it: drawn at scale 1 into the doubled image, the stylesheet's
# background still fills all of it and the text sits small in one corner --
# measured, every check above passed on exactly that.
def ink(im):
    im = im.convert("RGB")
    xs, ys = [], []
    for y in range(im.height):
        for x in range(im.width):
            r, g, b = im.getpixel((x, y))
            if r < 128:                      # the black text on red
                xs.append(x); ys.append(y)
    return (max(xs) - min(xs) + 1, max(ys) - min(ys) + 1) if xs else (0, 0)
w1, t1 = ink(load("heading.png"))
w2, t2 = ink(h2)
say(w1 > 0 and 1.8 <= w2 / w1 <= 2.2 and 1.6 <= t2 / max(t1, 1) <= 2.4,
    f"and its text is drawn twice as large ({w1}x{t1} at 1, {w2}x{t2} at 2)")
stretched = load("heading.png").convert("RGB").resize(h2.size, Image.NEAREST)
say(stretched.tobytes() != h2.convert("RGB").tobytes(),
    "it was drawn at scale 2, not the scale-1 picture stretched")
EOF
    if grep -q '^FAIL\|Traceback' "$work/pix.out"; then
        grep '^FAIL\|Error' "$work/pix.out" | head -5
        fail "the pictures do not show what was set"
    else
        pass "$(grep -c '^ok' "$work/pix.out") pixel checks agree"
    fi
fi

printf 'TIER valgrind: no invalid access across the captures and refusals\n'
if [ "$ran" != "1" ]; then
    printf '  SKIP (the fixture did not run)\n'
elif ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    mkdir -p "$work/vgshots"
    OUT="$work/vgshots" QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen \
        timeout -k 5 420 valgrind $VG_ACCESS_FLAGS ${VG_EXTRA:-} ./gbasic tests/qt_capture_test.bas \
        >"$work/vg.out" 2>"$work/vg.err"
    vg_rc=$?
    if [ "$vg_rc" = "124" ] || [ "$vg_rc" = "137" ]; then
        fail "the fixture hung under valgrind, so memory access was NOT measured"
    elif [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    elif ! grep -q '^ok [0-9]* checks, 0 mismatches' "$work/vg.out"; then
        tail -3 "$work/vg.out"
        fail "under valgrind the fixture did not finish clean (exit $vg_rc)"
    else
        pass "no invalid access, and the fixture still passes under valgrind"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_capture: all checks passed\n'
else
    printf 'run_qt_capture: FAILED\n'
fi
exit "$status"
