#!/usr/bin/env bash
# `a excluding b` / `a intersecting b` -- set difference and intersection as
# INFIX WORD OPERATORS -- plus `last(a)` and `slice(a, at [, count])`.
#
# MEASURED BEFORE ANY OF IT: of 813 `for each` loops in this tree, 20 are
# "append if not in another list" and 4 are its mirror -- the shape `excluding`
# and `intersecting` replace. `unique`, `contains` and `find` are already core, so
# set operations on lists are the same tier of vocabulary and a library would be
# the odd one out.
#
# THE SPELLING WAS DECIDED BY A MEASUREMENT, NOT A PREFERENCE. This project
# rejected `IDENT expression` as a statement form over FOUR shift/reduce
# conflicts, and the design offered `excluding(a, b)` as the fallback if the infix
# word operator was expensive to bison. Measured: the operator costs **ZERO**
# conflicts, at its own precedence level between comparison and additive. So the
# English reading was free and the function form was not needed -- and TIER 4
# asserts the zero, because that measurement is the whole reason for the shape.
#
# THE PRICE IS TWO RESERVED WORDS, measured too: no identifier in this tree was
# named `excluding` or `intersecting` (three matches, all in prose comments), and
# the keyword-field rules already let `r.excluding` and `{ excluding: 1 }` work --
# so what is actually claimed is the name of a variable, parameter or function,
# which nothing had. TIER 5 asserts both halves.
#
# MEMBERSHIP IS `array_find_index`, the same authority `contains`, `find` and
# `remove_value` use. PLAT-EQ's sweep established that six routes ask "is this
# value present" and must agree; these are the seventh and eighth, and they agree
# BY CONSTRUCTION rather than by a test -- which is the only way that property
# survives an edit. The ORACLE in tier 1 asserts it anyway, through `contains`,
# because "by construction" is a claim about today's source.
#
# SELF-CHECKING, NOT GOLDEN, AND FORCED: every defect here is a PLAUSIBLE LIST. An
# operation that deduplicated, or reordered, or compared records by identity
# rather than by value, returns something that still looks like the answer, and a
# golden would record it as expected.
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
    if [ "${ok:-0}" -lt "$2" ]; then
        printf 'FAIL %s (only %s checks ran; at least %s expected)\n' "$3" "$ok" "$2"
        status=1
        return
    fi
    printf 'PASS %s (%s checks)\n' "$3" "$ok"
}

echo "--- TIER 1: semantics, the contains oracle, last, slice, refusals ---"
run tests/set_ops/set_ops_test.bas 54 "set operations"

echo "--- TIER 2: the migrations these replace ---"
# MEASURED: the "append if not in another list" shape, written out by hand. Both
# of these are the loop the operator exists to remove, and both keep their
# goldens, which is what says the operator means the same thing.
#
# NOTHING IS MIGRATED YET and this tier says so rather than being omitted: the
# 20 measured loops are spread across suites whose goldens are the slow half of
# this gate, so they move in increment 7 with the rest of §7's migrations. What
# is asserted here is that the two spellings AGREE on real data, which is the
# property a migration would rely on.
cat >"$tmp/agree.bas" <<'BAS'
load frame
function hand_excluding(a, b)
    out = []
    for each x in a
        if not contains(b, x) then
            append(out, x)
        end if
    end for
    return out
end function
program main( args )
    ' Real columns out of a real frame, so the values are records and text rather
    ' than the small integers a unit fixture reaches for.
    df = frame.from_rows([ { who: "ann", tag: "a" }, { who: "bob", tag: "b" },
                           { who: "cal", tag: "a" }, { who: "dee", tag: "c" } ])
    rows = frame.to_rows(df)
    drop = [ { who: "bob", tag: "b" } ]
    if string(rows excluding drop) = string(hand_excluding(rows, drop)) then
        print "ok   records agree"
    else
        print "MISMATCH records: " + string(rows excluding drop)
    end if
    tags = rows.tag
    if string(tags excluding ["a"]) = string(hand_excluding(tags, ["a"])) then
        print "ok   a projection agrees"
    else
        print "MISMATCH projection: " + string(tags excluding ["a"])
    end if
end program
BAS
run "$tmp/agree.bas" 2 "the hand-written loop agrees"

echo "--- TIER 3: the cost, measured, because the reference states it ---"
# THE COST IS THE PRODUCT OF THE TWO LENGTHS and reference.md says so, which under
# this tree's rules means a suite has to assert it. Asserted as a RATIO across a
# 2x step on BOTH sides -- ideal 4x -- never as a time, since an absolute bound
# would be a fact about this machine. The data is DISJOINT on purpose: membership
# short-circuits on a match, so overlapping data measures how early the matches
# happen rather than the shape.
#
# BOUNDED BOTH WAYS. Above, because worse than the product means something became
# cubic. Below, because if it ever gets cheaper than the product the REFERENCE IS
# WRONG, and a claim nothing contradicts is how this tree's performance notes went
# stale for six weeks.
shape="$({ GBASIC_PATH=stdlib timeout -k 5 300 ./gbasic tests/set_ops/set_ops_shape.bas 2>&1 </dev/null || true; })"
printf '  %s\n' "$shape"
ratio="$(printf '%s' "$shape" | sed -n 's/.*ratio=\([0-9.]*\).*/\1/p')"
if [ -z "$ratio" ]; then
    printf 'FAIL shape (no ratio reported)\n'; status=1
elif awk -v r="$ratio" 'BEGIN { exit !(r >= 2.5 && r <= 8.0) }'; then
    printf 'PASS shape (ratio %s over a 2x step on both sides; the product of the lengths)\n' "$ratio"
else
    printf 'FAIL shape (ratio %s is outside 2.5..8.0; reference.md states the cost is the product of the lengths)\n' "$ratio"
    status=1
fi

echo "--- TIER 4: the grammar measurement that chose the operator ---"
# NOT DECORATION. The design offered `excluding(a, b)` as the fallback if the
# infix form was expensive, and this project rejected a statement form over FOUR
# conflicts. If the operator ever starts costing conflicts, the decision has to be
# revisited rather than absorbed.
if ! command -v bison >/dev/null 2>&1; then
    printf 'SKIP grammar (bison unavailable)\n'
else
    conf="$({ bison -d -o "$tmp/p.c" src/parser.y 2>&1 || true; } | grep -ci conflict || true)"
    if [ "${conf:-0}" -eq 0 ]; then
        printf 'PASS grammar (bison reports zero conflicts with the set operators)\n'
    else
        bison -d -o "$tmp/p.c" src/parser.y 2>&1 | grep -i conflict | sed 's/^/  /'
        printf 'FAIL grammar (the operator form now costs conflicts; the design says reconsider excluding(a, b))\n'
        status=1
    fi
fi

echo "--- TIER 5: the two reserved words, and what they do NOT claim ---"
say() {
    printf '%s\n' "$1" >"$tmp/p.bas"
    { GBASIC_PATH=stdlib ./gbasic "$tmp/p.bas" 2>&1 </dev/null || true; } | head -1
}
want() {
    local got; got="$(say "$2")"
    case "$got" in
        *"$3"*) printf 'ok   %s\n' "$1" ;;
        *) printf 'MISMATCH %s\n  want: %s\n  got:  %s\n' "$1" "$3" "$got"; status=1 ;;
    esac
}
# The words ARE reserved, and the diagnostic names them in the author's own
# spelling -- which the reserved-word tier in run_negative.sh already generalises
# to any new keyword, and this is the proof it did.
want 'excluding is reserved'    'excluding = 5'                     "'excluding' is a reserved word"
want 'intersecting is reserved' 'print 1
intersecting = 2'                                                   "'intersecting' is a reserved word"
want 'not as a parameter'       'function f(a, excluding)
end function'                                                       "'excluding' is a reserved word"
# AND THE CONTROLS, which are what make the price affordable: a record FIELD may
# still be called either, by literal, by dot and by subscript. Without these,
# "the words are reserved" would be indistinguishable from reserving them
# everywhere, which would have been too expensive to take.
want 'a field may be named so'  'r = { excluding: 1, intersecting: 2 }
print string(r.excluding) + string(r.intersecting)'                 '12'
want 'and read by subscript'    'r = { intersecting: 9 }
print r["intersecting"]'                                            '9'

echo "--- TIER 6: valgrind ---"
# Both operations malloc a result array and `value_copy` elements out of shared
# refcounted storage, while `array_find_index` copies BOTH sides of every
# comparison -- a borrow freed once too often there gives the right answer and a
# corrupted heap.
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    GBASIC_PATH=stdlib vg_run ./gbasic tests/set_ops/set_ops_test.bas \
        >/dev/null 2>"$tmp/vg.err" </dev/null
    rc=$?
    if [ "$rc" = "$VG_EXIT" ]; then
        cat "$tmp/vg.err"; printf 'FAIL valgrind\n'; status=1
    else
        printf 'PASS valgrind (no definite leak or invalid access)\n'
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

exit $status
