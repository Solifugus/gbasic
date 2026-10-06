#!/usr/bin/env bash
# `{file}path` -- the assignment clause, INLINE, wherever an expression may go.
#
# WHY IT EXISTS. A modifier could only be applied by an ASSIGNMENT, so a value
# wanted once had to be given a name and a line to be given it on. MEASURED
# 2026-09-22 across stdlib, examples and tests: 1,034 one-word assignment
# clauses, and 510 of them bind a name that is read EXACTLY ONCE afterwards --
# a value named only so that it could be passed. The clause form is unchanged
# and stays the right spelling when the value is being STORED; what this adds
# is the case where it is being USED.
#
# WHY IT LIVES IN THE LEXER, WHICH IS THE WHOLE REASON IT WORKS. `{a}` and
# `{a: 1}` differ at their THIRD token, and LALR(1) cannot see that far from
# the brace: adding `LBRACE IDENT RBRACE unary_expression` to the grammar costs
# ONE shift/reduce conflict against a project standard of zero, and the fuller
# rule that would also admit `{split ","}` inline costs NINETEEN. Both measured
# on 2026-09-22, and both refused -- this project rejected `IDENT expression`
# as a statement form over FOUR. The lexer may look as far ahead as it likes,
# so recognising the exact shape `{ IDENT }` there costs none. Same technique,
# and the same argument, as the keyword-field fix.
#
# THE PRICE IS STATED RATHER THAN HIDDEN: the inline form takes a ONE-WORD name
# (a library qualifier counts as one word). `{end of month}` and `{split ","}`
# still need the assignment clause, and the CONTROL tier asserts they do.
#
# Tiers:
#   SEMANTICS   the self-checking fixture. THE LOAD-BEARING TIER IS PARITY:
#               ten modifier kinds, each asserted to give the SAME value inline
#               as through the clause, because the two must not be able to
#               disagree about what a modifier MEANS. A tier saying only that
#               the inline form produced something passes on one that produced
#               anything. Plus the two CONTROL blocks -- the clause shapes the
#               lexer does NOT claim, and record literals -- without which the
#               change is satisfied by a lexer that ate every brace.
#   GRAMMAR     bison must still report ZERO conflicts. Not decoration: it is
#               the entire justification for putting this in the lexer, and a
#               later edit that moved it into the grammar would pass every
#               behavioural check in this file.
#   TOKEN       the shape is recognised at token delivery, asserted through
#               --tokens: `{date}` is ONE token carrying the NAME. This is what
#               the grammar tier cannot see -- a build that reached the same
#               answers through LBRACE would be indistinguishable otherwise.
#   REFUSAL     an unknown modifier is refused BY NAME and LOCATED at the name;
#               and when the SUBJECT fails, the subject's error is what is
#               reported -- the reports-the-wrong-cause class this tree has
#               produced repeatedly.
#   VALGRIND    a new expression node owning a heap name per parse.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null
export GBASIC_PATH=stdlib

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER semantics: parity, precedence, positions, and the two controls\n'
if ! timeout -k 5 60 ./gbasic tests/inline_modifier_test.bas >"$work/out" 2>"$work/err"; then
    cat "$work/err"; fail "the fixture did not run"
elif grep -q MISMATCH "$work/out"; then
    grep MISMATCH "$work/out"; fail "the fixture disagreed with itself"
elif ! grep -qx 'mismatches: 0' "$work/out"; then
    fail "the fixture did not finish"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    # A coverage floor: a fixture that stops running its checks otherwise
    # passes by asserting nothing.
    if [ -z "$n" ] || [ "$n" -lt 34 ]; then
        fail "only ${n:-0} checks ran, wanted at least 34"
    else
        pass "$n checks (10 parity pairs, precedence, 8 positions, 6 nesting, 8 controls)"
    fi
fi

printf 'TIER grammar: still zero conflicts\n'
if command -v bison >/dev/null 2>&1; then
    c=$(bison -d -o "$work/p.tab.c" src/parser.y 2>&1 | grep -c conflict || true)
    if [ "$c" -eq 0 ]; then
        pass "bison reports no conflicts"
    else
        bison -d -o "$work/p.tab.c" src/parser.y 2>&1 | grep conflict
        fail "the inline modifier introduced $c grammar conflict line(s)"
    fi
else
    pass "SKIP (bison unavailable)"
fi

printf 'TIER token: the shape is decided by the LEXER\n'
# The tier the grammar check cannot stand in for. If this ever becomes three
# tokens again, the feature is being carried by the grammar and the conflict
# count is the thing to re-measure.
printf 'x = {date}"2026-01-01"\n' > "$work/tok.bas"
./gbasic --tokens "$work/tok.bas" > "$work/tok.out" 2>&1 || true
if grep -q 'MODIFIER_PREFIX' "$work/tok.out"; then
    pass "\`{date}\` arrives as one MODIFIER_PREFIX token"
else
    head -8 "$work/tok.out"; fail "no MODIFIER_PREFIX token -- the lexer is not recognising the shape"
fi
if grep -E 'MODIFIER_PREFIX' "$work/tok.out" | grep -q 'date'; then
    pass "and it carries the NAME, not the braces"
else
    grep MODIFIER_PREFIX "$work/tok.out"; fail "the token does not carry the modifier name"
fi
# CONTROL: a record literal must still produce LBRACE. Without this, "it is one
# token" is satisfied by a lexer that folds every brace it meets.
printf 'r = { a: 1 }\n' > "$work/rec.bas"
./gbasic --tokens "$work/rec.bas" > "$work/rec.out" 2>&1 || true
if grep -q 'LBRACE' "$work/rec.out" && ! grep -q 'MODIFIER_PREFIX' "$work/rec.out"; then
    pass "CONTROL: a record literal still lexes as LBRACE"
else
    head -8 "$work/rec.out"; fail "a record literal was claimed by the modifier shape"
fi

printf 'TIER refusal\n'
printf 'print {nosuchmodifier}"hi"\n' > "$work/bad.bas"
if ./gbasic "$work/bad.bas" >/dev/null 2>"$work/bad.err"; then
    fail "an unknown inline modifier was accepted"
elif ! grep -q 'assign modifier not found: nosuchmodifier' "$work/bad.err"; then
    cat "$work/bad.err"; fail "refused, but without naming the modifier"
elif ! grep -qE ':1:7:' "$work/bad.err"; then
    cat "$work/bad.err"; fail "refused, but not located at the modifier name"
else
    pass "an unknown modifier is refused by name, at the name"
fi
# The reports-the-wrong-cause class. When the SUBJECT raises, that is the
# error the author has to see -- not a second one about a modifier that was
# never reached.
printf 'print {USD}undefined_name\n' > "$work/sub.bas"
if ./gbasic "$work/sub.bas" >/dev/null 2>"$work/sub.err"; then
    fail "a failing subject was accepted"
elif grep -q 'undefined' "$work/sub.err" && ! grep -q 'modifier' "$work/sub.err"; then
    pass "a failing subject reports the SUBJECT's cause, not the modifier's"
else
    cat "$work/sub.err"; fail "the subject's failure was reported as a modifier problem"
fi
# CONTROL: the clause form refuses the same unknown name the same way, or
# "it refuses" would be a fact about the inline path alone.
printf 'x {nosuchmodifier}= "hi"\n' > "$work/badc.bas"
if ./gbasic "$work/badc.bas" >/dev/null 2>"$work/badc.err" ; then
    fail "the clause form accepted an unknown modifier"
elif grep -q 'assign modifier not found: nosuchmodifier' "$work/badc.err"; then
    pass "CONTROL: the clause form refuses it identically"
else
    cat "$work/badc.err"; fail "the two forms refuse differently"
fi

printf 'TIER record or modifier: the follow-set is DERIVED, not surveyed\n'
# THE DECISION IS ONE TOKEN PAST THE LEADING IDENTIFIER. A record literal always
# has `:`, `=` or `(` there and a modifier never does, which is what lets the
# lexer admit `{split ","}` and `{end of month}` inline at zero grammar cost.
#
# ASKED OF THE GRAMMAR RATHER THAN OF ME, because the first design was a scan to
# the closing brace for a top-level separator -- and the safety argument for that
# is "I enumerated the record forms", a survey, where the one-word rule's
# argument is a proof. A survey cannot see a record form nobody has written yet.
# So this reads `record_field_list` and fails if a fourth token joins the three,
# which is the moment somebody has to revisit src/lexer.c.
# `|| true` IS LOAD-BEARING, and the perturbation is what showed it: under
# `pipefail` a `grep` that matches nothing fails the whole assignment and
# `set -e` ends the suite SILENTLY -- exit 1 with the TIER line printed and no
# FAIL after it, which is a break nobody could diagnose. The same trap
# run_http.sh records from the other direction.
follow="$(awk '/^record_field_list/,/^    ;/' src/parser.y \
    | grep -oE '(field_name|IDENT) [A-Z_]+' | awk '{print $2}' | sort -u | tr '\n' ' ' || true)"
if [ "$follow" = "COLON LPAREN OP_EQ " ]; then
    pass "a record's first field name is followed by exactly COLON, OP_EQ or LPAREN"
else
    printf '    got: [%s]\n' "$follow"
    fail "the record follow-set changed -- src/lexer.c decides record-vs-modifier on it"
fi
# And the behavioural half, both directions, because a derived list proves
# nothing about what the lexer does with it.
inline_shape() {   # <source line> <expected stdout> <label>
    printf 'load dates\n%s\n' "$1" >"$work/shape.bas"
    got="$(GBASIC_PATH=stdlib timeout -k 5 20 ./gbasic "$work/shape.bas" 2>"$work/shape.err")"
    if [ "$got" = "$2" ]; then
        pass "$3"
    else
        printf '    got [%s], want [%s] %s\n' "$got" "$2" "$(tail -1 "$work/shape.err")"
        fail "$3"
    fi
}
inline_shape 'print(count({split ","}"a,b,c"))' '3' 'an argument-bearing modifier applies inline'
inline_shape 'print(string({end of month}({date}"2026-02-10")))' '2026-02-28' 'and a multi-word one'
inline_shape 'print({trimmed; upper}"  hi  ")' 'HI' 'and a chain'
inline_shape 'print(type({ a: 1 }))' 'record' 'CONTROL: a COLON record is still a record'
inline_shape 'print(type({ a = 1 }))' 'record' 'CONTROL: so is an OP_EQ record'
inline_shape 'print(type({ serial (reset 7): 0 }))' 'record' 'CONTROL: so is an LPAREN policy record'
inline_shape 'print(type({ "k": 1 }))' 'record' 'CONTROL: so is one keyed by a STRING'
inline_shape 'print(type({}))' 'record' 'CONTROL: so is the empty one'
# A RECORD LITERAL SPANS LINES ROUTINELY and a modifier clause cannot, so the
# scan bails at a newline -- which is what makes a multi-line record unreachable
# by the modifier path rather than merely unlikely.
printf 'load dates\nr = {\n  a: 1,\n  b: 2\n}\nprint(type(r) + ":" + string(r.b))\n' >"$work/ml.bas"
if [ "$(GBASIC_PATH=stdlib ./gbasic "$work/ml.bas" 2>/dev/null)" = "record:2" ]; then
    pass "CONTROL: a record across lines is untouched"
else
    fail "CONTROL: a record across lines is untouched"
fi

printf 'TIER the clause takes ONE modifier, and why neither spelling can mean more\n'
# ASKED RATHER THAN DESIGNED: `{trimmed,upper}=` was tried 2026-10-02 and the
# answer is no -- but the two obvious spellings fail in two DIFFERENT ways, and
# each one is a fact about what that syntax already means rather than a gap.
#
# A COMMA BECOMES PART OF THE NAME, so the lookup fails on a name nobody
# registered. A SPACE is how a modifier name CONTINUES (`{end of month}`) or
# takes an ARGUMENT (`{split ","}`), resolved by longest match against the
# registered names -- so `trimmed` matched as the name and `upper` was read as
# an argument to it. Both spellings are claimed; that is the whole reason
# neither is available for composition.
#
# PINNED BECAUSE THE INLINE FORM DOES COMPOSE (asserted in the fixture), which
# makes "can I write two in a clause" a question somebody will ask again -- and
# the answer is more useful as two located diagnostics than as a sentence.
clause_refusal() {   # <source line> <needle>
    printf 's = "  hello  "\n%s\nprint(x)\n' "$1" >"$work/clause.bas"
    out="$(timeout -k 5 20 ./gbasic "$work/clause.bas" 2>&1 || true)"
    case "$out" in
        *"$2"*) pass "$3" ;;
        *) printf '    got: %s\n' "$(printf '%s' "$out" | tail -1)"; fail "$3" ;;
    esac
}
clause_refusal 'x {trimmed,upper}= s' 'assign modifier not found: trimmed,upper' \
    'a comma is part of the NAME, so the lookup fails on it'
clause_refusal 'x {trimmed upper}= s' 'expects no arguments' \
    'a space makes the second word an ARGUMENT, not a second modifier'
clause_refusal 'x {trimmed}{upper}= s' 'syntax error' \
    'two clauses in a row is a parse error'
# THE CONTROLS, or "the clause refuses" is satisfied by a clause that refuses
# everything -- and these three are exactly what the two spellings above are
# claimed BY, so without them the refusals above read as gaps rather than as
# consequences.
#
# A SEPARATE HELPER THAT REQUIRES SUCCESS AND AN ANSWER, which is a correction
# made before this shipped: the first draft ran them through `clause_refusal`
# with an EMPTY needle, and `case "$out" in *""*)` matches anything -- so all
# three passed while asserting nothing at all, including on a build where the
# clause had stopped working. The same vacuous-control shape this suite's own
# neighbours keep producing.
clause_works() {   # <source line> <expected stdout> <label>
    printf 's = "  hello  "\n%s\nprint(string(x))\n' "$1" >"$work/clause.bas"
    got="$(timeout -k 5 20 ./gbasic "$work/clause.bas" 2>"$work/clause.err")"
    rc=$?
    if [ "$rc" != 0 ]; then
        printf '    exit %s: %s\n' "$rc" "$(tail -1 "$work/clause.err")"; fail "$3"
    elif [ "$got" != "$2" ]; then
        printf '    got [%s], want [%s]\n' "$got" "$2"; fail "$3"
    else
        pass "$3"
    fi
}
clause_works 'x {trimmed}= s' 'hello' 'CONTROL: one modifier in a clause still works'
clause_works 'x {split "e"}= s' '["  h","llo  "]' 'CONTROL: a name plus an argument still works'
clause_works 'x = {upper}{trimmed}s' 'HELLO' 'CONTROL: the inline form still nests'

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/inline_modifier_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        tail -30 "$work/vg.err"; fail "valgrind objected"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

exit $status
