#!/usr/bin/env bash
# Studio's notebook asks (docs/qt_gui_design.md §0.38): a .bas file that reads
# like a notebook, with run results under each section and marked comments
# shown as prose. This suite holds the Qt half; source_comments (B1) has
# tests/run_source_comments.sh.
#
# Tiers:
#   FIGURES (C, D1) -- tests/qt_notebook/figures_test.bas: qt.render_svg's
#     sizes read from each PNG's own header, scale, aspect, the same bytes
#     twice, every refusal and that none wrote a file; and `theme:` naming a
#     KSyntaxHighlighting theme, one shipped (Printing) and one the runner puts
#     on the syntax path (GBASIC_SYNTAX_PATH/themes/gray.theme), read back from
#     the highlighter Qt holds, an unknown name refused with its near miss or
#     the list. Floor 25 checks.
#   PIXELS -- PIL decodes the pictures: the SVG's box is its colour where the
#     SVG put it (at scale 1 and 2), the background white and the PNG without
#     alpha, and its text antialiased in gray (no colour fringes); the
#     gray-themed editors are the theme's background. qt.capture's own text
#     has subpixel colour fringes, which is printed, not asserted: a known
#     limit for print figures, reported to Studio.
#   VALGRIND, over the figures fixture.
#
# Skips without Qt in the build; the pixel tier without python3's PIL.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/build_has.sh
source tests/valgrind_tier.sh
make >/dev/null

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
fail() { printf 'FAIL %s\n' "$1"; status=1; }

if ! build_has qt; then
    printf 'SKIP run_qt_notebook (no Qt in this build: nothing was measured)\n'
    exit 0
fi

# a syntax path with the repository's gBASIC definition and a gray theme
mkdir -p "$work/kate/themes" "$work/figs"
ln -s "$PWD/editors/kate/syntax" "$work/kate/syntax"
cat > "$work/kate/themes/gray.theme" <<'EOF'
{
    "metadata": { "name": "Gray Print", "revision": 1, "license": "MIT" },
    "text-styles": {
        "Normal": { "text-color": "#202020" },
        "Keyword": { "text-color": "#000000", "bold": true },
        "Comment": { "text-color": "#707070", "italic": true },
        "DecVal": { "text-color": "#404040" },
        "String": { "text-color": "#505050" },
        "Operator": { "text-color": "#303030" },
        "Others": { "text-color": "#303030" },
        "Function": { "text-color": "#202020" },
        "Variable": { "text-color": "#202020" }
    },
    "editor-colors": { "BackgroundColor": "#f2f2f2", "CurrentLine": "#f2f2f2", "LineNumbers": "#909090" }
}
EOF

run_fixture() {   # name floor args...
    local name=$1 floor=$2
    shift 2
    QT_QPA_PLATFORM=offscreen GBASIC_SYNTAX_PATH="$work/kate" timeout -k 5 120 \
        ./gbasic "tests/qt_notebook/$name.bas" "$@" >"$work/$name.out" 2>"$work/$name.err"
    local rc=$?
    local n
    n=$(sed -n 's/^checks: \([0-9]*\)$/\1/p' "$work/$name.out")
    grep -v 'This plugin does not support\|^QThreadStorage' "$work/$name.err" >"$work/$name.err2"
    if [ "$rc" = 0 ] && [ "$(tail -1 "$work/$name.out")" = "mismatches: 0" ] && [ "${n:-0}" -ge "$floor" ] && [ ! -s "$work/$name.err2" ]; then
        printf 'PASS %s (%s checks)\n' "$name" "$n"
        return 0
    fi
    grep -v '^ok ' "$work/$name.out" | head -20
    head -5 "$work/$name.err2"
    fail "$name (exit $rc, floor $floor checks, stderr empty)"
    return 1
}

printf -- '-- figures and themes\n'
figs_ran=0
run_fixture figures_test 25 "$work/figs" && figs_ran=1

printf -- '-- pixels\n'
if [ "$figs_ran" != 1 ]; then
    printf 'SKIP pixels (the fixture did not finish, so there are no pictures)\n'
elif ! python3 -c 'import PIL' 2>/dev/null; then
    printf 'SKIP pixels (no python3 PIL: the pixels were not measured)\n'
elif o=$(python3 -I - "$work/figs" 2>&1 <<'EOF'
import sys
from PIL import Image
d = sys.argv[1]
bad = 0
checks = 0
def check(label, ok, detail=""):
    global bad, checks
    checks += 1
    if not ok:
        bad += 1
        print("MISMATCH", label, detail)
blue = (0x33, 0x66, 0xcc)
own = Image.open(d + "/own.png")
check("own.png has no alpha", own.mode == "RGB", own.mode)
check("the box is blue where the SVG put it", own.getpixel((50, 40))[:3] == blue, own.getpixel((50, 40)))
check("outside it, white", own.getpixel((5, 5))[:3] == (255, 255, 255), own.getpixel((5, 5)))
check("and past its right edge", own.getpixel((95, 40))[:3] == (255, 255, 255), own.getpixel((95, 40)))
x2 = Image.open(d + "/x2.png")
check("at scale 2 the box is twice as far in", x2.getpixel((100, 80))[:3] == blue and x2.getpixel((185, 80))[:3] == (255, 255, 255),
      (x2.getpixel((100, 80)), x2.getpixel((185, 80))))
check("drawn at 2, not stretched: the line is smooth at its own resolution", len(set(x2.get_flattened_data())) > len(set(own.get_flattened_data())),
      (len(set(x2.get_flattened_data())), len(set(own.get_flattened_data()))))
for name in ("own", "x2"):
    im = Image.open(d + "/%s.png" % name).convert("RGB")
    fringes = sum(1 for p in im.get_flattened_data() if max(p) - min(p) > 3 and p != blue)
    check(name + ": text drawn in gray, no colour fringes (grayscale antialiasing)", fringes == 0, "%d" % fringes)
for name in ("themes", "plain"):
    im = Image.open(d + "/%s.png" % name).convert("RGB")
    w, h = im.size
    check(name + ": the theme's background", im.getpixel((w - 30, h - 30)) == (0xf2, 0xf2, 0xf2), im.getpixel((w - 30, h - 30)))
# NOT ASSERTED, MEASURED: qt.capture draws widget text with subpixel
# antialiasing (colour fringes on glyphs), which drawing into an image with
# alpha does not change for QWidget::render. A known limit for print figures.
im = Image.open(d + "/themes.png").convert("RGB")
print("note: qt.capture's subpixel fringes in the gray editor: %d pixels" % sum(1 for p in im.get_flattened_data() if max(p) - min(p) > 3))
th = Image.open(d + "/themes.png").convert("RGB")
dark = sum(1 for p in th.get_flattened_data() if p[0] < 0x90)
check("themes: text was drawn (dark pixels exist)", dark > 20, dark)
print("checks: %d" % checks)
sys.exit(1 if bad else 0)
EOF
); then
    printf 'PASS pixels (%s)\n' "$(printf '%s\n' "$o" | tail -1)"
else
    printf '%s\n' "$o" | head -12
    fail "pixels"
fi

# Access errors only, as the other Qt suites run it: Qt's own allocations at
# exit are not this code's leaks. Compared with $VG_EXIT, never with zero.
if vg_available; then
    mkdir -p "$work/vgfigs"
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_SYNTAX_PATH="$work/kate" \
        vg_run_access_only ./gbasic tests/qt_notebook/figures_test.bas "$work/vgfigs" >/dev/null 2>"$work/vg"
    if [ "$?" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg" | head -5
        fail "valgrind objected"
    else
        printf 'PASS valgrind tests/qt_notebook/figures_test.bas (access)\n'
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
