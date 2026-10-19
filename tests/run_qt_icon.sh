#!/usr/bin/env bash
# ICONS (docs/qt_gui_design.md §0.23) -- `icon:` on a leaf or a menu item,
# `qt.set(... "icon" ...)`, and `qt.has_icon`.
#
# `set +e` BELOW WITHOUT A MATCHING `set -e`, DELIBERATELY: this file's mode is
# `set -uo pipefail` -- no `-e` -- so `set +e` is a no-op and a `set -e` after
# it would TURN THE MODE ON, which costs nothing until a suite goes red and
# then kills it mid-tier with no verdict. run_portable.sh tripwires it.
#
# IT IS THE ONE PROPERTY THE BY-NAME ROUTE CANNOT CARRY, measured rather than
# assumed: `setProperty("icon", QString(...))` returns FALSE for both a theme
# name and a file path and the icon stays null, QVariant not converting a
# string to a QIcon. So `icon` gets a branch of its own -- in `set_property`,
# the ONE door the declaration's options and `qt.set` both go through, which is
# what makes them unable to disagree about what an icon source is.
#
# SIX MEASUREMENTS, AND FOUR DECIDED SOMETHING:
#
#   setProperty from a string      FALSE, both for a name and for a path --
#                                  so this is an increment, not a doc line
#   QIcon::themeName() here        EMPTY, and hasThemeIcon("document-save")
#                                  FALSE: this machine has NO theme at all
#   QStyle's StandardPixmap        81 keys, and standardIcon works with no
#                                  theme -- the only source that always does
#   QIcon(path) on a real PNG      non-null
#   a QIcon's toString             EMPTY whether or not an icon is set, for a
#                                  button with a real PNG and one with none
#   `icon` as a Q_PROPERTY         QPushButton 62, QAction 4
#
# THE TWO FAILURES ARE ANSWERED DIFFERENTLY AND THAT IS THE DESIGN. A path that
# is not there is a fact we can check and an intent that is unambiguous, so it
# is REFUSED. A theme name that resolves to nothing is a fact about THE
# MACHINE, so refusing would make `icon: "document-save"` fail on exactly the
# headless machines the release targets -- it WARNS (2116), the position §0.14
# takes for a missing syntax definition and for the same reason, that the
# program is usable without the picture and the failure is otherwise INVISIBLE.
# A `SP_` name Qt does not know is refused, because that is Qt's OWN namespace
# and a name in it that Qt rejects is a misspelling. All three asserted.
#
# THE TIER TO READ FIRST IS ORACLE, and it was not foreseen -- it came from
# asking what a check could OBSERVE. A QIcon has no text form, so a declared
# `icon:` was unobservable and every check would have passed on a backend that
# resolved nothing; `qt.read` on an icon was answering "" for a button that HAD
# one, which is a WRONG answer rather than no answer. It refuses now and names
# `qt.has_icon`, which is the oracle and is also what a program needs, an icon
# the theme lacks being a warning rather than a refusal.
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

# A REAL PNG, MADE HERE rather than committed: gBASIC can write the bytes, but
# a binary fixture nothing can reproduce is a fixture that tests itself.
png="$work/one.png"
printf '\211PNG\r\n\032\n\000\000\000\015IHDR\000\000\000\001\000\000\000\001\010\006\000\000\000\037\025\304\211\000\000\000\012IDATx\234c\000\001\000\000\005\000\001\r\n-\264\000\000\000\000IEND\256B`\202' > "$png"
notimage="$work/notimage.txt"
printf 'not an image\n' > "$notimage"
# THE PREMISE, ASSERTED: if the generator is wrong the whole file-path half of
# this suite measures nothing, and "a path was refused" would pass for the
# wrong reason.
if [ "$(wc -c < "$png")" -lt 60 ]; then
    fail "the generated PNG is $(wc -c < "$png") bytes -- the file-path tier would measure nothing"
fi

# ------------------------------------------------------------------ ONE DOOR
# THE DECLARATION AND `qt.set` MUST AGREE ABOUT WHAT AN ICON SOURCE IS, so the
# conversion lives in `set_property` and nowhere else. Two copies would drift
# in the way that matters: a path accepted by one door and refused by the other.
printf 'TIER one door: the conversion lives in set_property alone\n'
convs=$(grep -c 'static int icon_from_text' src/modules/qt_backend.cpp || true)
callers=$(grep -o 'icon_from_text(' src/modules/qt_backend.cpp | wc -l)
if [ "${convs:-0}" = "1" ] && [ "${callers:-0}" = "2" ]; then
    pass "one converter, called from one place"
else
    fail "expected 1 converter and 2 mentions (its own definition and one call), got ${convs:-0}/${callers:-0} -- a second conversion means the two doors can disagree"
fi
# THE VACUITY CONTROL, counting occurrences rather than lines.
sed 's/int how = icon_from_text(/int how = icon_from_text(\/* again *\/ 0 ? 0 : 0) + icon_from_text(/' \
    src/modules/qt_backend.cpp > "$work/two.cpp"
planted=$(grep -o 'icon_from_text(' "$work/two.cpp" | wc -l)
if [ "${planted:-0}" -gt 2 ]; then
    pass "CONTROL: a planted second call is counted ($planted)"
else
    fail "CONTROL: the scan counts lines rather than occurrences"
fi

# ------------------------------------------------------------------ SEMANTICS
printf 'TIER semantics: the three sources, the oracle and the refusals\n'
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic \
    --line-buffered tests/qt_icon_test.bas "$png" "$notimage" \
    > "$work/sem.out" 2> "$work/sem.err"
rc=$?
if grep -q "SKIP no display" "$work/sem.out"; then
    printf '  SKIP (no display -- icons were NOT exercised)\n'
elif [ "$rc" != "0" ]; then
    grep MISMATCH "$work/sem.out" | head -4
    tail -3 "$work/sem.out"; tail -3 "$work/sem.err"
    fail "the fixture exited $rc"
else
    checks=$(grep -c '^ok' "$work/sem.out" || true)
    bad=$(grep -c 'MISMATCH' "$work/sem.out" || true)
    if [ "$bad" = "0" ] && [ "${checks:-0}" -ge 17 ]; then
        pass "$checks checks, 0 mismatches"
    else
        grep MISMATCH "$work/sem.out" | head -6
        fail "$checks checks, $bad mismatches (want >= 17 and none)"
    fi
    grep -v 'This plugin does not support' "$work/sem.err" > "$work/sem.err2" || true
    if [ -s "$work/sem.err2" ]; then
        head -3 "$work/sem.err2"; fail "the fixture wrote to stderr"
    else
        pass "and nothing of its own on stderr"
    fi
    if grep -q "^ok   a declared SP_ name gave a button an icon" "$work/sem.out" &&
       grep -q "^ok   a part that declared none has none" "$work/sem.out"; then
        pass "ORACLE: an icon is observable, and its absence too"
    else
        fail "qt.has_icon did not run -- a QIcon has no text form, so without it every check passes on a backend that resolved nothing"
    fi
    if grep -q "^ok   reading an icon as text is refused" "$work/sem.out"; then
        pass "READ: refused rather than answering an empty string"
    else
        fail "qt.read on an icon was not asserted -- measured, it answers \"\" for a button that HAS one, which is a wrong answer and not no answer"
    fi
    if grep -q "^ok   a path that is not there is refused, naming it" "$work/sem.out" &&
       grep -q "^ok   an unknown SP_ name is refused" "$work/sem.out"; then
        pass "REFUSALS: a missing path and a misspelled standard name"
    else
        fail "the two refusable failures were not measured -- a path and an SP_ name are both facts we can check"
    fi
    if grep -q "^ok   an empty source clears it" "$work/sem.out" &&
       grep -q "^ok   and it can be set again" "$work/sem.out"; then
        pass "CLEAR: taking one away is legal and silent"
    else
        fail "clearing was not measured -- falling through would ask the theme for an icon called \"\""
    fi
    if grep -q "^ok   a button's text still reads normally" "$work/sem.out"; then
        pass "CONTROL: the icon branch did not capture every property"
    else
        fail "the ordinary property route was not asserted beside the icon branch"
    fi
fi

# ------------------------------------------------------------------- WARNING
# THE THIRD OUTCOME, and it needs its own tier because the fixture asserts
# CLEAN STDERR and this one deliberately writes to it. A theme name is a fact
# about the machine, so it WARNS (2116) rather than refusing -- and on a
# machine that DOES have an icon theme this tier legitimately finds the icon
# instead, which it says rather than failing.
printf 'TIER warning: a theme name this machine lacks warns rather than refusing\n'
cat > "$work/theme.bas" <<'BAS'
window app( title: "T", width: 200, height: 120 )
    column
        button go( label: "Go", icon: "document-save" )
    end column
end window
program main(args)
    if not qt.can_display() then
        print "NO DISPLAY"
        return
    end if
    qt.open(app)
    print "FOUND=" + string(qt.has_icon(app, "go"))
    print "STILL RUNNING"
    qt.close(app)
end program
BAS
set +e
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib timeout -k 5 40 ./gbasic \
    --line-buffered "$work/theme.bas" > "$work/t.out" 2> "$work/t.err"
t_rc=$?
if grep -q "NO DISPLAY" "$work/t.out"; then
    printf '  SKIP (no display)\n'
elif [ "$t_rc" != "0" ]; then
    tail -2 "$work/t.err"
    fail "a theme name ended the program -- it must warn, not raise"
elif grep -q "FOUND=true" "$work/t.out"; then
    # A MACHINE WITH A THEME. Says so rather than failing, and still asserts
    # the half that is testable here: nothing was warned about.
    if grep -q '\[2116\]' "$work/t.err"; then
        fail "the icon was FOUND and 2116 was warned anyway"
    else
        pass "this machine HAS an icon theme, so the name resolved and nothing warned"
    fi
elif grep -q '\[2116\]' "$work/t.err" && grep -q "STILL RUNNING" "$work/t.out"; then
    # THREE THINGS, because any one alone is weaker: it warned, it carried the
    # code, and THE PROGRAM CARRIED ON -- which is the whole difference from a
    # refusal and the reason a theme name does not break a headless machine.
    pass "warned 2116, named the theme, and the program carried on"
    if grep -q "icon theme" "$work/t.err"; then
        pass "and the message says where it looked"
    else
        head -1 "$work/t.err"
        fail "the warning does not say it looked in the icon theme"
    fi
else
    head -2 "$work/t.err"
    fail "a theme name neither resolved nor warned -- an icon that silently does nothing is invisible"
fi

# ------------------------------------------------------------------ VALGRIND
# ACCESS-ONLY, for the reason run_qt_backend.sh records. What is NEW here is a
# QIcon built from text on every option and every `qt.set`, and an enum lookup
# through QStyle's static metaobject.
printf 'TIER valgrind: no invalid access across the icon conversions\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    set +e
    QT_ENABLE_REGEXP_JIT=0 QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib \
        vg_run_access_only ./gbasic tests/qt_icon_test.bas "$png" "$notimage" \
        >/dev/null 2>"$work/vg.err"
    vg_rc=$?
    if [ "$vg_rc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised" "$work/vg.err" | head -4
        fail "valgrind objected"
    else
        pass "no invalid access across the conversions and the enum lookup"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_qt_icon: all checks passed\n'
else
    printf 'run_qt_icon: FAILED\n'
fi
exit "$status"
