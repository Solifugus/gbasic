#!/usr/bin/env bash
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
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

# ONE FILE BY TWO SPELLINGS IS ONE LIBRARY. Transward's layout, in shape: the
# program loads `util.bas`, and a library in `web/` loads the same file as
# `../util.bas`. Paths were compared as TEXT, so `./util.bas` and
# `./web/../util.bas` were two libraries claiming one name and the program was
# refused before a line ran (10 of Transward's 25 tests, 2026-11-05).
# THE CONTROLS ARE WHAT KEEP THIS HONEST: two DIFFERENT files declaring one
# name are still refused (negative_alias_same_declared_name, above), and a
# cycle spelled two ways is reported as the cycle it is -- before the fix it
# was misreported as a name collision.
printf 'TIER spelling: one file reached by two path spellings is one library\n'
here="$PWD"
tw="$scratch/tw"
mkdir -p "$tw/src/web" "$tw/cyc/sub"
printf 'library util\nfunction hi()\n    return "hi"\nend function\nend library\n' \
    >"$tw/src/util.bas"
printf 'library server\nload util from "../util.bas"\nfunction greet()\n    return util.hi() + " from server"\nend function\nend library\n' \
    >"$tw/src/web/server.bas"
printf 'load util from "util.bas"\nload server from "web/server.bas"\nprint server.greet()\nprint util.hi()\n' \
    >"$tw/src/main.bas"
for where in src .; do
    prog=main.bas
    [ "$where" = . ] && prog=src/main.bas
    (cd "$tw/$where" && timeout -k 5 30 "$here/gbasic" "$prog") \
        >"$scratch/sp.out" 2>"$scratch/sp.err"
    if [ "$(cat "$scratch/sp.out")" = "$(printf 'hi from server\nhi')" ] && [ ! -s "$scratch/sp.err" ]; then
        pass "util.bas and web/../util.bas are one library (run from $where)"
    else
        fail "util.bas and web/../util.bas are one library (run from $where): $(head -c 200 "$scratch/sp.err")"
    fi
done
ln -s util.bas "$tw/src/util_link.bas"
printf 'load util from "util.bas"\nload util from "util_link.bas"\nprint util.hi()\n' >"$tw/src/linked.bas"
if (cd "$tw/src" && timeout -k 5 30 "$here/gbasic" linked.bas) >"$scratch/sp.out" 2>"$scratch/sp.err" \
   && [ "$(cat "$scratch/sp.out")" = hi ]; then
    pass "a symbolic link to the file is the same file"
else
    fail "a symbolic link to the file is the same file: $(head -c 200 "$scratch/sp.err")"
fi
printf 'library a\nload b from "sub/b.bas"\nfunction fa()\n    return 1\nend function\nend library\n' \
    >"$tw/cyc/a.bas"
printf 'library b\nload a from "../sub/../a.bas"\nfunction fb()\n    return 2\nend function\nend library\n' \
    >"$tw/cyc/sub/b.bas"
printf 'load a from "cyc/a.bas"\nprint a.fa()\n' >"$tw/cmain.bas"
(cd "$tw" && timeout -k 5 30 "$here/gbasic" cmain.bas) >"$scratch/sp.out" 2>"$scratch/sp.err"
if grep -qF "circular use detected for a from" "$scratch/sp.err" && [ ! -s "$scratch/sp.out" ]; then
    pass "a cycle spelled two ways is reported as a cycle, and nothing ran"
else
    fail "a cycle spelled two ways is reported as a cycle: $(head -c 200 "$scratch/sp.err")"
fi

# AN ACTOR IS A FRESH PROCESS WITH A FRESH FUNCTION TABLE. The child is
# fork+exec: it re-parses the source and re-runs the `load`, so the effective
# name has to be reconstructed there rather than carried across -- and a
# function VALUE crossing the wire carries a library name that must mean the
# same thing on both sides.
printf 'TIER actor: an alias survives fork+exec\n'
# No `program` block: a spawned function runs in a re-exec'd child that never
# enters one, so a library it needs has to be loaded at the top level.
. tests/build_has.sh
if ! build_has actors; then
    printf '  SKIP actor (actors are not available on this platform)\n'
else
# The path is written INTO a program, so it must be one gbasic can open: under
# MSYS2 $PWD is /c/..., which a native gbasic.exe cannot (the run_silent_traps
# idiom). This tier skipped on Windows until actors worked there.
here_native="$(cygpath -m "$PWD" 2>/dev/null || printf '%s' "$PWD")"
cat >"$scratch/actor.bas" <<EOF
load alias_host from "$here_native/tests/libs/alias_host.bas" as host

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
fi   # build_has actors

printf "TIER private: a library's own loads are its own\n"
# docs/library_private_loads_design.md (Transward §7.6, 2026-11-07). alpha,
# beta and the program each load a different library declared `util`, and
# each util.who() reaches its own; a function value keeps its library; a trace
# and library_collisions() show names as written. Before this, alpha's own
# `load util` was refused because the program had claimed the name.
if ./gbasic tests/private_loads/main.bas >"$scratch/pl.out" 2>"$scratch/pl.err" &&
   grep -q '^mismatches: 0$' "$scratch/pl.out" &&
   [ "$(sed -n 's/^checks: //p' "$scratch/pl.out")" -ge 9 ]; then
    pass "three libraries declared util, each reached by its own loader ($(sed -n 's/^checks: //p' "$scratch/pl.out") checks)"
else
    fail "three libraries declared util, each reached by its own loader"
    grep MISMATCH "$scratch/pl.out" | head -5; head -3 "$scratch/pl.err"
fi
if ./gbasic tests/private_loads/twice.bas >"$scratch/tw.out" 2>"$scratch/tw.err"; then
    fail "one file claiming one name for two libraries is still refused"
elif grep -qF "the name 'util' already refers to library 'util' from tests/private_loads/alpha_util.bas" "$scratch/tw.err" &&
     [ ! -s "$scratch/tw.out" ]; then
    pass "one file claiming one name for two libraries is still refused, and nothing ran"
else
    fail "one file claiming one name for two libraries: $(head -1 "$scratch/tw.err")"
fi
# Transward's own case, which needs stdlib's mcp (and so webclient)
printf 'load webclient\nprint "ok"\n' >"$scratch/wc.bas"
if [ "$(./gbasic "$scratch/wc.bas" 2>/dev/null)" = ok ]; then
    # written here, not kept in the tree: a `library tools` anywhere under
    # tests/ is found by every test that does `load tools`, which then warns
    # (2103) that it was not used -- measured, three suites
    mkdir -p "$scratch/withmcp"
    printf 'library tools\n    function hello()\n        return "the application'"'"'s tools"\n    end function\nend library\n' \
        >"$scratch/withmcp/tools.bas"
    cat >"$scratch/withmcp/main.bas" <<'MCPEOF'
load tools
load mcp
function served(args)
    return "served"
end function
print(tools.hello())
ts = { name: "t", entries: [ { name: "served", describe: "s", params: [], reads: [], mutates: [], fn: served } ] }
r = decode(mcp.handle(ts, { name: "x", version: "1" }, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"))
print(r.result.tools[0].name)
MCPEOF
    got="$(cd "$scratch/withmcp" && GBASIC_PATH="$OLDPWD/stdlib" "$OLDPWD/gbasic" main.bas 2>/dev/null | tr '\n' '|')"
    if [ "$got" = "the application's tools|served|" ]; then
        pass "an application's own library tools beside stdlib's mcp, which loads stdlib's tools"
    else
        fail "an application's own tools beside mcp: got '$got'"
    fi
else
    printf '  SKIP an application tools beside mcp (no webclient in this build -- NOT measured)\n'
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
    vg_run ./gbasic tests/private_loads/main.bas >/dev/null 2>"$scratch/vg3"
    if [ "$?" != "$VG_EXIT" ]; then
        pass "  nor over three libraries declared util, each private"
    else
        fail "  nor over three libraries declared util, each private"
        tail -20 "$scratch/vg3"
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
