#!/usr/bin/env bash
# `--add-loads` and a QUALIFIED call (DOGFOOD 16).
#
# WHAT WAS WRONG IS SHARPER THAN THE REPORT. It was filed as "--add-loads did
# not add `load sqlite`; it may handle only .bas libraries and not native
# modules". MEASURED, it added NOTHING AT ALL -- not for `sqlite.open`, and not
# for `stats.zscore` either. The analyser only ever looked at UNQUALIFIED calls,
# resolving a bare function name to whichever library provides it.
#
# That was the whole of the feature when it was written, and the LANGUAGE MOVED
# UNDERNEATH IT: the scope rules made a call into another library QUALIFIED BY
# REQUIREMENT (tests/run_scope.sh), so the shape --add-loads understands is now
# the shape a correct program cannot contain, and the shape every correct
# program DOES contain was invisible. The tool did not fail -- it returned the
# source unchanged, which reads exactly like "you already have every load you
# need". A tool that is silently a no-op is worse than one that is missing.
#
# SELF-CHECKING, and forced for the same reason: the output of this tool is a
# gBASIC program, and a WRONG one still looks like a gBASIC program.
#
# THE CONTROLS OUTNUMBER THE POSITIVE CASES, because the risk in teaching a
# tool to suggest `load X` is that it suggests one for everything -- a `load`
# line the author did not need is a line they must read, understand and delete,
# and enough of them and the tool goes unused. So: an alias already in force, a
# native module that needs no load, a library already loaded, and the ordinary
# unqualified path all have to stay silent or unchanged.
set -u
cd "$(dirname "$0")/.."

make >/dev/null || { echo "FAIL build"; exit 1; }
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
GB="$PWD/gbasic"
export GBASIC_PATH="$PWD/stdlib"

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; status=1; }

# Run --add-loads over TEXT in a directory of its own, so the analyser's
# directory scan cannot pick up a neighbouring fixture and report a library
# that has nothing to do with the case under test.
addloads() {
    local name="$1" text="$2"
    local dir="$work/$name"
    mkdir -p "$dir"
    printf '%s' "$text" > "$dir/case.bas"
    ( cd "$dir" && timeout 60 "$GB" --add-loads case.bas 2>stderr.txt >stdout.txt ) || true
    cat "$dir/stdout.txt"
}
addloads_err() {
    cat "$work/$1/stderr.txt"
}

says() {   # label, needle, haystack
    if printf '%s' "$3" | grep -qxF "$2"; then pass "$1"; else
        fail "$1"; printf '       want line: %s\n       got:\n%s\n' "$2" "$3"; fi
}
lacks() {
    if printf '%s' "$3" | grep -qxF "$2"; then
        fail "$1"; printf '       unwanted line present: %s\n' "$2"
    else pass "$1"; fi
}

echo "== QUALIFIED: the shape the scope rules made mandatory =="
out="$(addloads native 'x = sqlite.open("t.db")
')"
says "a native module gets its load" "load sqlite" "$out"

out="$(addloads libbas 'print stats.zscore(7, 5, 2)
')"
says "a .bas library gets its load" "load stats" "$out"

out="$(addloads both 'x = sqlite.open("t.db")
print stats.zscore(7, 5, 2)
')"
says "both, in one pass (native)" "load sqlite" "$out"
says "both, in one pass (library)" "load stats" "$out"

# The source itself must come through unchanged -- the tool INSERTS, and a
# version that rewrote the program while getting the loads right would be
# worse than the defect it fixes.
says "and the program is untouched" 'print stats.zscore(7, 5, 2)' "$out"

echo
echo "== CONTROLS: what must NOT gain a load =="

# An alias is the name THIS FILE calls the library by, so it is what the
# qualified call is written with. Without recording it, a file that is already
# correct is told to `load st`, which is not a library at all.
out="$(addloads alias 'load stats as st
print st.mean([1, 2, 3])
')"
lacks "an alias in force is not re-loaded" "load st" "$out"
lacks "and neither is the library behind it" "load stats" "$out"
# THE ASSERTION THAT ACTUALLY BITES, and the two above do not: with the alias
# unrecorded, `st` simply falls through to the unresolved path, which ALSO adds
# nothing -- so both `lacks` checks pass on the broken tool and the tier would
# have been vacuous. What changes is that a file which is already correct is
# told its library does not exist. (Measured: without this line the alias
# perturbation is caught by no check in this suite.)
if [ -z "$(addloads_err alias)" ]; then
    pass "and a correct file is told nothing is wrong"
else
    fail "and a correct file is told nothing is wrong"; addloads_err alias
fi

# A native qualifier that answers WITHOUT a load. Telling an author to
# `load money` would be advice that does not work.
out="$(addloads noload 'p = process.run("echo hi")
print p.stdout
')"
lacks "a module that needs no load gets none" "load process" "$out"
if [ -z "$(addloads_err noload)" ]; then
    pass "and nothing is reported about it"
else
    fail "and nothing is reported about it"; addloads_err noload
fi

out="$(addloads already 'load sqlite
x = sqlite.open("t.db")
')"
if [ "$(printf '%s' "$out" | grep -cxF 'load sqlite')" = "1" ]; then
    pass "a load already present is not repeated"
else
    fail "a load already present is not repeated"; printf '%s\n' "$out"
fi

# THE UNQUALIFIED PATH IS UNCHANGED. It is what the tool was built for, it
# still applies inside one library, and a fix that broke it would trade one
# silent no-op for another.
mkdir -p "$work/unqualified"
cat > "$work/unqualified/helper.bas" <<'LIBEOF'
library helper
  function triple(n)
    return n * 3
  end function
end library
LIBEOF
out="$(addloads unqualified 'program demo(args)
    print triple(2)
end program
')"
says "an unqualified call still resolves" "    load helper" "$out"

echo
echo "== UNRESOLVED: a qualifier that names nothing =="
# THE QUALIFIER IS TRUSTED FOR THE FUNCTION, checked only for the LIBRARY.
# `stats.nosuchthing(x)` still gets `load stats`, because the author named the
# library and the tool's job is loads -- a missing function is a different
# mistake, it has its own diagnostic (`invalid function call: stats.mean`), and
# declining to add the load would send the author hunting for a packaging
# problem they do not have. A library can also provide a name through a
# dotted-def or a modifier that this scanner does not model, so "I could not
# find the function" is not evidence the function is absent.
# NOT invented. An unknown qualifier is far likelier to be a typo or a record
# field than a library nobody installed, and a confident `load wibble` on the
# first line sends the author looking for a file that was never meant to exist.
out="$(addloads unknown 'print wibble.thing(1)
')"
lacks "an unknown qualifier invents no load" "load wibble" "$out"
if addloads_err unknown | grep -q 'unresolved library: wibble'; then
    pass "and it is reported by name"
else
    fail "and it is reported by name"; addloads_err unknown
fi

echo
echo "== TRIPWIRE: the loadable list against the flags it describes =="
# `eval_module_needs_load` is a LIST, and a list is a second representation of
# something the code already knows: a module needs a `load` exactly when it
# holds a `<name>_library_loaded` flag that dispatch checks. A module added
# with a flag and forgotten here would simply never be suggested, and nothing
# would say so -- the same silence this suite exists to remove.
flags="$(grep -oE '\b[a-z0-9_]+_library_loaded\b' src/eval.c \
         | sed 's/_library_loaded//' | sort -u)"
listed="$(sed -n '/static const char \*loadable\[\]/,/NULL/p' src/eval.c \
          | grep -oE '"[a-z0-9_]+"' | tr -d '"' | sort -u)"
if [ -n "$flags" ] && [ "$flags" = "$listed" ]; then
    pass "every module with a loaded flag is listed ($(printf '%s' "$flags" | wc -w) of them)"
else
    fail "the loadable list and the _library_loaded flags disagree"
    printf '       flags:  %s\n' "$(printf '%s' "$flags"  | tr '\n' ' ')"
    printf '       listed: %s\n' "$(printf '%s' "$listed" | tr '\n' ' ')"
fi

echo
echo "== ROUND: places is optional (the same entry's other half) =="
if timeout 60 "$GB" examples/round_test.bas >"$work/round.out" 2>"$work/round.err" </dev/null; then
    if grep -q '^MISMATCH' "$work/round.out"; then
        fail "round"; grep '^MISMATCH' "$work/round.out"
    elif [ "$(grep -c '^ok' "$work/round.out")" -ge 12 ]; then
        pass "round(x) and round(x, n) agree and are correct ($(grep -c '^ok' "$work/round.out") checks)"
    else
        fail "round -- too few checks ran"; cat "$work/round.out"
    fi
else
    fail "round (exit)"; cat "$work/round.err"
fi

echo
if [ "$status" = "0" ]; then echo "run_add_loads: all checks passed"; fi
exit "$status"
