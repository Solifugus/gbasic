#!/usr/bin/env bash
# source_comments(text) (Qt design §0.38 B1): every comment run the parser
# consumed, as { start_line, end_line, start_column, start_offset, end_offset,
# text, raw, trailing }, read through the parser's own lexer so a `'` in a
# string or an SQL body is never a comment.
#
# Tiers:
#   1. SEMANTICS -- tests/source_comments_test.bas, by hand: runs and what
#      ends them, trailing comments, markers, strings and SQL bodies, brackets,
#      CRLF, byte columns (and the lexer's modifier column, which counted the
#      `{` twice until this builtin's corpus tier found it), a text that does
#      not parse, refusals. Floor 22 checks, stderr empty.
#   2. CORPUS -- every .bas under stdlib/, examples/ and tests/ (under 400 KB):
#      each comment is exactly the bytes at its line and column; blanking all of
#      them leaves source_outline's answer unchanged (nothing reported was
#      code); and every line whose first non-blank is a quote, outside an SQL
#      body, is reported (nothing leading was missed). Checked by python3.
#   3. VALGRIND, over tier 1.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -u
cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_source_comments: build failed\n'
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
fail() { printf 'FAIL %s\n' "$1"; status=1; }

printf -- '-- semantics\n'
out=$(timeout 60 ./gbasic tests/source_comments_test.bas 2>"$work/err")
n=$(printf '%s\n' "$out" | sed -n 's/^checks: \([0-9]*\)$/\1/p')
if [ "$(printf '%s\n' "$out" | tail -1)" = "mismatches: 0" ] && [ "${n:-0}" -ge 22 ] && [ ! -s "$work/err" ]; then
    printf 'PASS semantics (%s checks)\n' "$n"
else
    printf '%s\n' "$out" | grep -v '^ok ' | head -20
    head -5 "$work/err"
    fail "semantics (floor 22 checks, stderr empty)"
fi

printf -- '-- corpus\n'
if command -v python3 >/dev/null 2>&1; then
    find stdlib examples tests -name '*.bas' -size -400k | sort >"$work/files"
    if timeout 300 ./gbasic tests/source_comments/corpus_emit.bas $(cat "$work/files") >"$work/got.jsonl" 2>"$work/emit.err" &&
       [ ! -s "$work/emit.err" ] && o=$(python3 -I tests/source_comments/check_corpus.py "$work/got.jsonl" 2>&1); then
        printf 'PASS corpus (%s; %s checks)\n' "$(printf '%s\n' "$o" | sed -n 's/^files: //p')" "$(printf '%s\n' "$o" | sed -n 's/^checks: //p')"
    else
        head -5 "$work/emit.err"
        printf '%s\n' "${o:-}" | head -20
        fail "corpus"
    fi
else
    printf 'SKIP corpus (no python3: no file was held to its bytes)\n'
fi

if vg_available; then
    if timeout 600 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/source_comments_test.bas' >/dev/null 2>"$work/vg"; then
        printf 'PASS valgrind tests/source_comments_test.bas\n'
    else
        printf 'FAIL valgrind\n'; head -20 "$work/vg"; status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
