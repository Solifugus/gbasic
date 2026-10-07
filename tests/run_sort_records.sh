#!/usr/bin/env bash
# `sort(rows, { by: ..., descending: ... })` -- ordering a list of RECORDS.
#
# `sort` refused records outright before this (`sort supports only scalar array
# values`), so a list of records -- which is what business data IS -- could not
# be sorted at all. MEASURED, the consequence was four hand-rolled sorts in
# shipped stdlib:
#
#   stdlib/frame.bas:340        one field ascending, O(n^2) insertion sort
#   stdlib/fundamentals.bas:183 TWO fields, faked as r["end"] + "|" + r["start"]
#   stdlib/nlq.bas:1507         score DESCENDING then id ASCENDING
#   stdlib/stats.bas:3438,8313  an INDEX array against a parallel column
#
# -- and the third is why `descending` takes two shapes. A plain boolean turns
# every key around, which covers the first two and NOT nlq's, whose own comment
# says why the total order matters ("or the answer depends on a driver's row
# order"). So `descending` is a boolean (every key) or a LIST OF FIELD NAMES.
# The fourth is not a field sort at all and a comparator would not have helped
# it either, since what it orders is an index array against a separate column.
#
# A COMPARATOR FUNCTION IS REJECTED, NOT DEFERRED: fields, several fields and
# per-field direction cover every measured case; a comparator is the one piece
# of code everyone gets wrong (`a - b` against `a < b`, and returning a boolean
# silently half-sorts); and it costs a gBASIC call per comparison, O(n log n)
# times, in a tree-walking interpreter.
#
# A DECLARED COLLATION (`using: {trimmed; caseless}`) IS DEFERRED, with the
# measurement as the reason: of 73 `sort` call sites in this tree, NONE orders a
# lowered, trimmed or naturally-collated key. Its spelling is not free either --
# a comparison lens is a grammar construct driven by a parser-triggered lexer
# mode, not a value, so nothing can pass one as an argument today.
#
# SELF-CHECKING, NOT GOLDEN, AND FORCED: every defect here is a PLAUSIBLE ORDER.
# A sort honouring only the first key, or turning the wrong one around, or losing
# ties to an unstable merge, produces a list that still looks sorted, and a
# golden would record it as expected and defend it.
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

echo "--- TIER 1: semantics, the oracle, stability and the refusals ---"
# The ORACLE tier inside the fixture is a plainly-correct insertion sort over the
# same keys, written in gBASIC -- a SECOND IMPLEMENTATION rather than a second
# call into the one under test, which is what catches a merge sort that is
# self-consistently wrong. Run over 40 rows as well as 3, so agreement is not
# luck.
run tests/sort_records/sort_records_test.bas 40 "sort by field"

echo "--- TIER 2: the migrations, whose goldens must not move ---"
# THE POINT OF A CORE PRIMITIVE IS THAT LIBRARIES STOP HAND-ROLLING, and a
# migration proves the primitive MATCHES the hand-rolled behaviour only if the
# output is byte-identical. Both of these replaced an insertion sort.
for f in fundamentals_derived_test fundamentals_series_test; do
    got="$({ GBASIC_PATH=stdlib timeout -k 5 300 ./gbasic "examples/$f.bas" 2>/dev/null </dev/null || true; })"
    if [ "$got" = "$(cat "examples/$f.out")" ]; then
        printf 'ok   %s byte-identical after _sort_rows moved to core sort\n' "$f"
    else
        printf 'MISMATCH %s moved:\n' "$f"
        diff <(printf '%s\n' "$got") "examples/$f.out" | head -6 | sed 's/^/  /'
        status=1
    fi
done
# AND THE MIGRATIONS ARE REAL, read from the source, because a library that
# quietly kept its own sort would pass the goldens above perfectly.
for pair in "stdlib/fundamentals.bas:_sort_rows" "stdlib/nlq.bas:_by_score"; do
    file="${pair%%:*}"; fn="${pair##*:}"
    body="$(awk -v f="$fn" '$0 ~ ("function " f "\\(") {on=1} on {print} on && /end function/ {exit}' "$file")"
    if printf '%s' "$body" | grep -q 'sort(.*by:'; then
        printf 'ok   %s %s calls core sort\n' "$file" "$fn"
    else
        printf 'MISMATCH %s %s does not call core sort with `by:`\n' "$file" "$fn"
        status=1
    fi
    if printf '%s' "$body" | grep -qE 'while|for each'; then
        printf 'MISMATCH %s %s still contains a loop; the hand-rolled sort is not gone\n' "$file" "$fn"
        status=1
    else
        printf 'ok   %s %s has no loop left\n' "$file" "$fn"
    fi
done
# nlq's own suite is the behavioural half: its ranking is what `_by_score` is for.
if GBASIC_PATH=stdlib timeout -k 5 600 ./tests/run_nlq.sh >"$tmp/nlq.log" 2>&1; then
    printf 'ok   run_nlq still passes with _by_score on core sort\n'
else
    tail -15 "$tmp/nlq.log" | sed 's/^/  /'; printf 'FAIL run_nlq\n'; status=1
fi

echo "--- TIER 3: the disagreement this migration FOUND, pinned ---"
# `frame.sort_by` ranks an absence LAST; core `sort` ranks it FIRST. So
# `frame.sort_by` is NOT migrated, and the disagreement is PINNED rather than
# left to be discovered -- changing either side without the other now goes red,
# and whoever rules on it has to come here.
#
# There is no universal answer to appeal to: `ORDER BY x ASC` puts NULLs LAST in
# PostgreSQL and Oracle and FIRST in MySQL and SQLite. It is a convention gBASIC
# has to choose, which makes it the user's call rather than this suite's.
cat >"$tmp/absence.bas" <<'BAS'
load frame
rows = [ { x: 3 }, { x: unknown }, { x: 1 } ]
print "frame: " + string(frame.to_rows(frame.sort_by(frame.from_rows(rows), "x")))
print "core:  " + string(sort(rows, { by: "x" }))
BAS
out="$({ GBASIC_PATH=stdlib ./gbasic "$tmp/absence.bas" 2>&1 </dev/null || true; })"
expect_frame='frame: [{"x":1},{"x":3},{"x":unknown}]'
expect_core='core:  [{"x":unknown},{"x":1},{"x":3}]'
for want in "$expect_frame" "$expect_core"; do
    if printf '%s\n' "$out" | grep -qxF "$want"; then
        printf 'ok   pinned: %s\n' "$want"
    else
        printf 'MISMATCH the absence convention moved\n  want: %s\n  got:\n' "$want"
        printf '%s\n' "$out" | sed 's/^/    /'
        status=1
    fi
done

echo "--- TIER 4: refusals as pinned messages ---"
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
want 'three arguments'  'print sort([1], {}, {})'  'sort expects 1 to 2 arguments'
want 'no arguments'     'print sort()'             'sort expects 1 to 2 arguments'
# AND THE OPTIONS ARE READ BEFORE THE ARRAY IS TOUCHED, so a misspelled option
# cannot leave a half-sorted array behind -- asserted by giving it an array that
# would ALSO be refused, and requiring the OPTIONS message.
want 'options checked first' 'print sort([1, "a"], { bye: 1 })'  "unknown option 'bye'"

echo "--- TIER 5: valgrind ---"
# The stable merge allocates a scratch array per sort and MOVES item structs
# through it; a struct copied rather than moved would double-free a refcounted
# element, which produces the right answer and a corrupted heap.
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    GBASIC_PATH=stdlib vg_run ./gbasic tests/sort_records/sort_records_test.bas \
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
