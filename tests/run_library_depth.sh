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

# --- A BROKEN FILE IN THE DIRECTORY IS NOT THE ANSWER (DOGFOOD 42) ---------
# The walk parses every `.bas` beside the loader looking for a `library` block
# with the name. A file it could not parse used to ABORT that walk and become
# the message, so with any half-written program in the directory `load money`
# answered `could not parse library file: ./broken.bas` -- naming a file that
# has nothing to do with `money`, and losing the real answer. Hit for real in a
# scratch directory of sixty throwaway files, which is exactly a beginner's
# directory. A file that does not parse does not define the library being
# looked for, which is all the search needs to know.
rm -rf "$work/r"; mkdir -p "$work/r"
printf 'a = [1, 2]\na[5\n' > "$work/r/broken.bas"
printf 'program main(args)\n    load no_such_library_anywhere\n    print "x"\nend program\n' > "$work/r/prog.bas"
( cd "$work/r" && "$gb" prog.bas >/dev/null 2>"$work/err3.txt" ) && fail "a missing library still resolved"
grep -q "library not found: no_such_library_anywhere" "$work/err3.txt" \
    || { cat "$work/err3.txt"; fail "the missing library is not what was reported"; }
grep -q "broken.bas" "$work/err3.txt" \
    && { cat "$work/err3.txt"; fail "an unrelated unparsable file was named"; }
tier_pass 'unparsable neighbour (the answer is the missing library, not the broken file)'

# THE FIRST CONTROL: the walk must not merely STOP at the broken file either.
# A real library sitting beside it has to still be found, or "skip what does
# not parse" would be indistinguishable from "give up on the first failure".
printf 'library zebra\n    function hi()\n        return "z"\n    end function\nend library\n' > "$work/r/zebra.bas"
printf 'program main(args)\n    load zebra\n    print zebra.hi()\nend program\n' > "$work/r/uses.bas"
( cd "$work/r" && "$gb" uses.bas >"$work/out3.txt" 2>&1 ) \
    || { cat "$work/out3.txt"; fail "a real library beside the broken file was not found"; }
grep -qx "z" "$work/out3.txt" || { cat "$work/out3.txt"; fail "the real library did not answer"; }
tier_pass 'the walk continues past it (a real library beside it is still found)'

# THE SECOND CONTROL, and the one that keeps this from being "never report a
# bad file": a file the caller NAMED must still fail loudly, because there the
# author said which file they meant.
printf 'program main(args)\n    load b from "broken.bas"\n    print "x"\nend program\n' > "$work/r/named.bas"
( cd "$work/r" && "$gb" named.bas >/dev/null 2>"$work/err4.txt" ) && fail "a named broken file was accepted"
grep -q "could not parse library file" "$work/err4.txt" \
    || { cat "$work/err4.txt"; fail "a NAMED broken file must still fail loudly"; }
tier_pass 'a named file still fails loudly (the caller said which file they meant)'

# THE THIRD CONTROL, and the gate found it rather than the design: a file named
# AFTER THE LIBRARY is not an innocent bystander. `watchers.bas` failing to
# parse while `load watchers` is what asked went from a parse error naming the
# file to `library not found` -- which hides the author's own syntax error
# behind a sentence about something else. Naming the file after the library is
# how you say which file you meant, the same as `from "..."`, so it fails
# loudly too. Caught by run_warning_model's collision tier, which loads a
# library file it deliberately makes unparsable; asserted here, where the rule
# lives, rather than left to a suite that meets it by accident.
rm -rf "$work/s"; mkdir -p "$work/s"
printf 'library brokenlib\n    function f(\nend library\n' > "$work/s/brokenlib.bas"
printf 'program main(args)\n    load brokenlib\n    print "x"\nend program\n' > "$work/s/prog.bas"
( cd "$work/s" && "$gb" prog.bas >/dev/null 2>"$work/err5.txt" ) && fail "an unparsable brokenlib.bas was accepted"
grep -q "brokenlib.bas" "$work/err5.txt" \
    || { cat "$work/err5.txt"; fail "the file named after the library was not named"; }
grep -q "library not found" "$work/err5.txt" \
    && { cat "$work/err5.txt"; fail "its syntax error was hidden behind 'library not found'"; }
tier_pass 'the file named after the library still fails loudly (it is the file you meant)'

exit $status
