#!/usr/bin/env bash
set -uo pipefail

# WHAT MAY SHARE A NAME WITH WHAT.
#
# gBASIC has several places a name can come from -- the file you are reading, a
# library you loaded, another library you loaded, and the builtins -- and until
# now exactly one of the combinations was a silent mistake: DEFINING THE SAME
# FUNCTION TWICE IN ONE SCOPE. The second registration overwrote the first's
# statement and returned, so the first became unreachable with nothing said.
# Two functions of one name in one file is a paste that went wrong or an edit
# that missed its twin, and either way the program runs the definition the
# author is less likely to be looking at.
#
# THE REFUSAL IS ABOUT ONE SCOPE AND THE CONTROLS ARE THE POINT. A local
# shadowing a library function, two libraries sharing a name, and a local
# shadowing a builtin are all LEGAL, all documented, and all have their own
# diagnostics. A refusal that reached any of them would be a ban on ordinary
# programs rather than a refusal of a mistake -- so this suite asserts the
# legal neighbours as hard as it asserts the refusals, and there are twice as
# many of them.
#
# THREE REGISTRATION PATHS, and each needs its own case because they are
# genuinely different code: the top-level walk, a program block's declaration
# hoisting, and a library import. The library one is the reason
# `function_find_in_library` exists at all -- `function_find_local` matches
# only NON-imported entries, so it cannot see two definitions inside one
# library, and that case stayed silent after the file-scope one was fixed.
#
# AND WHAT MAY BE CALLED WITHOUT A PREFIX. An unqualified name used to search
# every loaded library, newest registration first. It resolved `ols(...)` after
# `load stats`, which is a real convenience -- and it decided which library a
# call meant by LOAD ORDER, a thing the author of the call does not control and
# cannot see from the call. Adding a `load` at the top of a file could silently
# move a call in the middle of it.
#
# Measured before removing it, by instrumenting `function_resolve` and running
# the whole gate: 4,311 resolutions went through that scan, 3,931 of them one
# library reaching another (`stats.bas` -> `matrix`, five names), and of the 177
# distinct library.function pairs root programs reached, exactly ONE name was
# defined by more than one library. The scan was almost never choosing between
# candidates; it was resolving a unique name by a rule that could have chosen
# wrongly.
#
# THE EXEMPTION IS A LIBRARY'S OWN FUNCTIONS, and it has to hold in every shape
# a library takes -- in its own file, declared in the SAME file as the program,
# and inside a MODIFIER body, which is invoked by a different path. The
# same-file case did not work and nobody could tell: the own-first rule was
# keyed on the source PATH, a same-file library has none, and the cross-library
# scan caught the call on the way past. Removing the scan exposed it.
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

printf 'TIER refusal: the same name twice in one scope\n'
for name in negative_duplicate_function \
            negative_duplicate_function_program \
            negative_duplicate_function_library; do
    if ./gbasic "tests/$name.bas" >"$scratch/out" 2>"$scratch/err"; then
        fail "$name exits nonzero"
    else
        pass "$name exits nonzero"
    fi
    # NOTHING RAN. The refusal happens at registration, so the program must not
    # have produced output before failing -- a duplicate caught halfway through
    # a run would be a different and much weaker property.
    if [ ! -s "$scratch/out" ]; then
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
done

# The message must NAME THE FIRST DEFINITION'S LINE, because "defined twice" on
# its own sends the reader hunting for the other one.
if grep -q "the first, at line 4, would be unreachable" tests/negative_duplicate_function.err; then
    pass "the message names the line of the definition being lost"
else
    fail "the message names the line of the definition being lost"
fi

printf 'TIER control: every same-name case that is LEGAL\n'
if ./gbasic tests/scope_test.bas >"$scratch/ok" 2>"$scratch/okerr"; then
    pass "scope_test exits 0"
else
    fail "scope_test exits 0 ($(head -1 "$scratch/okerr"))"
fi
if grep -q '^mismatches: 0$' "$scratch/ok"; then
    pass "no mismatches"
else
    fail "no mismatches"
    grep '^MISMATCH' "$scratch/ok" | head -6
fi
n=$(sed -n 's/^checks: //p' "$scratch/ok")
if [ -n "$n" ] && [ "$n" -ge 6 ]; then
    pass "check count floor ($n checks)"
else
    fail "check count floor (got '${n:-none}', want >= 6)"
fi
for needle in "two libraries may share a function name" \
              "a local may share a name with a library function" \
              "a local may share a name with a builtin" \
              "two differently-named functions in one scope"; do
    if grep -qF "ok   $needle" "$scratch/ok"; then
        pass "ran: $needle"
    else
        fail "ran: $needle"
    fi
done

# RE-REGISTERING ONE DEFINITION IS NOT A DUPLICATE. This tier passes with the
# AST-pointer guard removed too -- checked, along with the whole gate -- because
# every path that could re-register is already closed: `used_pairs` returns
# early for a library imported twice, an actor child registers into a fresh
# table in its own process, and a program block's hoisting pass and the
# top-level walk never both run. The tier is here as the control for a case the
# refusal must never reach, not as evidence the guard is load-bearing.
printf 'TIER control: an actor re-registers its functions and is not a duplicate\n'
cat >"$scratch/actor.bas" <<'EOF'
function double()
    back = receive()
    send(back, twice(21))
end function

function twice(x)
    return x * 2
end function

program main(args)
    me = self()
    w = spawn double()
    send(w, me)
    print(receive())
end program
EOF
if ./gbasic "$scratch/actor.bas" >"$scratch/aout" 2>"$scratch/aerr"; then
    if [ "$(cat "$scratch/aout")" = "42" ]; then
        pass "a spawned actor still resolves its own top-level function"
    else
        fail "a spawned actor still resolves its own top-level function (got '$(cat "$scratch/aout")')"
    fi
else
    fail "a spawned actor still resolves its own top-level function ($(head -1 "$scratch/aerr"))"
fi

printf 'TIER refusal: a function from ANOTHER library must be qualified\n'
for name in negative_unqualified_library_call negative_unqualified_ambiguous; do
    if ./gbasic "tests/$name.bas" >"$scratch/out" 2>"$scratch/err"; then
        fail "$name exits nonzero"
    else
        pass "$name exits nonzero"
    fi
    if diff -u "tests/$name.err" "$scratch/err" >/dev/null 2>&1; then
        pass "  with the message its golden pins"
    else
        fail "  with the message its golden pins"
        diff -u "tests/$name.err" "$scratch/err" | head -6
    fi
done

# THE ERROR MUST TEACH THE FIX. "invalid function call: ols" sends the reader
# looking for a name that is right there; naming the library and spelling the
# qualified call turns the diagnostic into the edit. This is the half that
# makes the refusal affordable, so it is asserted rather than assumed.
if grep -q "library 'scope_alpha' defines it" tests/negative_unqualified_library_call.err \
   && grep -q "Write scope_alpha.only_alpha(...)" tests/negative_unqualified_library_call.err; then
    pass "the message names the library and spells the qualified call"
else
    fail "the message names the library and spells the qualified call"
fi
# And where more than one library defines the name there is nothing to choose
# on, so it offers both rather than picking.
if grep -q "2 loaded libraries define it" tests/negative_unqualified_ambiguous.err \
   && grep -q "scope_alpha.shared(...), scope_beta.shared(...)" tests/negative_unqualified_ambiguous.err; then
    pass "  and an ambiguous name offers every candidate instead of choosing"
else
    fail "  and an ambiguous name offers every candidate instead of choosing"
fi
# THE CONTROL for the hint: a name NO library defines must still get the plain
# message, or the hint is being manufactured rather than looked up.
cat >"$scratch/unknown.bas" <<'EOF'
program main(args)
    print no_such_function_anywhere(1)
end program
EOF
./gbasic "$scratch/unknown.bas" >/dev/null 2>"$scratch/uerr"
if grep -q "invalid function call: no_such_function_anywhere$" "$scratch/uerr"; then
    pass "  and a name no library defines gets the plain message"
else
    fail "  and a name no library defines gets the plain message ($(tail -c 90 "$scratch/uerr"))"
fi

printf 'TIER exemption: a library reaches its OWN functions unqualified\n'
if GBASIC_PATH=stdlib ./gbasic tests/scope_own_test.bas >"$scratch/own" 2>"$scratch/ownerr"; then
    pass "scope_own_test exits 0"
else
    fail "scope_own_test exits 0 ($(head -1 "$scratch/ownerr"))"
fi
if grep -q '^mismatches: 0$' "$scratch/own"; then
    pass "no mismatches"
else
    fail "no mismatches"
    grep '^MISMATCH' "$scratch/own" | head -6
fi
own_n=$(sed -n 's/^checks: //p' "$scratch/own")
if [ -n "$own_n" ] && [ "$own_n" -ge 5 ]; then
    pass "check count floor ($own_n checks)"
else
    fail "check count floor (got '${own_n:-none}', want >= 5)"
fi
for needle in "a library in its own file calls its own function" \
              "  and gets ITS OWN, not the one loaded after it" \
              "a library in the SAME FILE calls its own function" \
              "a modifier body calls its library's own function" \
              "the root program calls its own function unqualified"; do
    if grep -qF "ok   $needle" "$scratch/own"; then
        pass "ran: $needle"
    else
        fail "ran: $needle"
    fi
done


printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/scope_test.bas >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        tail -20 "$scratch/vg"
    fi
    # And on the refusal path, which allocates a message and unwinds.
    vg_run ./gbasic tests/negative_duplicate_function.bas >/dev/null 2>"$scratch/vg2"
    if [ "$?" != "$VG_EXIT" ]; then
        pass "  nor on the refusal path"
    else
        fail "  nor on the refusal path"
        tail -20 "$scratch/vg2"
    fi
else
    printf '  SKIP valgrind not installed\n'
fi

printf '\nrun_scope: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
