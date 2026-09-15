#!/usr/bin/env bash
# WHERE A LIBRARY MAY COME FROM: beside the loading file, never below it.
#
# The directory holding the file that issues a `load` used to be searched
# RECURSIVELY, and a match there WON, so a stray `chart.bas` three directories
# under that file replaced the standard library. Reported by gdash, for whom
# the exposed names include `crypto` -- every password hash and session secret
# -- and `web`, the server.
#
# THE ASYMMETRY IS WHAT MADE IT WORTH CHANGING. Two libraries claiming one name
# are REFUSED at parse time; a function shadowing a builtin gets a NOTE; a whole
# standard library being replaced by a file several directories below the one
# that asked for it was the QUIETEST of the three and the highest-consequence.
#
# A file BESIDE the loader is a project layout somebody designed, so that still
# overrides -- which is the CONTROL here, and without it this suite would be
# satisfied by a change that simply stopped local libraries working.
#
# AND THE DEEP MATCH IS STILL FOUND AND REPORTED, because dropping it silently
# would be the same class of defect one level along: a project that relied on it
# must be told, with the remedy, rather than quietly served a different library.
# Measured before the change: across the whole gate, ZERO libraries resolved via
# the recursive search, so this cost the tree nothing.
set -euo pipefail
cd "$(dirname "$0")/.."
gb="$PWD/gbasic"

make >/dev/null
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
status=0
tier_ok=1
# `fail` clears the CURRENT tier's flag, and a tier's PASS is printed only
# through `tier_pass`. Written this way because the alternative -- remembering
# to guard each printf -- has been got wrong three times in this tree, and a
# tier that prints FAIL and then PASS is how a red suite reads as green.
fail() { printf 'FAIL %s\n' "$1"; status=1; tier_ok=0; }
tier() { tier_ok=1; }
tier_pass() { [ "$tier_ok" = 1 ] && printf 'PASS %s\n' "$1"; return 0; }

mk() {  # mk <dir-for-the-stray> <what it says>
    rm -rf "$work/p"; mkdir -p "$work/p/lib/deep/deeper"
    cat > "$work/p/lib/mod.bas" <<'BAS'
library mod
    load chart
    function ask()
        return chart.line_xy([1, 2], [3, 4])
    end function
end library
BAS
    printf 'program main(args)\n    load mod from "lib/mod.bas"\n    print left(mod.ask(), 4)\nend program\n' > "$work/p/prog.bas"
    if [ -n "$1" ]; then
        mkdir -p "$work/p/$1"
        cat > "$work/p/$1/chart.bas" <<BAS
library chart
    function line_xy(a, b)
        return "$2"
    end function
end library
BAS
    fi
}

run() { ( cd "$work/p" && GBASIC_PATH="$PWD/../../../stdlib" "$gb" prog.bas 2>"$work/err.txt" ); }

# --- 1. a stray BELOW the loading file must NOT be used ----------------------
tier
mk "lib/deep/deeper" "STRAY"
out="$(run || true)"
# left(...,4) -- comparing against the whole word here would never match and the
# check would be vacuous, which is exactly what it was.
[ "$out" = "STRA" ] && fail "a library three directories below the loader was used"
grep -q "was NOT used: it is below the file that loaded it" "$work/err.txt" \
    || { cat "$work/err.txt"; fail "the demoted deep match was not reported"; }
grep -q "for that file to see it" "$work/err.txt" \
    || fail "the warning does not carry the remedy"
tier_pass 'deep (a library below the loader is not used, and is named with the fix)'

# --- 2. THE CONTROL: beside the loading file it still overrides ---------------
# Without this, everything above is satisfied by a change that broke local
# libraries altogether.
tier
mk "lib" "BESIDE"
out="$(run || true)"
[ "$out" = "BESI" ] || { cat "$work/err.txt"; fail "a library beside the loader no longer overrides (got '$out')"; }
tier_pass 'beside (a library next to the loading file still overrides)'

# --- 3. and with no stray at all, the real library is used -------------------
tier
mk "" ""
out="$(run || true)"
[ "$out" = "<svg" ] || { cat "$work/err.txt"; fail "the standard chart library was not used (got '$out')"; }
[ -s "$work/err.txt" ] && { cat "$work/err.txt"; fail "an ordinary load warned about something"; }
tier_pass 'ordinary (with no local copy, the standard library is used, silently)'

# --- 4. a project that RELIED on the deep search is told, not left guessing ---
tier
rm -rf "$work/q"; mkdir -p "$work/q/lib/nested"
cat > "$work/q/lib/nested/mylib.bas" <<'BAS'
library mylib
    function greet()
        return "nested"
    end function
end library
BAS
cat > "$work/q/lib/mod.bas" <<'BAS'
library mod
    load mylib
    function ask()
        return mylib.greet()
    end function
end library
BAS
printf 'program main(args)\n    load mod from "lib/mod.bas"\n    print mod.ask()\nend program\n' > "$work/q/prog.bas"
( cd "$work/q" && "$gb" prog.bas >/dev/null 2>"$work/err2.txt" ) && fail "a load that can no longer resolve still succeeded"
grep -q "library not found: mylib" "$work/err2.txt" || fail "the failure is not reported as a missing library"
grep -q "was NOT used: it is below" "$work/err2.txt" \
    || { cat "$work/err2.txt"; fail "the file that WOULD have matched is not named"; }
tier_pass 'relied-on (the error says what failed; the warning says why and where)'

exit $status
