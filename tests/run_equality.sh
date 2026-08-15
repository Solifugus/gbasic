#!/usr/bin/env bash
# PLAT-EQ: `=` on two compound values answers a question about the values.
#
# eval_comparison had no branch for arrays or records, so both sides fell
# through to a numeric coercion at the end of the chain where a record becomes 0
# and an array becomes 0. 0 = 0, so ANY two compounds compared equal:
#
#     {x:1} = {y:2}     -> true        [] = [1,2,3]   -> true
#     [1,2] = [3,4,5]   -> true        [1,2] = "hi"   -> true   (both 0)
#
# and the ordering operators silently answered a comparison between two zeros
# rather than refusing.
#
# THE DAMAGE WAS NOT IN THE OPERATOR. Few programs write `if recA = recB`. But
# `contains`, `find` and `remove_value` route through the same comparison via
# values_equal, and `consider` dispatches through it, so:
#
#     people = [{name:"ann"}, {name:"bob"}]
#     contains(people, {name:"zed"})   -> true     (absent value "found")
#     find(people, {name:"bob"})       -> 0        (index of ann)
#     consider aRecord                            (always took the first branch)
#
# An array of records is how every frame in the xlsx pipeline is represented, so
# this was live in shipped code. `unique` was the honourable exception: it
# RAISES on non-scalar arrays rather than guessing, which is what the others
# should have done.
#
# The fix routes `=`/`!=` on compound kinds to value_storage_equal -- which
# already implemented correct deep comparison and is what watchers use to decide
# whether a value changed -- and refuses the ordering operators, following the
# idiom eval_comparison already used for functions, regexes, gobjects and boxed
# values.
#
# Tiers:
#   1. SEMANTICS -- tests/equality_test.bas, SELF-CHECKING. Every check states
#      its own expected answer and prints `ok` or a MISMATCH naming both sides.
#      This matters more than usual here: a golden records whatever the binary
#      answers AS the expected output, so a plain golden would have happily
#      enshrined `true` for {x:1} = {y:2}. The golden pins that every line says
#      ok; the checks decide what correct means.
#   2. ORACLE -- the fixture ALSO compares every pair against a deep equality
#      written in gBASIC itself (walking type/count/keys/has, bottoming out on
#      `=` only for scalars, which were never broken). Where the C comparison
#      and the in-language walk disagree, one is wrong. This is a genuinely
#      separate implementation, not a second call into the same function -- the
#      lesson from PLAT-NUMFMT, whose first oracle shared a path with the thing
#      it was checking and passed against the unfixed binary.
#   3. DISPATCH -- tests/equality_dispatch_test.bas: `consider` on a record and
#      on an array, and watcher firing. The implicit uses, and the ones a caller
#      never thinks of as comparisons.
#   4. REFUSAL -- the ordering operators must raise rather than coerce, with the
#      message pinned. Also wired into run_negative.sh as four cases.
#   5. VALGRIND -- value_storage_equal recurses through shared, refcounted array
#      and record storage; a comparison that consumed or freed a borrow would be
#      a use-after-free that a correct answer would not reveal.
#
# Headless, GI-independent. Never skips (bar valgrind).
set -u

cd "$(dirname "$0")/.."

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_equality: build failed\n'
    exit 1
fi

out=$(mktemp)
err=$(mktemp)
work=$(mktemp -d)
trap 'rm -f "$out" "$err"; rm -rf "$work"' EXIT

status=0

# --- Tier 1: semantics --------------------------------------------------------
if timeout 300 ./gbasic tests/equality_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u tests/equality_test.out "$out"; then
        printf 'PASS tests/equality_test.bas (semantics)\n'
    else
        printf 'FAIL tests/equality_test.bas -- comparison SEMANTICS moved\n'
        status=1
    fi
else
    printf 'FAIL tests/equality_test.bas (exit)\n'
    cat "$err"
    status=1
fi

if grep -q 'MISMATCH' "$out"; then
    printf 'FAIL semantics -- a check reported MISMATCH:\n'
    grep 'MISMATCH' "$out"
    status=1
fi

# --- Tier 2: the in-language oracle -------------------------------------------
if grep -q 'ORACLE DISAGREES' "$out"; then
    printf 'FAIL oracle -- the operator and the in-language walk disagree:\n'
    grep 'ORACLE DISAGREES' "$out"
    status=1
else
    # Coverage, not just absence of failure: if the fixture stopped running its
    # pairs the tier would pass by saying nothing, which is how a suite quietly
    # stops testing. Every `pair` check emits a line ending in ok.
    checks=$(grep -c ' ok$' "$out")
    if [ "$checks" -ge 50 ]; then
        printf 'PASS oracle (%s checks, C comparison and in-language walk agree on every pair)\n' "$checks"
    else
        printf 'FAIL oracle -- only %s checks ran, expected at least 50 (fixture truncated?)\n' "$checks"
        status=1
    fi
fi

# --- Tier 3: the implicit uses ------------------------------------------------
if timeout 300 ./gbasic tests/equality_dispatch_test.bas >"$work/disp.txt" 2>"$err" </dev/null; then
    if diff -u tests/equality_dispatch_test.out "$work/disp.txt"; then
        printf 'PASS tests/equality_dispatch_test.bas (consider + watchers)\n'
    else
        printf 'FAIL tests/equality_dispatch_test.bas -- dispatch or watcher behaviour moved\n'
        status=1
    fi
    # The fixture names the wrong outcomes explicitly, so a wrong branch is a
    # visible word rather than a missing line.
    if grep -q 'WRONG' "$work/disp.txt"; then
        printf 'FAIL dispatch -- a consider took the wrong branch:\n'
        grep 'WRONG' "$work/disp.txt"
        status=1
    fi
else
    printf 'FAIL tests/equality_dispatch_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# --- Tier 4: ordering is refused, not coerced ---------------------------------
# Kept here as well as in run_negative.sh so this suite stands alone: the
# refusal is half the design, and a coercion that silently answered `false`
# would otherwise only be caught in another file.
refuse() { # expr expected-message
    printf 'program main(args)\n  print(%s)\nend program\n' "$1" >"$work/r.bas"
    if ./gbasic "$work/r.bas" >/dev/null 2>"$work/r.err"; then
        printf 'FAIL refusal: %-22s did NOT raise (it answered instead)\n' "$1"
        status=1
        return
    fi
    if grep -q "$2" "$work/r.err"; then
        printf 'PASS refusal %-24s -> %s\n' "$1" "$2"
    else
        printf 'FAIL refusal: %-22s raised the wrong error:\n' "$1"
        cat "$work/r.err"
        status=1
    fi
}
refuse '[1, 2] > [3]'        'arrays support only = and !='
refuse '[1] < [9]'           'arrays support only = and !='
refuse '[1] >= "x"'          'arrays support only = and !='
refuse '{ a: 1 } < { a: 2 }' 'records support only = and !='
refuse '{ a: 1 } <= 5'       'records support only = and !='
refuse '{ a: 1 } !> { b: 2 }' 'records support only = and !='

# --- Tier 5: valgrind ---------------------------------------------------------
if command -v valgrind >/dev/null 2>&1; then
    vg_ok=1
    for fixture in tests/equality_test.bas tests/equality_dispatch_test.bas; do
        if ! valgrind --error-exitcode=9 --leak-check=full \
                      --errors-for-leak-kinds=definite \
                      ./gbasic "$fixture" >"$work/vg.txt" 2>"$err" </dev/null; then
            printf 'FAIL valgrind %s\n' "$fixture"
            cat "$err"
            status=1
            vg_ok=0
        fi
    done
    [ "$vg_ok" = "1" ] && printf 'PASS valgrind (both fixtures)\n'
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit "$status"
