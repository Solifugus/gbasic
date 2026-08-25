#!/usr/bin/env bash
# PLAT-BRACE — modifier clauses in braces (docs/brace_modifier_design.md).
#
# The paren spelling made `name(caseless) = "joe"` and `kind(x) = "record"` the
# same tokens in the same order, so the parser had to GUESS which was a clause
# and which was a call. `modifier_lparen_ahead` was ninety lines of lookahead
# doing that guessing, and its own comment admitted the identifier-argument
# case could not be closed at token delivery.
#
# A brace cannot open a call, so there is nothing to guess. This suite is
# therefore two claims:
#
#   forms     every modifier position works in the new spelling
#   residual  the case that could not be fixed now parses as an ordinary call
#
# The second is the point of the whole change: it was pinned as a permanent
# defect in tests/negative_clause_residual.bas, and it is now a passing case.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fail() { printf 'FAIL %s\n' "$1"; exit 1; }

for name in forms residual; do
    GBASIC_PATH=stdlib ./gbasic "tests/brace_modifiers/$name.bas" \
        >"$scratch/got" 2>"$scratch/err" \
        || fail "$name (exited nonzero: $(cat "$scratch/err"))"
    diff -u "tests/brace_modifiers/$name.out" "$scratch/got" \
        || fail "$name (output diverged)"
    printf 'PASS %s\n' "$name"
done

# --- the retired spelling is refused, and says what replaced it -------------
printf 'program main( args )\n    p(USD) = 19.95\nend program\n' >"$scratch/old.bas"
if ./gbasic "$scratch/old.bas" >/dev/null 2>"$scratch/err"; then
    fail "paren form (must be refused)"
fi
grep -qiE "syntax error|parse error|modifier" "$scratch/err" \
    || fail "paren form (unhelpful refusal: $(cat "$scratch/err"))"
printf 'PASS paren_form_refused\n'

# --- and the guesser is gone from the source, not merely unused ------------
grep -q "modifier_lparen_ahead" src/parser.y \
    && fail "modifier_lparen_ahead still present: the guess is what this change removes"
printf 'PASS guesser_removed\n'

printf 'run_brace_modifiers: 4 cases passed\n'
