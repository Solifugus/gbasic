#!/usr/bin/env bash
# `rows.amount` -- PROJECTION: the array of that field from every element, and
# the blocking primitive for working with data in bulk.
#
# WHY THIS AND NOT `map`/`filter`/`sort`: MEASURED across stdlib, examples and
# tests before anything was built -- of 813 `for each` loops, 22 are a sum over
# the loop variable and EVERY ONE of the 22 is `total = total + r.amount`,
# summing a FIELD. `sum`, `mean`, `count`, `min` and `max` already existed and
# simply could not reach data, because nothing in the language could name a
# field across a list. One expression form unlocks five aggregates, and
# `any(rows.paid)`/`all(rows.paid)` then need no predicate at all.
#
# THE SYNTAX IS SAFE BY PROOF RATHER THAN BY SURVEY: every dotted access on an
# array raised before this, so no working program could contain the shape. That
# is a stronger argument than the enumeration the record/modifier classifier
# rests on, which had to survey the record forms one by one.
#
# SELF-CHECKING, NOT GOLDEN, AND FORCED: every defect here is a PLAUSIBLE
# NUMBER. A projection that silently drops the elements with no value yields a
# shorter array, and every value check in the fixture -- the sum, the mean, the
# min, the max -- still passes on it, because the dropped elements contributed
# nothing. A golden would record the shortened answer as expected and defend it.
# What catches it is the ORACLE (the loop it replaces must agree) and the COUNT.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null
status=0
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

run() {   # run <fixture> <min-ok> <label>
    local out
    out="$({ GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic "$1" 2>&1 </dev/null || true; })"
    local bad ok
    bad="$(printf '%s\n' "$out" | grep -c '^MISMATCH' || true)"
    ok="$(printf '%s\n' "$out" | grep -c '^ok  ' || true)"
    if [ "${bad:-0}" -ne 0 ]; then
        printf '%s\n' "$out" | grep '^MISMATCH' | sed 's/^/  /'
        printf 'FAIL %s\n' "$3"; status=1
        return
    fi
    # A COVERAGE FLOOR, because a fixture whose checks stopped running reports
    # no MISMATCH either -- the way this gate has gone quiet more than once.
    if [ "${ok:-0}" -lt "$2" ]; then
        printf 'FAIL %s (only %s checks ran; at least %s expected)\n' "$3" "$ok" "$2"
        status=1
        return
    fi
    printf 'PASS %s (%s checks)\n' "$3" "$ok"
}

echo "--- TIER 1: the oracle is the loop it replaces ---"
# Plus order, absence, nesting, the widened min/max, any/all, and the refusals.
run tests/projection/projection_test.bas 58 "projection semantics"

echo "--- TIER 2: the ordering operator is the oracle for the sorter ---"
# `sort`/`min`/`max` and `<` ask ONE question, and they had drifted: `<`
# answered for money while `sort(invoices.total)` refused. Different code (a
# raising branch chain against a qsort comparator that cannot raise), same
# question, so agreement is evidence rather than a second call into one place.
run tests/projection/ordering_oracle_test.bas 18 "ordering agrees with the < operator"

echo "--- TIER 3: refusals, each beside its nearest legal neighbour ---"
say() {
    printf '%s\n' "$1" >"$tmp/p.bas"
    { GBASIC_PATH=stdlib ./gbasic "$tmp/p.bas" 2>&1 </dev/null || true; } \
        | head -1 | sed 's/^[^:]*:[0-9]*:[0-9]*: //'
}
want() {
    local got; got="$(say "$2")"
    case "$got" in
        *"$3"*) printf 'ok   %s\n' "$1" ;;
        *) printf 'MISMATCH %s\n  want: %s\n  got:  %s\n' "$1" "$3" "$got"; status=1 ;;
    esac
}
# A NON-RECORD ELEMENT IS A SHAPE ERROR, not missing data, so it raises and says
# WHICH element -- a list someone accidentally built from numbers should name the
# one that broke it.
want 'non-record element named' 'rows = [ { a: 1 }, 7 ]
print rows.a'                   'element 1 is a number'
# THE FIVE MUTATORS STILL REFUSE, and the sentence is about the right subject:
# before this they reached `resolve_lvalue_ref` and said `field assignment
# target expects a record`, naming the one thing that is NOT wrong, since `rows`
# plainly IS an array.
for verb in append prepend insert remove remove_value; do
    case "$verb" in
        insert) call="insert(rows.a, 0, 9)" ;;
        remove) call="remove(rows.a, 0)" ;;
        *)      call="$verb(rows.a, 9)" ;;
    esac
    want "$verb refused" "rows = [ { a: 1 } ]
$call" "$verb cannot change a projection"
done
# CONTROLS: the three VALUE-returning builtins must go on working through a
# projection, or "a projection is not a place" is satisfied by refusing every
# builtin that takes an array path. These were the defect that produced the
# distinction: `sort(rows.item)` raised `field assignment target expects a
# record`, because sort takes its argument as an lvalue to mutate in place.
want 'sort of a projection works'    'rows = [ { a: 3 }, { a: 1 } ]
print sort(rows.a)'                  '[1,3]'
want 'reverse of a projection works' 'rows = [ { a: 3 }, { a: 1 } ]
print reverse(rows.a)'               '[1,3]'
want 'unique of a projection works'  'rows = [ { a: 3 }, { a: 3 } ]
print unique(rows.a)'                '[3]'
# AND THE OTHER CONTROL: in-place mutation of a real array is UNTOUCHED, which
# is what the five refusals must not have cost.
want 'sort still mutates in place'   'a = [3, 1, 2]
sort(a)
print a'                             '[1,2,3]'
# MIXED CURRENCIES AND MONTH-BEARING DURATIONS: each aggregate says what the
# OPERATOR says for the same pair -- `+` for the fold, `<` for the ordering.
want 'sum across currencies'  'print sum([{USD}"1.00", {EUR}"2.00"])'   'cannot add money in different currencies (USD and EUR)'
want 'max across currencies'  'print max([{USD}"1.00", {EUR}"2.00"])'   'cannot order money in different currencies (USD and EUR)'
want 'sort a month span'      'print sort([1 month, 2 days])'           'a month has no fixed length'
want 'money array with a number' 'print sum([{USD}"1.00", 7])'          'element 1 is a number'
# CONTROLS for those four, or the money work is satisfied by refusing money.
want 'one currency sums'      'print sum([{USD}"1.00", {USD}"2.00"])'   '3.00'
want 'one currency orders'    'print max([{USD}"1.00", {USD}"2.00"])'   '2.00'
want 'exact durations order'  'print sort([3 days, 1 hour])'            '[1 hour,3 days]'
# `any`/`all` TAKE BOOLEANS and the refusal must name a remedy THAT EXISTS: the
# first version said `any(rows.amount > 0)`, which does NOT work, since a
# projection is an array and PLAT-EQ refuses ordering on arrays. The
# `web.configure` defect class.
want 'any refuses non-boolean' 'rows = [ { a: 1 } ]
print any(rows.a)'             'takes a boolean field'
want 'all refuses non-boolean' 'rows = [ { a: 1 } ]
print all(rows.a)'             'takes a boolean field'

echo "--- TIER 4: the remedy the refusal names must work ---"
# A MESSAGE THAT NAMES A REMEDY IS A CLAIM, and this tree has shipped two that
# were false (`web.configure`, and `any`'s first draft). So the remedies are
# RUN rather than read: `projection_not_a_place` offers two.
want 'remedy: the array itself' 'rows = [ { a: 1 } ]
append(rows, { a: 2 })
print count(rows)'              '2'
want 'remedy: name it first'    'rows = [ { a: 1 } ]
amounts = rows.a
append(amounts, 9)
print amounts'                  '[1,9]'

echo "--- TIER 5: a projection is a COPY, which only mutation can show ---"
# The field values are copied out, so changing the projection cannot reach back
# into the rows -- and changing a row cannot reach the projection either. Both
# directions, because sharing the storage in either one is a different defect
# and refcounted copy-on-write makes each independently possible.
want 'writing the copy leaves rows' 'rows = [ { a: 1 }, { a: 2 } ]
amounts = rows.a
amounts[0] = 99
print string(rows[0].a) + " " + string(amounts[0])'  '1 99'
want 'writing rows leaves the copy' 'rows = [ { a: 1 }, { a: 2 } ]
amounts = rows.a
rows[0].a = 99
print string(rows[0].a) + " " + string(amounts[0])'  '99 1'

echo "--- TIER 6: valgrind ---"
# A NEW ALLOCATION PATH PER ELEMENT: the projection mallocs an array and
# `value_copy`s each field out of shared refcounted storage, and the money and
# duration comparators were added to a qsort path. A borrowed reference freed
# once too often here produces a CORRECT ANSWER and a corrupted heap.
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    vg_fail=0
    for f in tests/projection/projection_test.bas \
             tests/projection/ordering_oracle_test.bas; do
        GBASIC_PATH=stdlib vg_run ./gbasic "$f" >/dev/null 2>"$tmp/vg.err" </dev/null
        rc=$?
        if [ "$rc" = "$VG_EXIT" ]; then
            printf '  %s\n' "$f"; cat "$tmp/vg.err"; vg_fail=1
        fi
    done
    if [ "$vg_fail" = 1 ]; then
        printf 'FAIL valgrind\n'; status=1
    else
        printf 'PASS valgrind (no definite leak or invalid access)\n'
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

exit $status
