#!/usr/bin/env bash
# A reported parse error must FAIL the parse (DOGFOOD ledger item 6).
#
# `yylex` returns 0 — end of file — for a token the grammar has no place for.
# Bison cannot tell that EOF from a real one, so wherever the grammar allows a
# program to end, it ACCEPTS: the file is silently truncated at the bad token,
# whatever preceded it runs, and the process exits 0. `dim x` is the smallest
# instance and the one the ledger recorded; a byte the lexer cannot tokenize at
# all does the same thing, which is what makes this a parser-interface defect
# rather than a `dim` defect.
#
# Four claims, and the third is the one that bites CI:
#
#   located     every diagnostic carries file:line:col — the `dim` path printed
#               a bare line with no location and no sink, so `--json-diagnostics`
#               emitted non-JSON into a JSON stream
#   nonzero     a reported error exits nonzero, wherever the token sits
#   nothing_ran a truncated program does not half-execute
#   one_cause   one diagnostic per cause; no follow-on blaming "end of file",
#               which is not what stopped the parse
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fail() { printf 'FAIL %s\n' "$1"; exit 1; }

# Every line the diagnostic machinery emits is `<kind> error at PATH:L:C: msg`.
# A line that does not match is one that bypassed the sink -- unlocated, absent
# from --json-diagnostics, and invisible to any tool reading the stream.
located_only() { # label errfile
    while IFS= read -r line; do
        [ -z "$line" ] && continue
        printf '%s\n' "$line" | grep -qE '^[a-z ]+ (error|warning) at .+:[0-9]+:[0-9]+: ' \
            || fail "$1 (unlocated line bypassed the diagnostic sink: $line)"
    done <"$2"
}

# Counted, not hardcoded. The summary line used to carry a literal 7 while the
# script ran seven cases, which held exactly until someone added an eighth --
# a gate that reports a number it does not measure is a gate that can shrink
# without saying so.
cases=0
ok() { cases=$((cases + 1)); printf 'PASS %s\n' "$1"; }

refused() { # label file expect-fragment expect-empty-stdout
    if ./gbasic "tests/parse_exit/$2" >"$scratch/out" 2>"$scratch/err"; then
        fail "$1 (expected a NONZERO exit; exiting 0 is what lies to CI)"
    fi
    grep -qF "$3" "$scratch/err" \
        || fail "$1 (missing: $3; got: $(cat "$scratch/err"))"
    located_only "$1" "$scratch/err"
    grep -q "unexpected end of file" "$scratch/err" \
        && fail "$1 (blames end-of-file, which is not what stopped the parse)"
    if [ "$4" = "empty" ] && [ -s "$scratch/out" ]; then
        fail "$1 (a refused parse still executed: $(cat "$scratch/out"))"
    fi
    ok "$1"
}

# `dim` is reserved for exactly one purpose -- saying it is not a statement --
# so the message must actually say what to do instead.
refused dim_toplevel      dim_toplevel.bas      "assign to create a variable" empty
refused dim_in_program    dim_in_program.bas    "assign to create a variable" empty
refused bad_char_toplevel bad_char_toplevel.bas "unexpected token"            empty
# ...and in a `consider` body, which is the OTHER statement position. Until
# 2026-08-27 `dim` was refused by yylex at token delivery, so this case came
# free; now the grammar states the refusal and it has to be stated TWICE, once
# per statement nonterminal. A test per position is what stops the second one
# being dropped.
refused dim_consider      dim_consider.bas      "assign to create a variable" empty

# THE CASE THE OLD REFUSAL GOT WRONG. `dim` is refused where a STATEMENT was
# expected -- that is what its advice is about -- and nowhere else. Every other
# keyword is a legal record field, in a literal and after a dot; `dim` was the
# sole exception, rejected at 1:7 INSIDE a record literal with "not a gBASIC
# statement" at a position where no statement is possible. This is the positive
# half, and it must keep passing or the refusal has crept back out of position.
printf 'TIER dim is an ordinary field name\n'
if out="$(./gbasic tests/parse_exit/dim_as_field.bas 2>"$scratch/err")" \
   && [ "$out" = "$(printf '7\n7\n9\n1')" ] && [ ! -s "$scratch/err" ]; then
    ok dim_as_field
else
    fail "dim_as_field (got '$out'; stderr: $(cat "$scratch/err"))"
fi

# The library path always refused to LOAD; it is the diagnostic that was wrong.
refused dim_library dim_library.bas "could not parse library file" empty

# --- the bare line was not merely ugly: it corrupted a machine stream --------
./gbasic --json-diagnostics tests/parse_exit/dim_toplevel.bas \
    >"$scratch/out" 2>"$scratch/err" && fail "json (expected nonzero exit)"
while IFS= read -r line; do
    [ -z "$line" ] && continue
    case "$line" in
        '{'*'}') ;;
        *) fail "json (non-JSON line in a JSON stream: $line)" ;;
    esac
done <"$scratch/err"
grep -q '"code":"GB_DIAG_PARSE_ERROR"' "$scratch/err" \
    || fail "json (the diagnostic never reached the sink: $(cat "$scratch/err"))"
ok json_diagnostics

# --- the control: rejecting a REPORTED error, not an accepted end of file ----
./gbasic tests/parse_exit/clean.bas >"$scratch/out" 2>"$scratch/err" \
    || fail "clean (a valid top-level program must still run: $(cat "$scratch/err"))"
printf 'before\nafter\n' >"$scratch/want"
diff -u "$scratch/want" "$scratch/out" || fail "clean (output diverged)"
ok clean

# --- no token may reach the unlocated fallback ------------------------------
# The `default:` arm of the token map is what printed the bare line. It stays as
# a backstop for a token added to the lexer and not to the grammar, but it must
# report like everything else, so this asserts the raw fprintf is gone.
grep -n 'fprintf(stderr, "unexpected token' src/parser.y \
    && fail "parser.y still prints a token diagnostic outside the sink"
ok no_raw_stderr

printf 'run_parse_exit: %d cases passed\n' "$cases"
