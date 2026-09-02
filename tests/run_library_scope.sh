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

printf 'TIER the built-in collision warning\n'
# d08409f made a library resolve its OWN functions first. That reached the
# BUILT-IN case too, and the diagnostic did not follow it (reported by the
# gdash session, 2026-09-01). Two separate defects, and the second is the
# worse one.
#
# (1) THE MESSAGE DESCRIBED THE OPPOSITE OF WHAT HAPPENS. It read "unqualified
#     calls use the built-in", which is true OUTSIDE the library and false at
#     the declaration it points at.
#
# (2) THE POSITION WAS THE CALLER'S. Registration-time warnings fire while the
#     `load` statement runs, so the line came from the CALLING file and was
#     printed beside the LIBRARY's path -- a mismatched pair that looks like a
#     real location. And since the position is also the DEDUPLICATION key,
#     every collision in one library shared the one load line: a library
#     shadowing three built-ins warned about ONE and swallowed two.
mkdir -p "$scratch/w"
cat >"$scratch/w/wlib.bas" <<'BAS'
library wlib
    function lines(t)
        return split(string(t), chr(10))
    end function

    function chars(t)
        return t
    end function

    function folders(t)
        return t
    end function

    function count_them(t)
        return count(lines(t))
    end function
end library
BAS
cat >"$scratch/w/wuse.bas" <<'BAS'
load wlib from "wlib.bas"
print "inside:  " + string(wlib.count_them("a" + chr(10) + "b"))
on error goto next
r = lines("a" + chr(10) + "b")
if error then
  print "outside: builtin"
  error.clear()
else
  print "outside: library"
end if
on error stop
BAS
./gbasic "$scratch/w/wuse.bas" >"$scratch/w/out" 2>"$scratch/w/err"

# ONE WARNING PER COLLISION. This is the tier that catches the dedup
# swallowing: the count, not the content.
n=$(grep -c "same name as a built-in" "$scratch/w/err")
if [ "$n" = 3 ]; then
    pass "all three shadowed built-ins warn (not just the first)"
else
    fail "all three shadowed built-ins warn (got $n of 3)"
fi

# EACH WARNING NAMES ITS OWN DECLARATION. `lines` is on line 2, `chars` on 6,
# `folders` on 10 -- all in wlib.bas, none of them line 1, which is where the
# `load` statement sits in the OTHER file.
for pair in lines:2 chars:6 folders:10; do
    fn=${pair%%:*}; ln=${pair##*:}
    if grep -q "'$fn' from library 'wlib'.*wlib\.bas:$ln:" "$scratch/w/err"; then
        pass "the warning for '$fn' names wlib.bas:$ln, its own declaration"
    else
        got=$(grep -o "wlib\.bas:[0-9]*:[0-9]*" <<<"$(grep "'$fn' from" "$scratch/w/err")")
        fail "the warning for '$fn' names wlib.bas:$ln (got '${got:-none}')"
    fi
done

# THE MESSAGE NAMES BOTH SIDES OF THE BOUNDARY, which is the whole correction:
# a sentence true of one side and false of the other is worse than silence.
if grep -q "inside 'wlib' unqualified calls use this function" "$scratch/w/err" \
   && grep -q "outside it they use the built-in" "$scratch/w/err"; then
    pass "the message says which side of the library boundary it means"
else
    fail "the message says which side of the library boundary it means"
    grep -m1 "same name as a built-in" "$scratch/w/err"
fi

# AND THE MESSAGE MUST MATCH THE BEHAVIOUR, or it is just a different lie.
# This is the control on the tier above: it asserts what actually happens.
if grep -q "^inside:  2$" "$scratch/w/out"; then
    pass "behaviour: inside the library, its own function wins"
else
    fail "behaviour: inside the library, its own function wins ($(head -1 "$scratch/w/out"))"
fi
if grep -q "^outside: builtin$" "$scratch/w/out"; then
    pass "behaviour: outside it, the built-in wins"
else
    fail "behaviour: outside it, the built-in wins ($(sed -n 2p "$scratch/w/out"))"
fi

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
