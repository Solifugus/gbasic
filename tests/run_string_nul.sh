#!/usr/bin/env bash
set -uo pipefail

# PLAT-NUL -- a gBASIC string is a counted sequence of bytes, and NUL is
# content. docs/reference.md has promised exactly that since the string type
# was written:
#
#   "A gBASIC string is a binary-safe sequence of bytes ... any byte --
#    including NUL (chr(0)) -- is valid content; strings are not
#    NUL-terminated from the program's point of view."
#
# It was not true, and WHICH HALF WAS TRUE WAS UNGUESSABLE. `len`, `mid`,
# `contains`, `find`, `byte_at` and `hex_encode` read the string header's
# length; `replace`, `trim`, `join`, `split`, `repeat`, `starts_with` and
# `ends_with` read a C string and stopped at the first NUL. `contains` and
# `ends_with` are in the same family and disagreed about what the string was.
#
# Reported by the gdash session (docs/gdash5_platform_report_string_nul.md)
# from LDAP filter and DN escaping -- RFC 4515 and RFC 4514 both REQUIRE NUL
# to be escaped, so the one byte the code was most obliged to handle was the
# one the platform ate. Their sweep found five; running the whole family
# rather than the reported ones found three more, and one CONTRADICTED the
# report: `starts_with` was marked safe and is not.
#
# THE FAILURE MODE IS WHY THIS IS SELF-CHECKING RATHER THAN GOLDEN. Nothing
# raised. `replace` returned a shorter plausible string and `ends_with`
# returned a wrong boolean, so a golden would have recorded `1` as the length
# of a three-byte string and defended it.
#
# The needle half is the sharper one and it is the half the report missed:
# a needle read as a C string compares only its head, and a needle that BEGINS
# with NUL compares nothing at all and matches everything -- so
# `starts_with(anything, chr(0))` was TRUE. A predicate used to validate input
# answering true for input that does not match is the worst direction
# available.

cd "$(dirname "$0")/.."
. tests/valgrind_tier.sh

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

# TIMEOUT ON THE FIXTURE, not decoration. Removing the empty-needle refusal
# does not make `replace` answer wrongly -- it makes it search for a
# zero-length needle, find one at every position and never advance, so the
# fixture HANGS. A hang is not a failure: without this bound the suite sits
# there until run_all's 1800s cap, reporting nothing. Found by removing the
# refusal and watching the red-proof produce no output at all. `-k` is there
# because SIGTERM alone is not obviously enough for this interpreter -- it
# installs a handler for pool drain -- and a bound that might not fire is not a
# bound. 60s against a fixture that runs in about one second.
printf 'TIER semantics\n'
if GBASIC_PATH=stdlib timeout -k 5 60 ./gbasic tests/string_nul_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "string_nul_test exits 0"
else
    fail "string_nul_test exits 0 ($(head -1 "$scratch/err"); a timeout here means it hung)"
fi
if grep -q '^mismatches: 0$' "$scratch/out"; then
    pass "no mismatches"
else
    fail "no mismatches"
    grep '^MISMATCH' "$scratch/out" | head -10
fi
n=$(sed -n 's/^checks: //p' "$scratch/out")
if [ -n "$n" ] && [ "$n" -ge 66 ]; then
    pass "check count floor ($n checks)"
else
    fail "check count floor (got '${n:-none}', want >= 66)"
fi

# The named tiers must have run. A floor alone is satisfied by forty checks of
# anything, and the two that matter are the ones nobody reported.
printf 'TIER the load-bearing tiers ran\n'
for needle in \
    "starts_with with a NUL needle is not a wildcard" \
    "a needle's bytes past its NUL still count" \
    "an EMPTY needle is still refused" \
    "replace can rewrite a NUL" \
    "  and trim leaves it alone" \
    "trim leaves multibyte content alone" \
    "a VALID needle never matches mid-codepoint" \
    "  and split separates on it"
do
    if grep -qF "ok   $needle" "$scratch/out"; then
        pass "ran: $needle"
    else
        fail "ran: $needle"
    fi
done

# THE CROSS-FAMILY TIER, which is the whole point of the defect rather than of
# any one function. Two builtins in the same family must not disagree about
# what the string is -- that disagreement is what made binary-safety a fact a
# caller discovered one probe at a time.
printf 'TIER the family agrees with itself\n'
cat >"$scratch/family.bas" <<'EOF'
s = "a" + chr(0) + "b"
' Every one of these asks the same question about the same bytes.
print string(contains(s, "b")) + " " + string(ends_with(s, "b")) + " " + string(find(s, "b") = 2)
print string(contains(s, "a")) + " " + string(starts_with(s, "a")) + " " + string(find(s, "a") = 0)
print string(len(s)) + " " + string(byte_count(s)) + " " + string(len(mid(s, 0, 3))) + " " + string(len(trim(s))) + " " + string(len(replace(s, "z", "y")))
EOF
if GBASIC_PATH=stdlib ./gbasic "$scratch/family.bas" 2>/dev/null >"$scratch/fam"; then
    if [ "$(sed -n 1p "$scratch/fam")" = "true true true" ] \
       && [ "$(sed -n 2p "$scratch/fam")" = "true true true" ] \
       && [ "$(sed -n 3p "$scratch/fam")" = "3 3 3 3 3" ]; then
        pass "contains, ends_with, find, starts_with, len, mid, trim and replace all agree"
    else
        fail "contains, ends_with, find, starts_with, len, mid, trim and replace all agree"
        cat "$scratch/fam"
    fi
else
    fail "the family probe runs"
fi

# The empty-needle refusal is load-bearing for TERMINATION, not merely for
# sanity: with it removed, `replace` searches for a zero-length needle, finds
# one at every position and never advances -- it hangs rather than answering
# wrongly. Found by removing it, which is the only way that would have been
# found. The timeout is what turns that into a failing test rather than a
# stuck suite.
printf 'TIER an empty needle terminates\n'
cat >"$scratch/empty.bas" <<'EOF'
on error goto next
x = replace("abc", "", "X")
print error.message
error.clear()
on error stop
EOF
if out=$(GBASIC_PATH=stdlib timeout 10 ./gbasic "$scratch/empty.bas" 2>&1); then
    if printf '%s' "$out" | grep -q 'cannot be empty'; then
        pass "an empty needle raises rather than looping forever"
    else
        fail "an empty needle raises rather than looping forever (got: $out)"
    fi
else
    fail "an empty needle raises rather than looping forever (timed out or crashed)"
fi

# The modifier spellings share the same helpers as the calls, so a fix applied
# to one form and not the other would pass everything above.
printf 'TIER the modifier forms share the fix\n'
cat >"$scratch/mods.bas" <<'EOF'
s = " a" + chr(0) + "b "
t{trimmed}= s
p{split ","}= "a" + chr(0) + "b,c"
j{join "-"}= ["a" + chr(0) + "b", "c"]
print string(len(t)) + " " + string(len(first(p))) + " " + string(len(j))
EOF
if GBASIC_PATH=stdlib ./gbasic "$scratch/mods.bas" 2>/dev/null >"$scratch/mod"; then
    if [ "$(cat "$scratch/mod")" = "3 3 5" ]; then
        pass "trim, split and join modifiers are length-aware too"
    else
        fail "trim, split and join modifiers are length-aware too (got $(cat "$scratch/mod"))"
    fi
else
    fail "the modifier probe runs ($(GBASIC_PATH=stdlib ./gbasic "$scratch/mods.bas" 2>&1 >/dev/null | head -1))"
fi

printf 'TIER valgrind\n'
if vg_available; then
    cat >"$scratch/vg.bas" <<'EOF'
s = "a" + chr(0) + "b"
for i = 1 to 50
    x = replace(s, "b", "BB")
    y = trim(" " + s + " ")
    z = join([s, x, y], chr(0))
    w = split(z, chr(0))
    v = repeat(s, 3)
    u = starts_with(z, s) and ends_with(z, y)
next
print len(z)
EOF
    if GBASIC_PATH=stdlib vg_run ./gbasic "$scratch/vg.bas" >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_string_nul: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
