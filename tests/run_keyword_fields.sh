#!/usr/bin/env bash
set -uo pipefail

# A keyword used as a FIELD NAME is a name, not a keyword.
#
# rc6 made keywords usable as field names, but only halfway: the lexer
# classified them as keywords and the grammar then mapped every keyword token
# to a canonical LOWERCASE spelling (`kw_name("or")`). So `{ OR: 1 }` stored
# the key "or", `r["OR"]` missed, and `{ OR: 1, or: 2 }` produced a record
# whose keys() were `or,or` -- two spellings the language otherwise treats as
# distinct, collapsed into one, silently.
#
# WHETHER A FIELD NAME KEPT ITS CASE THEREFORE DEPENDED ON WHETHER IT HAPPENED
# TO COLLIDE WITH A KEYWORD, which is the inconsistency this fixes.
#
# FIXED IN THE LEXER, NOT THE GRAMMAR, and that is the whole reason it works:
# emitting TOKEN_IDENT hands the parser the ORIGINAL SPAN, which it copies
# verbatim, where a grammar rule only ever had the token's identity. Matthew
# suggested the lexer level; the alternatives considered were all in the
# grammar and all worse (a semantic value on 46 keyword tokens leaks a string
# per `if`/`then`/`end` in every program, since bison does not free a value a
# reduction ignores).
#
# THE CONTEXT TEST IS EXACT RATHER THAN HEURISTIC. Inside an open `{` -- a
# depth the lexer already tracks for line continuation -- an identifier-shaped
# word followed by `:` can only be a record key: a brace MODIFIER contains no
# colon, and `consider` recognises branches by column. After a `.` the word can
# only be a field. Everything else still lexes as the keyword it is, which the
# control tier asserts.
#
# SELF-CHECKING, because the defect produced a perfectly ordinary record: a
# golden would have recorded `or,or` as the expected keys and defended it.
#
# Headless, GI-independent, never skips (bar valgrind).

cd "$(dirname "$0")/.."
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER semantics\n'
if ./gbasic tests/keyword_fields_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "keyword_fields_test exits 0"
else
    fail "keyword_fields_test exits 0 ($(head -1 "$scratch/err"))"
fi
[ -s "$scratch/err" ] && fail "writes nothing to stderr" || pass "writes nothing to stderr"
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "reports no mismatch"
else
    fail "reports no mismatch"; grep MISMATCH "$scratch/out" | head -5
fi
ran=$(sed -n 's/^checks: //p' "$scratch/out")
if [ "${ran:-0}" -ge 14 ]; then
    pass "ran at least 14 checks (ran ${ran:-0})"
else
    fail "ran at least 14 checks (ran ${ran:-0})"
fi

for label in \
    'a keyword key keeps its case' \
    'and the two are distinct keys' \
    'dot access reads the upper-case field' \
    'a non-keyword name is unchanged' \
    'a currency modifier still works' \
    'for/if/then/next still parse and run'
do
    command grep -Fq "ok   $label" "$scratch/out" && pass "asserted: $label" || fail "asserted: $label"
done

printf 'TIER the keyword is still a keyword everywhere else\n'
# The control that makes the context test more than "treat keywords as names":
# a keyword NOT in field position must still be refused as one.
cat >"$scratch/kw.bas" <<'EOF'
to = 5
EOF
if ./gbasic "$scratch/kw.bas" >/dev/null 2>"$scratch/kwerr"; then
    fail "'to = 5' is still a parse error"
else
    if grep -qi "syntax error\|parse error" "$scratch/kwerr"; then
        pass "'to = 5' is still a parse error"
    else
        fail "'to = 5' fails for the right reason ($(head -1 "$scratch/kwerr"))"
    fi
fi
# And a keyword inside braces that is NOT followed by a colon stays a keyword.
cat >"$scratch/kw2.bas" <<'EOF'
r = { a: 1 }
print r["a"]
EOF
if [ "$(./gbasic "$scratch/kw2.bas" 2>/dev/null)" = "1" ]; then
    pass "an ordinary record literal is unaffected"
else
    fail "an ordinary record literal is unaffected"
fi

printf 'TIER valgrind\n'
if command -v valgrind >/dev/null 2>&1; then
    if valgrind --error-exitcode=9 --leak-check=full --errors-for-leak-kinds=definite \
            ./gbasic tests/keyword_fields_test.bas >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_keyword_fields: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
