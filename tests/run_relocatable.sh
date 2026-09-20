#!/usr/bin/env bash
# A BINARY FINDS THE STDLIB THAT SHIPPED WITH IT.
#
# `GBASIC_DEFAULT_STDLIB` is baked in at compile time as an ABSOLUTE path, so a
# copy of the tree unpacked anywhere else resolved NO library at all and every
# `load` failed. That is what made a downloadable build an INSTALLER rather than
# something a reader can extract and run -- measured while scoping the release
# tarball, not reported by a user, because nobody had yet tried to move one.
#
# `gb_exe_relative_stdlib()` derives <prefix>/share/gbasic/stdlib from
# /proc/self/exe, matching the layout `make install` already produces, so an
# extracted tree and an installed one are the same shape.
#
# SELF-CHECKING, and the LOAD-BEARING TIER IS A DIFFERENCE. "The relocated
# binary loaded the library" is satisfied by a machine that merely has gBASIC
# installed at the compiled-in path -- the library would come from there and the
# check would pass on a binary with none of this in it. So the fixture plants a
# library that exists NOWHERE ELSE and requires the ORIGINAL binary to fail on
# the same program. One answer without the other proves nothing.
#
# THE CONTROLS OUTNUMBER THE POSITIVE CASE, because this changes the lookup path
# every `load` in the language goes down. What must be shown is mostly that
# nothing else moved: GBASIC_PATH still wins (it is the dev override and the way
# this entire tree invokes its own tests), a real stdlib library still loads, a
# binary in no recognisable layout still reports the ordinary error rather than
# resolving from somewhere nobody chose, and a spawned actor -- which re-execs
# the binary in a fresh process -- resolves the same way its parent did.
set -u
cd "$(dirname "$0")/.."

make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }
ORIG="$PWD/gbasic"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; status=1; }
want() { # label, expected, actual
    if [ "$2" = "$3" ]; then pass "$1"; else
        fail "$1"; printf '       want: %s\n       got:  %s\n' "$2" "$3"; fi
}

# The relocated tree, in the layout `make install` produces. Deliberately NOT a
# parent of the working directory: a tree sitting under the CWD is also found by
# the ordinary directory scan, which would make every tier below pass for a
# reason that has nothing to do with the binary's own location.
R="$work/install"; P="$work/work"
mkdir -p "$R/bin" "$R/share/gbasic/stdlib" "$P"
cp "$ORIG" "$R/bin/gbasic"

# A library that exists in the relocated tree and NOWHERE ELSE on this machine.
marker="$R/share/gbasic/stdlib/relocmarker.bas"
cat > "$marker" <<'LIB'
library relocmarker
  function who()
    return "shipped-with-the-binary"
  end function
end library
LIB
printf 'load relocmarker\nprint relocmarker.who()\n' > "$P/p.bas"

echo "== RELOCATED: the binary finds the stdlib beside it =="
got="$( unset GBASIC_PATH; cd "$P" && "$R/bin/gbasic" p.bas 2>&1 )"
want "a relocated binary resolves its own stdlib" "shipped-with-the-binary" "$got"

# THE HALF THAT MAKES THE ABOVE MEAN SOMETHING. Without it, a machine with
# gBASIC installed at the compiled-in prefix passes the tier above regardless.
got="$( unset GBASIC_PATH; cd "$P" && "$ORIG" p.bas 2>&1 | tail -1 )"
case "$got" in
    *"library not found: relocmarker"*)
        pass "and a binary NOT in that tree does not (the difference)" ;;
    *)  fail "and a binary NOT in that tree does not (the difference)"
        printf '       got: %s\n' "$got" ;;
esac

echo
echo "== CONTROLS: what must NOT have moved =="

# GBASIC_PATH is the dev override and how this whole tree runs its tests. If the
# exe-relative directory shadowed it, every suite would silently start reading a
# different stdlib than the one it is testing.
mkdir -p "$work/override"
cat > "$work/override/relocmarker.bas" <<'LIB'
library relocmarker
  function who()
    return "from-GBASIC_PATH"
  end function
end library
LIB
got="$( cd "$P" && GBASIC_PATH="$work/override" "$R/bin/gbasic" p.bas 2>&1 )"
want "GBASIC_PATH still wins over the binary's own stdlib" "from-GBASIC_PATH" "$got"

# A REAL library, not the planted marker: the mechanism has to serve the actual
# stdlib, which is the whole point of shipping one beside the binary.
#
# AND IT MUST BE THE RELOCATED COPY. The first version of this tier just loaded
# `dates` and printed a word -- which PASSED against a build where the helper
# always answered NULL, because gBASIC is installed at the compiled-in prefix on
# this machine and `dates` resolved from there. The tier was measuring nothing.
# Caught by perturbation, not by reading. So the copy carries a function the
# installed one does not have, and resolving from the wrong place is an error
# rather than an identical answer.
cp stdlib/dates.bas "$R/share/gbasic/stdlib/"
python3 - "$R/share/gbasic/stdlib/dates.bas" <<'MARK'
import sys
p = sys.argv[1]
s = open(p).read()
i = s.rfind("end library")
assert i > 0, "dates.bas no longer ends with `end library`"
s = s[:i] + '  function reloc_marker()\n    return "relocated-dates"\n  end function\n' + s[i:]
open(p, "w").write(s)
MARK
printf 'load dates\nprint dates.reloc_marker()\n' > "$P/d.bas"
got="$( unset GBASIC_PATH; cd "$P" && "$R/bin/gbasic" d.bas 2>&1 | tail -1 )"
want "a real stdlib library loads FROM the relocated tree" "relocated-dates" "$got"

# NO LAYOUT AT ALL. A binary that is not <prefix>/bin/gbasic has no prefix to
# derive, and must contribute nothing rather than resolve from some fallback
# nobody chose. The helper answers NULL for exactly this case.
#
# WHAT THIS TIER CANNOT PROVE, AND SAYS SO: the helper's `stat` guard -- which
# makes it answer NULL rather than a path that does not exist -- is NOT
# observable here. Measured: with the guard removed, all six checks stay green,
# because searching a directory that is not there simply finds nothing. The
# guard is kept because it makes the returned value meaningful to any caller
# that does more than search it, but it is DEFENSIVE rather than tested, and a
# tier claiming otherwise would be claiming something this suite cannot see.
mkdir -p "$work/flat"
cp "$ORIG" "$work/flat/gbasic"
got="$( unset GBASIC_PATH; cd "$P" && "$work/flat/gbasic" p.bas 2>&1 | tail -1 )"
case "$got" in
    *"library not found: relocmarker"*)
        pass "a binary in no install layout reports the ordinary error" ;;
    *)  fail "a binary in no install layout reports the ordinary error"
        printf '       got: %s\n' "$got" ;;
esac

# A SPAWNED ACTOR IS A FRESH PROCESS that re-execs the interpreter, so it
# computes its own answer rather than inheriting the parent's. A relocated tree
# whose parent resolves and whose children do not would fail only in programs
# that use actors -- the kind of gap that surfaces long after a release.
cat > "$P/a.bas" <<'ACT'
function child()
  m = receive()
  send(m.from, relocmarker.who())
end function

program demo(args)
  load relocmarker
  a = spawn child()
  send(a, { from: self() })
  r = receive()
  print r
end program
ACT
got="$( unset GBASIC_PATH; cd "$P" && timeout 30 "$R/bin/gbasic" a.bas 2>&1 | tail -1 )"
want "a spawned actor resolves it too" "shipped-with-the-binary" "$got"

echo
if [ "$status" = "0" ]; then echo "run_relocatable: all checks passed"; fi
exit "$status"
