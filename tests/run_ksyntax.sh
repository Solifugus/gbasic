#!/usr/bin/env bash
# DOES THE KATE DEFINITION THIS REPO ALREADY SHIPS WORK WITH A QT EDITOR'S
# HIGHLIGHTING ENGINE?
#
# docs/qt_gui_design.md §0.1 records Studio's move from GTK to Qt and called its
# EDITOR the piece with no Qt counterpart -- GTK's answer being GtkSourceView
# plus a `.lang` file. MEASURED 2026-10-17, THAT WAS WRONG IN THE HELPFUL
# DIRECTION: the nearest Qt answer is KDE's KSyntaxHighlighting, which IS the
# extracted Kate engine, and `editors/kate/gbasic.xml` was written for Kate --
# so the file is already the input it wants, unchanged.
#
# WHAT WAS MEASURED (Ubuntu's libkf6syntaxhighlighting 6.24.0):
#   * LICENCE: MIT for the project (`Files: *` in its own copyright). The
#     GPL-2.0-only items are ONE bundled definition (nesC), a test fixture and
#     the Debian packaging -- so, unlike Qt itself, it is STATICALLY LINKABLE
#     and Apache-2.0 compatible.
#   * WEIGHT: one 2 MB .so. Runtime deps are libc, Qt6 Core/Gui/Network and
#     libstdc++ -- no KDE desktop, no KParts. The `-data` package is ONLY
#     translations: the definitions and themes are baked into the .so.
#   * `editors/kate/gbasic.xml` LOADS AND CLASSIFIES: 20 formats over five
#     lines -- Comment, Keyword, Number, String, Symbol.
#   * NO WIDGET AND NO QApplication is needed, because AbstractHighlighter is a
#     pure callback interface. That is BETTER than GtkSourceView, where the
#     equivalent needs a widget, and it is why this tier is headless.
#
# THE TRAP, AND IT IS THE KIND THAT DOES NOT ANNOUNCE ITSELF: *.bas is claimed
# by TWO definitions at the SAME priority -- the bundled FreeBASIC one and ours
# -- and lookup BY EXTENSION picks FreeBASIC. There is no error; there is
# PLAUSIBLE BUT WRONG highlighting, FreeBASIC being a near neighbour (it
# classifies a `Functions` kind where gBASIC classifies `Symbol`). A host must
# ask BY NAME. This suite asserts both halves, because "gBASIC was found" alone
# passes on a machine whose engine happens to lack FreeBASIC.
#
# THIS TIER IS LIVE ON THE DEVELOPMENT MACHINE since 2026-10-17, Matthew having
# installed `libkf6syntaxhighlighting-dev`. It still SKIPS CLEANLY where the
# library is absent, saying that the definition went unchecked, and it does not
# fetch anything: no suite in this tree uses the network.
#
# THE FIGURES ABOVE WERE FIRST TAKEN AGAINST AN EXTRACTED PACKAGE, through the
# KSYNTAX_* override below, because the library was not installed when this was
# written -- and the installed run then reported THE SAME NUMBERS, which is what
# makes that method trustworthy rather than merely convenient.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

if ! pkg-config --exists Qt6Gui 2>/dev/null; then
    printf 'SKIP run_ksyntax (no Qt6Gui development files -- the definition was NOT checked)\n'
    exit 0
fi
if ! command -v g++ >/dev/null 2>&1; then
    printf 'SKIP run_ksyntax (no C++ compiler -- the definition was NOT checked)\n'
    exit 0
fi
# The headers moved between KF5 and KF6; only KF6 is tested.
# KSYNTAX_INCLUDE / KSYNTAX_LIB point at a tree that is not installed -- a local
# build, or an extracted package. IT IS NOT A CONVENIENCE: the library is absent
# from the development machine, so without it the live path of this suite could
# never have been RUN before being committed, and a tier whose only exercised
# branch is its skip is the green line that measures nothing. The measurements
# in the header above were taken this way.
inc="${KSYNTAX_INCLUDE:-}"
if [ -z "$inc" ]; then
    for c in /usr/include/KF6/KSyntaxHighlighting; do
        [ -d "$c" ] && inc="$c"
    done
fi
libdir="${KSYNTAX_LIB:-}"
if [ -z "$inc" ]; then
    printf 'SKIP run_ksyntax (KSyntaxHighlighting is not installed, so `editors/kate/gbasic.xml` was NOT checked against the engine a Qt editor would use -- `sudo apt install libkf6syntaxhighlighting-dev` makes this tier live)\n'
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

printf 'TIER build: the probe, headless and with no widget\n'
if g++ -std=c++17 -o "$work/probe" tests/qt/ksyntax_probe.cpp -I"$inc" \
     $(pkg-config --cflags Qt6Gui) ${libdir:+-L$libdir} -lKF6SyntaxHighlighting \
     $(pkg-config --libs Qt6Gui) 2>"$work/cxx.err"; then
    pass "compiled against KSyntaxHighlighting with no QApplication"
else
    head -20 "$work/cxx.err"; fail "the probe did not build"; exit $status
fi

# The probe wants a search path holding `syntax/<file>.xml`, which is the
# engine's own layout -- so the repo's file is copied rather than symlinked into
# a shape it does not have.
mkdir -p "$work/custom/syntax"
cp editors/kate/gbasic.xml "$work/custom/syntax/"

printf 'TIER the definition this repo ships\n'
set +e
out="$(cd "$work" && QT_QPA_PLATFORM=offscreen \
        LD_LIBRARY_PATH="${libdir:-}${libdir:+:}${LD_LIBRARY_PATH:-}" \
        timeout -k 5 60 ./probe 2>/dev/null)"
rc=$?
set -e
if [ "$rc" != "0" ]; then
    printf '%s\n' "$out"; fail "the probe did not run (exit $rc)"; exit $status
fi
# THE FIRST TOKEN ONLY. The probe prints two values on a line
# (`by_name=gBASIC priority=0`), and the first version of this took everything
# after the `=` -- so a correct run reported `by_name=gBASIC priority=0` and the
# check for `gBASIC` failed. Found by running the LIVE path against an extracted
# package, which is the whole reason the KSYNTAX_* override exists: the skip
# path would have hidden this for as long as the library stayed uninstalled.
field() { printf '%s\n' "$out" | sed -n "s/^$1=//p" | head -1 | awk '{print $1}'; }
# AND A WHOLE-LINE FORM, because `kinds` is a list whose members contain spaces
# ("Normal Text") -- the first-token rule above truncated it to "Comment,Keyword,
# Normal" and the String check then failed on a correct run. Two shapes of value,
# two extractors, rather than one that is wrong for half of them.
field_line() { printf '%s\n' "$out" | sed -n "s/^$1=//p" | head -1; }

if [ "$(field by_name)" = "gBASIC" ]; then
    pass "editors/kate/gbasic.xml loads unchanged, by name"
else
    printf '%s\n' "$out"; fail "the definition did not load: by_name=$(field by_name)"
fi

formats=$(field formats)
kinds=$(field_line kinds)
# A DEFINITION THAT LOADS AND CLASSIFIES NOTHING IS WORSE THAN NONE, so the
# claim is the classification rather than the load.
if [ "${formats:-0}" -ge 10 ] \
   && printf '%s' "$kinds" | grep -q Comment \
   && printf '%s' "$kinds" | grep -q Keyword \
   && printf '%s' "$kinds" | grep -q String; then
    pass "and it classifies: $formats formats over five lines -- $kinds"
else
    printf '%s\n' "$out"; fail "the definition loaded but classified too little: $formats formats, $kinds"
fi

printf 'TIER the extension collision, which fails SILENTLY\n'
by_ext="$(field by_extension)"
claims=$(printf '%s\n' "$out" | grep -c '^claims \*\.bas:')
if [ "$claims" -ge 2 ] && [ "$by_ext" != "gBASIC" ]; then
    pass "*.bas is claimed by $claims definitions and lookup BY EXTENSION picks '$by_ext' -- a host must ask by NAME"
elif [ "$claims" -lt 2 ]; then
    pass "SKIP: only $claims definition claims *.bas on this engine, so the collision could not be produced and NOTHING was measured about it"
else
    printf '%s\n' "$out"
    fail "extension lookup picked gBASIC, so the collision this tier warns about is gone -- check whether a priority was added to gbasic.xml, and if so say so where the warning is"
fi

exit $status
