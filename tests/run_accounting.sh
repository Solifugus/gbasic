#!/usr/bin/env bash
set -uo pipefail

# Double-entry accounting (stdlib/accounting.bas, docs/accounting_design.md).
#
# THE SUITE IS SELF-CHECKING RATHER THAN GOLDEN, and for this library that is
# not a stylistic preference. Every failure mode double-entry exists to prevent
# leaves a BALANCED ledger and a plausible statement: right amounts posted to
# the wrong side, a closing entry run twice, an account that is not in the
# chart. A golden records whatever the library produced and then defends it,
# and none of those defects would move a single line of it in a way a reader
# would question.
#
# So the fixture states its own expected answers -- figures computed by hand
# from the transactions -- and the load-bearing assertion is ARITHMETIC: the
# accounting equation, assets = liabilities + equity + earnings, must hold
# after every posting and again after closing. That is a statement about what
# must be TRUE, not about what the library happened to say.
#
# THE SIGN TIER exists because a normal-side error is invisible to the trial
# balance: debits still equal credits when both are on the wrong side. One
# assertion per account kind, so no quadrant is untested.
#
# THE CONTROL TIER matters as much as the refusals. Seven refusals with no
# control are satisfied by a library that refuses everything, so each sits
# beside its nearest legal neighbour -- a balanced entry is accepted, and an
# entry dated AFTER a close still posts.
#
# Headless, GI-independent, never skips (bar valgrind): accounting is pure
# gBASIC over core money.

cd "$(dirname "$0")/.."
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0
failures=0
pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER semantics\n'
if GBASIC_PATH=stdlib ./gbasic tests/accounting_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "accounting_test exits 0"
else
    fail "accounting_test exits 0 ($(head -1 "$scratch/err"))"
fi
if [ -s "$scratch/err" ]; then
    fail "accounting_test writes nothing to stderr ($(head -1 "$scratch/err"))"
else
    pass "accounting_test writes nothing to stderr"
fi
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "accounting_test reports no mismatch"
else
    fail "accounting_test reports no mismatch"
    grep MISMATCH "$scratch/out" | head -5
fi
ran=$(sed -n 's/^checks: //p' "$scratch/out")
if [ "${ran:-0}" -ge 38 ]; then
    pass "accounting_test ran at least 38 checks (ran ${ran:-0})"
else
    fail "accounting_test ran at least 38 checks (ran ${ran:-0})"
fi

# Name the load-bearing ones individually, so a fixture that stopped running
# them would fail here rather than passing with a smaller count.
printf 'TIER the assertions that carry the design\n'
for label in \
    'the accounting equation holds' \
    'and it still balances' \
    'asset is debit-normal' \
    'revenue is credit-normal' \
    'closing zeroes revenue' \
    'net income landed in equity' \
    'an unbalanced entry is refused' \
    'closing an already-closed period is refused' \
    'a balanced entry is accepted' \
    'a later entry still posts after a close'
do
    if command grep -Fq "ok   $label" "$scratch/out"; then
        pass "asserted: $label"
    else
        fail "asserted: $label"
    fi
done

printf 'TIER valgrind\n'
if command -v valgrind >/dev/null 2>&1; then
    if GBASIC_PATH=stdlib valgrind --error-exitcode=9 --leak-check=full \
            --errors-for-leak-kinds=definite \
            ./gbasic tests/accounting_test.bas >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_accounting: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
