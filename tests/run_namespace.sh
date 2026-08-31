#!/usr/bin/env bash
set -uo pipefail

# `library_collisions()` and `password_hash_cost()` -- the two audit builtins
# asked for by the gdash session (GDASH-4 step 0, 2026-08-30).
#
# WHY library_collisions EXISTS. gBASIC already warns when a library function
# overrides another, but that warning lives in the UNQUALIFIED-LOOKUP path, so
# it is CALL-TRIGGERED: two libraries sharing a name stay silent for as long as
# every call is qualified, and the warning fires only when a bare name has to
# choose. gdash built a namespace sweep whose entire assertion was "the
# interpreter said nothing", and hit exactly this -- `persist` and its own
# `gdash_paths` both define `ensure_dir`, silently. This builtin reports the
# LATENT state instead, before anyone writes the call that makes it live.
#
# IT IS NOT AN ERROR CONDITION and a caller must not assert it is empty:
# stdlib itself has seven benign shared names (`select` in dates and frame,
# `merge` in consolidate and dates, and so on), all harmless while callers
# qualify them. The useful shape is an allowlist or a baseline comparison, the
# way this repo's own documentation gates handle their deliberate exceptions.
#
# THE CONTROL TIER is what keeps the fixture honest: a builtin that returned an
# EMPTY array always would be indistinguishable from a clean namespace -- which
# is the false reassurance the whole thing exists to remove -- and one that
# returned EVERY imported name would pass the positive checks. So the fixture
# plants a collision and asserts it is found, and asserts that names unique to
# one library are NOT reported.
#
# password_hash_cost is MEASURED rather than declared: it performs one real
# hash and times it, because the parameters alone do not say what they cost on
# the machine in front of you, which is the number an operator needs. It
# therefore takes as long as a hash and is a diagnostic, not a per-request call.
#
# Headless, GI-independent. The password tier needs libxcrypt in the build.

cd "$(dirname "$0")/.."
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER semantics\n'
if ( cd tests && GBASIC_PATH=. ../gbasic namespace_test.bas ) >"$scratch/out" 2>"$scratch/err"; then
    pass "namespace_test exits 0"
else
    if grep -q "not available in this build" "$scratch/err"; then
        echo "SKIP run_namespace (no libxcrypt in this build)"
        exit 0
    fi
    fail "namespace_test exits 0 ($(head -1 "$scratch/err"))"
fi
[ -s "$scratch/err" ] && fail "namespace_test writes nothing to stderr" \
                      || pass "namespace_test writes nothing to stderr"
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "namespace_test reports no mismatch"
else
    fail "namespace_test reports no mismatch"; grep MISMATCH "$scratch/out" | head -5
fi
ran=$(sed -n 's/^checks: //p' "$scratch/out")
if [ "${ran:-0}" -ge 16 ]; then
    pass "namespace_test ran at least 16 checks (ran ${ran:-0})"
else
    fail "namespace_test ran at least 16 checks (ran ${ran:-0})"
fi

for label in \
    'exactly one collision between these two libraries' \
    'a name unique to alpha is not reported' \
    'reported without any unqualified call' \
    'the prefix is stable across calls'
do
    if command grep -Fq "ok   $label" "$scratch/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER against real stdlib\n'
# The documented example: `select` is defined by both dates and frame, and no
# runtime warning would ever mention it unless someone wrote a bare `select`.
cat >"$scratch/real.bas" <<'EOF'
load dates
load frame
for each c in library_collisions()
    print c.name
next
EOF
if GBASIC_PATH=stdlib ./gbasic "$scratch/real.bas" 2>/dev/null | grep -qx "select"; then
    pass "stdlib's dates/frame 'select' collision is reported"
else
    fail "stdlib's dates/frame 'select' collision is reported"
fi
# And a single library has none -- so the builtin is not simply listing names.
cat >"$scratch/one.bas" <<'EOF'
load frame
print count(library_collisions())
EOF
if [ "$(GBASIC_PATH=stdlib ./gbasic "$scratch/one.bas" 2>/dev/null)" = "0" ]; then
    pass "one library alone reports no collision"
else
    fail "one library alone reports no collision"
fi

printf 'TIER valgrind\n'
if command -v valgrind >/dev/null 2>&1; then
    if ( cd tests && GBASIC_PATH=. valgrind --error-exitcode=9 --leak-check=full \
            --errors-for-leak-kinds=definite ../gbasic namespace_test.bas ) \
            >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_namespace: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
