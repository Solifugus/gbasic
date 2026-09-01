#!/usr/bin/env bash
set -uo pipefail

# A library's unqualified call resolves to its OWN function first.
#
# Until 2026-08-31 it went through the same backward scan every caller uses --
# last registration wins -- so a library's internals were rewired by whatever
# was LOADED AFTER it. Measured: `alpha` and `beta` both define `helper`, and
# `alpha.outer` calls `helper` unqualified. Loading alpha then beta made
# alpha.outer return BETA's helper; the other order did not. A library could
# not call itself reliably, and which one it got depended on a load order it
# does not control.
#
# THE FIX NEEDED TWO SITES, and the second was invisible until a root program
# happened to define the same name: `function_resolve` is not the only path --
# an earlier dispatch consults `function_find_local` DIRECTLY and would
# otherwise hand a root-program function to a library calling itself. Fixing
# only the general resolver left exactly that hole, and the first version of
# this work shipped it until the root case was tried.
#
# THE CONTROL IS WHAT STOPS THIS FROM SEALING LIBRARIES OFF: a library calling
# a name it does NOT define must still reach the global table, and the root
# program must still get its own function. Both asserted, plus that qualified
# calls are untouched.
#
# SELF-CHECKING, because the failure returns a perfectly good string from the
# wrong function -- a golden would have recorded "beta" as the expected answer
# to alpha.outer() and defended it.
#
# Headless, GI-independent, never skips (bar valgrind).

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER semantics\n'
if ( cd tests && GBASIC_PATH=. ../gbasic library_scope_test.bas ) \
        >"$scratch/out" 2>"$scratch/err"; then
    pass "library_scope_test exits 0"
else
    fail "library_scope_test exits 0 ($(head -1 "$scratch/err"))"
fi
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "reports no mismatch"
else
    fail "reports no mismatch"; grep MISMATCH "$scratch/out" | head -5
fi
ran=$(sed -n 's/^checks: //p' "$scratch/out")
if [ "${ran:-0}" -ge 6 ]; then
    pass "ran at least 6 checks (ran ${ran:-0})"
else
    fail "ran at least 6 checks (ran ${ran:-0})"
fi
for label in \
    "a library calls its own function, not a later library's" \
    "nor the root program's function of the same name" \
    'a library still reaches outside for a name it does not define' \
    'the root program gets its own function'
do
    command grep -Fq "ok   $label" "$scratch/out" && pass "asserted: $label" || fail "asserted: $label"
done

printf 'TIER load order does not decide\n'
# The property in one line: the SAME program, with the two loads swapped, must
# give the same answer. Without the fix these differ.
for order in "alpha beta" "beta alpha"; do
    set -- $order
    # Written into tests/ rather than the scratch dir: `load X from "rel/path"`
    # resolves relative to the SOURCE FILE, so a program in /tmp cannot see
    # libscope/.
    cat >"tests/libscope/ord.bas" <<EOF
load $1 from "$1.bas"
load $2 from "$2.bas"
print alpha.outer()
EOF
    got=$( cd tests && GBASIC_PATH=. ../gbasic libscope/ord.bas 2>/dev/null )
    rm -f tests/libscope/ord.bas
    if [ "$got" = "alpha" ]; then
        pass "loaded $1 then $2: alpha.outer is still alpha's"
    else
        fail "loaded $1 then $2: alpha.outer is still alpha's (got '$got')"
    fi
done

printf 'TIER valgrind\n'
if vg_available; then
    if ( cd tests && GBASIC_PATH=. vg_run ../gbasic library_scope_test.bas ) \
            >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_library_scope: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
