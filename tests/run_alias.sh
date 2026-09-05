#!/usr/bin/env bash
set -uo pipefail

# `load NAME as ALIAS` -- THE NAME A FILE CALLS A LIBRARY BY.
#
# A library used to be reachable by exactly one name, the one on its own
# `library` block, and that name was claimed by whichever file happened to
# declare it. Two libraries that picked the same name could not coexist in one
# program at all: the second import met the duplicate-function refusal and
# stopped, with nothing the loading file could say about it even when it had
# named both files by full path. MEASURED before building this, with two
# throwaway files each declaring `library util`:
#
#   runtime error: function 'hello' is defined twice in library 'util'
#
# which names a function neither file defined twice and never mentions that
# there are two files.
#
# AN ALIAS IS AN IMPORT IDENTITY, NOT A LOOKUP-TIME RENAME. Registration keys
# on the effective name, so the same declared name can arrive from two paths at
# once. That choice is the whole feature: a scoped rename would have bought the
# typing convenience and left the collision exactly where it was, because both
# imports would still have registered under the one declared name.
#
# THE LOAD-BEARING TIER IS THE PAIR OF VENDORS, AND IT IS ASSERTED AS A
# DIFFERENCE. "Both aliases resolved" is satisfied by an implementation that
# merged the two imports and answered both from whichever won, so the two
# libraries disagree about what their shared functions return and the fixture
# says which answer it wants from which name. Every other tier checks a
# component; this one checks the identities are genuinely separate.
#
# THE CONTROLS OUTNUMBER THE REFUSALS, deliberately. Aliasing touches the path
# every qualified call in the language goes down, so what has to be shown is
# mostly that nothing else moved: an unaliased load is unchanged, an aliased
# library still reaches its own functions unqualified, its dependencies keep
# their own names, and its exported modifiers still resolve -- three different
# resolvers, none of which knows about the other two.
#
# Headless, GI-independent, never skips (bar valgrind).

cd "$(dirname "$0")/.."
. tests/valgrind_tier.sh
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER semantics: what an alias does and what it leaves alone\n'
if ./gbasic tests/alias_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "alias_test exits 0"
else
    fail "alias_test exits 0 ($(head -1 "$scratch/err"))"
fi
if grep -q '^mismatches: 0$' "$scratch/out"; then
    pass "no mismatches"
else
    fail "no mismatches"
    grep '^MISMATCH' "$scratch/out" | head -8
fi
# COVERAGE FLOOR. A fixture that stops running its checks otherwise passes by
# saying nothing, which is the quiet way this tier goes vacuous.
n=$(sed -n 's/^checks: //p' "$scratch/out")
if [ -n "$n" ] && [ "$n" -ge 10 ]; then
    pass "check count floor ($n checks)"
else
    fail "check count floor (got '${n:-none}', want >= 10)"
fi
for needle in "an alias reaches the library it was loaded from" \
              "and a second alias reaches the OTHER one" \
              "an aliased library still reaches its own functions" \
              "and its dependency keeps ITS name" \
              "an exported modifier survives the rename" \
              "an unaliased library answers to its own name"; do
    if grep -qF "ok   $needle" "$scratch/out" || grep -qF "ok     $needle" "$scratch/out"; then
        pass "ran: $needle"
    else
        fail "ran: $needle"
    fi
done

# THE DIFFERENCE, STATED SEPARATELY FROM THE FIXTURE'S OWN VERDICT. If the two
# imports had merged, both names would answer the same and the fixture's
# mismatch count would be the only thing objecting -- so the transcript is also
# read here for BOTH answers, which cannot both be present in a merged build.
printf 'TIER difference: two libraries of one declared name stay separate\n'
if grep -qF "so one declared name can arrive twice" "$scratch/out" && \
   grep -qF "ok" <(grep -F "so one declared name can arrive twice" "$scratch/out"); then
    pass "A:1/B:1 -- the two identities answer differently"
else
    fail "A:1/B:1 -- the two identities answer differently"
fi

printf 'TIER refusal: a name may answer for exactly one library\n'
for name in negative_alias_native_target \
            negative_alias_native_name \
            negative_alias_duplicate \
            negative_alias_shadows_library \
            negative_alias_shadowed_by_library \
            negative_alias_same_declared_name \
            negative_alias_declared_name_gone; do
    if ./gbasic "tests/$name.bas" >"$scratch/out" 2>"$scratch/err"; then
        fail "$name exits nonzero"
    else
        pass "$name exits nonzero"
    fi
    # NOTHING RAN. Every one of these is caught at import, before a line of the
    # program executes -- except the last, which is a call and is expected to
    # have got that far.
    if [ "$name" = negative_alias_declared_name_gone ] || [ ! -s "$scratch/out" ]; then
        pass "  and nothing ran"
    else
        fail "  and nothing ran ($(head -c 60 "$scratch/out"))"
    fi
    if diff -u "tests/$name.err" "$scratch/err" >/dev/null 2>&1; then
        pass "  with the message its golden pins"
    else
        fail "  with the message its golden pins"
        diff -u "tests/$name.err" "$scratch/err" | head -6
    fi
    cp "$scratch/err" "$scratch/$name.said"
done

# THE COLLISION MESSAGE MUST NAME BOTH SOURCES AND SPELL THE FIX. Without the
# two paths a reader cannot tell which `toolkit` is which, and the old failure
# named a function instead of the files -- which is the whole reason this
# refusal was rewritten rather than left where it was.
printf 'TIER diagnostic: the refusal teaches the fix\n'
msg="$scratch/negative_alias_same_declared_name.said"
if grep -q "vendor_a/toolkit.bas" "$msg" && grep -q "vendor_b/toolkit.bas" "$msg"; then
    pass "the collision message names both files"
else
    fail "the collision message names both files"
fi
if grep -q 'as my_toolkit' "$msg"; then
    pass "and spells the load that resolves it"
else
    fail "and spells the load that resolves it"
fi
# A RENAME THAT MISSED A CALL SITE is the mistake this feature makes easiest to
# write, and "invalid function call: toolkit.describe" sends the reader looking
# for a missing function rather than at the name.
if grep -q "was loaded as 'ta'" "$scratch/negative_alias_declared_name_gone.said"; then
    pass "a call to the replaced name names the alias in force"
else
    fail "a call to the replaced name names the alias in force"
fi

# BOTH DIRECTIONS OF THE COLLISION, because a rule that only holds one way
# around is a rule about statement order rather than about names.
if grep -q "already refers to library 'scope_alpha'" "$scratch/negative_alias_shadows_library.said" && \
   grep -q "already refers to library 'scope_beta'" "$scratch/negative_alias_shadowed_by_library.said"; then
    pass "the collision is refused whichever load comes first"
else
    fail "the collision is refused whichever load comes first"
fi

printf 'TIER control: the whole stdlib still loads unaliased\n'
# The gate at large is the real control, but a library with a DEPENDENCY it
# calls qualified is the case aliasing could most plausibly have broken while
# leaving simple libraries working, so it is asserted here too.
cat >"$scratch/plain.bas" <<'EOF'
load stats
print stats.zscore(2, 1, 1)
EOF
if GBASIC_PATH=stdlib ./gbasic "$scratch/plain.bas" 2>"$scratch/err" | grep -q '^1$'; then
    pass "an unaliased stats reaches matrix and answers"
else
    fail "an unaliased stats reaches matrix and answers ($(head -1 "$scratch/err"))"
fi
cat >"$scratch/aliased.bas" <<'EOF'
load stats as st
print st.zscore(2, 1, 1)
EOF
if GBASIC_PATH=stdlib ./gbasic "$scratch/aliased.bas" 2>"$scratch/err" | grep -q '^1$'; then
    pass "and so does an aliased one"
else
    fail "and so does an aliased one ($(head -1 "$scratch/err"))"
fi

# AN ALIAS ADDS NO SECOND NAME OF ITS OWN, but a library name is available when
# SOMETHING in the program loaded the library under it -- which has always been
# true and is why `stats.bas` reaching `matrix` makes `matrix.` work in a root
# program that never loaded it. So aliasing a library another library also loads
# plainly gives BOTH names, and that is deliberate: refusing it would let any
# library decide which names its consumers may pick, and every stdlib
# dependency would remove a word from the caller's vocabulary.
printf 'TIER coexistence: an alias does not un-load a name something else claimed\n'
cat >"$scratch/dual.bas" <<'EOF'
load stats
load matrix as m
print string(m.mat_transpose([[1, 2]]))
print string(matrix.mat_transpose([[1, 2]]))
EOF
GBASIC_PATH=stdlib timeout -k 5 30 ./gbasic "$scratch/dual.bas" >"$scratch/dual.out" 2>"$scratch/err"
if [ "$(grep -c '^\[\[1\],\[2\]\]$' "$scratch/dual.out")" = 2 ]; then
    pass "matrix reached through stats AND through the alias, same answer"
else
    fail "matrix reached through stats AND through the alias, same answer ($(head -1 "$scratch/err"))"
fi

# AN ACTOR IS A FRESH PROCESS WITH A FRESH FUNCTION TABLE. The child is
# fork+exec: it re-parses the source and re-runs the `load`, so the effective
# name has to be reconstructed there rather than carried across -- and a
# function VALUE crossing the wire carries a library name that must mean the
# same thing on both sides.
printf 'TIER actor: an alias survives fork+exec\n'
# No `program` block: a spawned function runs in a re-exec'd child that never
# enters one, so a library it needs has to be loaded at the top level.
cat >"$scratch/actor.bas" <<EOF
load alias_host from "$PWD/tests/libs/alias_host.bas" as host

function worker()
    back = receive()
    fn = host.outer
    send(back, fn() + "/" + host.through_dep())
end function

me = self()
w = spawn worker()
send(w, me)
print(receive())
EOF
if timeout -k 5 30 ./gbasic "$scratch/actor.bas" 2>"$scratch/err" | grep -q '^host helper/dep$'; then
    pass "the child resolves the alias and a function value through it"
else
    fail "the child resolves the alias and a function value through it ($(head -1 "$scratch/err"))"
fi

printf 'TIER valgrind\n'
# The import registry allocates three strings per effective name and the alias
# threads a new AST field through the parser -- both are per-parse leaks that no
# functional test can see. PLAT-OPTPARAM's six-site free is the precedent: three
# of its six missing frees turned 22 suites red on their own valgrind tiers and
# were invisible to every functional test.
if vg_available; then
    if vg_run ./gbasic tests/alias_test.bas >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        tail -20 "$scratch/vg"
    fi
    # And on the refusal path, which allocates a message and unwinds mid-import.
    vg_run ./gbasic tests/negative_alias_same_declared_name.bas >/dev/null 2>"$scratch/vg2"
    if [ "$?" != "$VG_EXIT" ]; then
        pass "  nor on the refusal path"
    else
        fail "  nor on the refusal path"
        tail -20 "$scratch/vg2"
    fi
else
    printf '  SKIP valgrind not installed\n'
fi

printf '\n%d checks, %d failures\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
