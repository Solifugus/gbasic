#!/usr/bin/env bash
# PLAT-CONT: a line break inside an unclosed (, [ or { continues the statement.
#
# gBASIC had no line continuation in any form, so every long expression had to
# be assembled -- `join([...], " ")` is the shape that showed up in the wild,
# once per `create table`. The mechanism needs no new syntax and no trailing
# marker to forget: the brackets the author already wrote say where the
# statement ends. It is a LEXER change only; the grammar is untouched, which is
# what the equivalence tier below states as a fact rather than a claim.
#
# Six tiers, and the second and third are the load-bearing pair:
#
#   forms        every bracket kind in every position the grammar admits one
#   equivalence  the same program on one line and across many must produce a
#                BYTE-IDENTICAL TOKEN STREAM (positions stripped) and identical
#                stdout. This is the whole claim -- continuation is transparent,
#                the parser and evaluator see exactly the program they saw
#                before -- and it is structural rather than a transcript, so it
#                cannot drift the way a golden of the multi-line form would.
#   terminator   THE CONTROL. Every other tier is satisfied by a lexer that
#                suppressed newlines UNCONDITIONALLY. This one asserts the
#                other half: outside brackets a newline still terminates, the
#                depth returns to zero when a bracket closes, and a bracket in
#                a STRING or a COMMENT is not depth at all -- the two places a
#                character-counting implementation goes wrong and swallows the
#                rest of the file.
#   modes        the two STATEFUL lexer modes -- brace-modifier content, and
#                `consider`, which recognises its branches by COLUMN -- still
#                work. Neither continuation tier writes a modifier or a
#                consider, so nothing above would notice them breaking.
#   refusal      an unclosed bracket at end of file NAMES the opener and its
#                line, because the parser's own report points at the wrong end
#                of the file; one that runs into a following statement is
#                reported there. Both nonzero, both located, neither runs.
#   depth        nesting past the fixed-size opener stack must not write out of
#                bounds -- 200 levels, each on its own line, under valgrind.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

cases=0
fail() { printf 'FAIL %s\n' "$1"; exit 1; }
ok()   { cases=$((cases + 1)); printf 'PASS %s\n' "$1"; }

golden() { # label fixture
    if ! ./gbasic "tests/continuation/$2.bas" >"$scratch/out" 2>"$scratch/err"; then
        fail "$1 (exited nonzero: $(cat "$scratch/err"))"
    fi
    [ -s "$scratch/err" ] && fail "$1 (unexpected stderr: $(cat "$scratch/err"))"
    diff -u "tests/continuation/$2.out" "$scratch/out" >"$scratch/diff" \
        || fail "$1 (output moved)$(printf '\n%s' "$(cat "$scratch/diff")")"
    ok "$1"
}

# ---------------------------------------------------------------- forms
golden "forms (every bracket kind, every position)" forms

# ---------------------------------------------------------- equivalence
# Strip only the leading `L:C` so what is compared is the token TYPE and
# LEXEME sequence. If continuation is transparent these are equal; if the
# lexer dropped or invented a token anywhere, they are not.
strip_positions() { sed 's/^[[:space:]]*[0-9]\{1,\}:[0-9]\{1,\}[[:space:]]*//'; }

./gbasic --tokens tests/continuation/pair_oneline.bas   | strip_positions >"$scratch/tok_one"
./gbasic --tokens tests/continuation/pair_multiline.bas | strip_positions >"$scratch/tok_multi"

# Coverage floor: two EMPTY streams are also identical, and a fixture that
# stopped parsing would produce one.
tokens=$(wc -l <"$scratch/tok_one")
[ "$tokens" -ge 100 ] \
    || fail "equivalence (only $tokens tokens; the fixture is not exercising anything)"

diff -u "$scratch/tok_one" "$scratch/tok_multi" >"$scratch/diff" \
    || fail "equivalence: token stream$(printf '\n%s' "$(cat "$scratch/diff")")"
ok "equivalence: token stream identical across $tokens tokens"

./gbasic tests/continuation/pair_oneline.bas   >"$scratch/out_one"   2>"$scratch/err_one"
./gbasic tests/continuation/pair_multiline.bas >"$scratch/out_multi" 2>"$scratch/err_multi"
[ -s "$scratch/err_one" ]   && fail "equivalence (one-line form wrote to stderr)"
[ -s "$scratch/err_multi" ] && fail "equivalence (multi-line form wrote to stderr)"
[ -s "$scratch/out_one" ]   || fail "equivalence (the one-line form printed nothing)"
diff -u "$scratch/out_one" "$scratch/out_multi" >"$scratch/diff" \
    || fail "equivalence: stdout$(printf '\n%s' "$(cat "$scratch/diff")")"
ok "equivalence: stdout identical"

# ------------------------------------------------------------ terminator
golden "terminator (the control: newlines outside brackets still end statements)" terminator

# ----------------------------------------------------------------- modes
if ! GBASIC_PATH=stdlib ./gbasic tests/continuation/modes.bas >"$scratch/out" 2>"$scratch/err"; then
    fail "modes (exited nonzero: $(cat "$scratch/err"))"
fi
[ -s "$scratch/err" ] && fail "modes (unexpected stderr: $(cat "$scratch/err"))"
diff -u tests/continuation/modes.out "$scratch/out" >"$scratch/diff" \
    || fail "modes (output moved)$(printf '\n%s' "$(cat "$scratch/diff")")"
ok "modes (brace-modifier content and consider columns undisturbed)"

# --------------------------------------------------------------- refusal
refused() { # label fixture expect-fragment
    if ./gbasic "tests/continuation/$2.bas" >"$scratch/out" 2>"$scratch/err"; then
        fail "$1 (expected a NONZERO exit)"
    fi
    grep -qF "$3" "$scratch/err" \
        || fail "$1 (missing: $3; got: $(cat "$scratch/err"))"
    grep -qE '^[a-z ]+ error at .+:[0-9]+:[0-9]+: ' "$scratch/err" \
        || fail "$1 (unlocated: $(cat "$scratch/err"))"
    [ -s "$scratch/out" ] \
        && fail "$1 (a refused parse still executed: $(cat "$scratch/out"))"
    ok "$1"
}

# The opener is named WITH ITS LINE. Without that the report lands at end of
# file, which is the wrong end: the mistake is where the bracket opened.
refused "refusal: unclosed at end of file names the opener" \
        unclosed_eof "unclosed '[' opened on line 3"
refused "refusal: unclosed running into a statement is reported there" \
        unclosed_midfile "unexpected PRINT, expecting RPAREN"

# ----------------------------------------------------------------- depth
# 200 openers, each on its own line, past the 64-entry opener stack. The stack
# is fixed-size by design (nesting that deep is pathological), so what matters
# is that going past it does not write out of bounds.
{
    printf 'x = '
    for _ in $(seq 200); do printf '(\n'; done
    printf '1\n'
    for _ in $(seq 200); do printf ')\n'; done
    printf 'print string(x)\n'
} >"$scratch/deep.bas"

./gbasic "$scratch/deep.bas" >"$scratch/out" 2>"$scratch/err" \
    || fail "depth (200 levels failed: $(cat "$scratch/err"))"
[ "$(cat "$scratch/out")" = "1" ] \
    || fail "depth (expected 1, got: $(cat "$scratch/out"))"
ok "depth: 200 nested levels across 401 lines"

# ... and unclosed BEYOND the stack, which is the branch that clamps the index
{ printf 'y = '; for _ in $(seq 200); do printf '(\n'; done; } >"$scratch/deep_open.bas"
if ./gbasic "$scratch/deep_open.bas" >"$scratch/out" 2>"$scratch/err"; then
    fail "depth-unclosed (expected a NONZERO exit)"
fi
grep -qF "unclosed '('" "$scratch/err" \
    || fail "depth-unclosed (missing message; got: $(cat "$scratch/err"))"
ok "depth: unclosed past the opener stack still names a bracket"

if command -v valgrind >/dev/null 2>&1; then
    valgrind -q --error-exitcode=9 --leak-check=no \
        ./gbasic "$scratch/deep.bas" >/dev/null 2>"$scratch/vg" \
        || fail "valgrind (deep nesting)$(printf '\n%s' "$(cat "$scratch/vg")")"
    valgrind -q --error-exitcode=9 --leak-check=no \
        ./gbasic "$scratch/deep_open.bas" >/dev/null 2>"$scratch/vg" || true
    grep -q "Invalid" "$scratch/vg" \
        && fail "valgrind (unclosed past the stack)$(printf '\n%s' "$(cat "$scratch/vg")")"
    valgrind -q --error-exitcode=9 --leak-check=no \
        ./gbasic tests/continuation/forms.bas >/dev/null 2>"$scratch/vg" \
        || fail "valgrind (forms)$(printf '\n%s' "$(cat "$scratch/vg")")"
    ok "valgrind: no invalid access across the opener stack"
else
    printf 'SKIP valgrind (not installed)\n'
fi

printf '\n%d checks passed\n' "$cases"
