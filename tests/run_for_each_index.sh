#!/usr/bin/env bash
# `for each item, i in list` -- the element AND its position.
#
# WHY IT EXISTS, and why the semantics did NOT change. gBASIC has no references
# and is not getting them: an actor is fork+exec so a reference cannot cross
# `spawn`, and `encode` totality is what lets an `agent` run sit in a store
# between HTTP requests -- references admit cycles and that property dies.
# The consequence is that a loop body cannot write through the element
# variable: `item.x = 1` mutates a COPY and is silently discarded.
#
# MEASURED BEFORE BUILDING ANYTHING: that mistake appears EXACTLY ONCE across
# stdlib, examples and tests, in examples/array_cow_test.bas, which is the
# fixture that asserts the semantics. So the fix is not to change the model but
# to make the CORRECT idiom cheap -- the index is what makes the write
# expressible, since `list[i] = item` is an lvalue PATH and paths write in
# place.
#
# Tiers:
#   SEMANTICS  the self-checking fixture. THE LOAD-BEARING TIER IS THE
#              WRITE-BACK, asserted as a DIFFERENCE against its control: the
#              same loop WITHOUT `list[i] = item` must change nothing.
#              "The array changed" alone passes on a language with references;
#              "the array did not change" alone passes on one where the loop
#              body does nothing. Both halves are required.
#   SNAPSHOT   writing to the array while walking it is safe and terminating --
#              the walk sees the original values, and appending inside the loop
#              does not extend it. That is what makes a single-pass rewrite
#              expressible at all, and it is stronger than Python (which skips
#              elements) or JavaScript (which can loop forever).
#   GRAMMAR    bison must still report ZERO conflicts. Not decoration: this
#              project rejected `IDENT expression` as a statement form over 4
#              MEASURED conflicts, and `IDENT COMMA IDENT` inside a `for` head
#              is exactly the shape that could collide with the counted loop
#              and the plain element form.
#   REFUSAL    the element and the index may not share a name, beside the
#              controls that say the plain forms are untouched.
#   VALGRIND   a new heap field on every for-each node. PLAT-OPTPARAM's lesson:
#              a parser field that is not freed turned 22 suites red.
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

printf 'TIER semantics, snapshot and the plain forms\n'
if ! timeout -k 5 60 ./gbasic tests/for_each_index_test.bas >"$work/out" 2>"$work/err"; then
    cat "$work/err"; fail "the fixture did not run"
elif grep -q MISMATCH "$work/out"; then
    grep MISMATCH "$work/out"; fail "the fixture disagreed with itself"
elif ! grep -qx 'mismatches: 0' "$work/out"; then
    fail "the fixture did not finish"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    # A coverage floor: a fixture that stops running its checks otherwise
    # passes by asserting nothing.
    if [ -z "$n" ] || [ "$n" -lt 17 ]; then
        fail "only ${n:-0} checks ran, wanted at least 17"
    else
        pass "$n checks (write-back, snapshot, plain forms, scope, nesting)"
    fi
fi

printf 'TIER grammar: still zero conflicts\n'
if command -v bison >/dev/null 2>&1; then
    c=$(bison -d -o "$work/p.tab.c" src/parser.y 2>&1 | grep -c conflict || true)
    if [ "$c" -eq 0 ]; then
        pass "bison reports no conflicts"
    else
        bison -d -o "$work/p.tab.c" src/parser.y 2>&1 | grep conflict
        fail "the index form introduced $c grammar conflict line(s)"
    fi
else
    pass "SKIP (bison unavailable)"
fi

printf 'TIER refusal, beside its legal neighbours\n'
printf 'for each x, x in [1,2]\n    print x\nend for\n' > "$work/dup.bas"
if ./gbasic "$work/dup.bas" >/dev/null 2>"$work/dup.err"; then
    fail "the element and the index were allowed to share a name"
elif ! grep -q "names the element and the index the same thing" "$work/dup.err"; then
    cat "$work/dup.err"; fail "refused, but not with the message that explains it"
else
    pass "the element and the index may not share a name"
fi
# THE CONTROLS. Without these, "it refuses" is satisfied by a build that
# refuses every indexed loop.
printf 'for each v, i in [1,2]\n    print string(i) + ":" + string(v)\nend for\n' > "$work/ok.bas"
if ./gbasic "$work/ok.bas" >"$work/ok.out" 2>&1 && grep -qx '0:1' "$work/ok.out"; then
    pass "CONTROL: distinct names are accepted"
else
    cat "$work/ok.out"; fail "a legal indexed loop was refused"
fi
printf 'for each v in [1,2]\n    print v\nend for\n' > "$work/plain.bas"
if ./gbasic "$work/plain.bas" >/dev/null 2>&1; then
    pass "CONTROL: the plain form still parses"
else
    fail "the plain for-each stopped working"
fi

printf 'TIER the discarded-write warning (2107)\n'
# WHAT THIS TIER IS FOR. The warning must fire on a write nothing reads and be
# SILENT on the two legitimate patterns -- reassigning the element as scratch,
# and enriching the copy for use inside the body. Both are ordinary code, and a
# syntactic "you assigned to a loop variable" rule flags them, which is exactly
# how the blind-shadow warning reached 287 false positives and was reverted.
#
# So this is asserted as a DIFFERENCE. "It warns" alone passes on a rule that
# warns about everything; "it is silent" alone passes on one that never fires.
# Both halves are required, and the controls outnumber the positive case.
dw() { GBASIC_PATH=stdlib ./gbasic "$1" 2>&1 >/dev/null </dev/null || true; }

cat > "$work/dead.bas" <<'BAS'
rows = [ { n: 1 } ]
for each item in rows
    item.n = 999
end for
BAS
if dw "$work/dead.bas" | grep -q "COPY of the element"; then
    pass "a write nothing reads is reported"
else
    fail "a discarded write was NOT reported"
fi
if dw "$work/dead.bas" | grep -q "for each item, i in"; then
    pass "and the message names the remedy that exists"
else
    fail "reported, but without pointing at the index form"
fi

# CONTROL 1: reassigning the element and then using it.
cat > "$work/live1.bas" <<'BAS'
rows = [ { n: 1 } ]
for each row in rows
    row = { n: row.n * 2 }
    print row.n
end for
BAS
[ -z "$(dw "$work/live1.bas")" ] && pass "CONTROL: reassign-then-use is silent" \
    || { dw "$work/live1.bas"; fail "warned about a legitimate reassignment"; }

# CONTROL 2: enriching the copy and using it in the body.
cat > "$work/live2.bas" <<'BAS'
rows = [ { n: 1 } ]
for each row in rows
    row.label = "r" + string(row.n)
    print row.label
end for
BAS
[ -z "$(dw "$work/live2.bas")" ] && pass "CONTROL: enrich-then-use is silent" \
    || { dw "$work/live2.bas"; fail "warned about a legitimate enrichment"; }

# CONTROL 3: THE WRITE-BACK IDIOM. Silent without being special-cased --
# `rows[i] = item` reads `item`, so the write is not dead.
cat > "$work/live3.bas" <<'BAS'
rows = [ { n: 1 } ]
for each item, i in rows
    item.n = item.n * 10
    rows[i] = item
end for
BAS
[ -z "$(dw "$work/live3.bas")" ] && pass "CONTROL: the write-back idiom is silent" \
    || { dw "$work/live3.bas"; fail "warned about the idiom the index form exists for"; }

# CONTROL 4: a read in the OTHER arm of a branch. Source order, not dataflow,
# so a textually later read suppresses -- erring towards silence.
cat > "$work/live4.bas" <<'BAS'
rows = [ { n: 1 } ]
for each r in rows
    if r.n > 0 then
        r.n = 5
    else
        print r.n
    end if
end for
BAS
[ -z "$(dw "$work/live4.bas")" ] && pass "CONTROL: a read in another branch is silent" \
    || { dw "$work/live4.bas"; fail "warned when a branch reads the value"; }

# CONTROL 5: the opt-out. A diagnostic whose own fixture cannot silence it is
# one that would have to be weakened instead.
cat > "$work/ignored.bas" <<'BAS'
on warning ignore
rows = [ { n: 1 } ]
for each item in rows
    item.n = 999
end for
BAS
[ -z "$(dw "$work/ignored.bas")" ] && pass "CONTROL: on warning ignore suppresses it" \
    || fail "the warning could not be silenced"

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/for_each_index_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_for_each_index.sh\n'
