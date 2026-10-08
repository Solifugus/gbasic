#!/usr/bin/env bash
# §2a and §2b of docs/bulk_data_design.md: WHAT AN AGGREGATE DOES WITH AN ABSENCE,
# `present(a)`, the three defects `mode` shipped with, and where an absence SORTS.
#
# EVERY AGGREGATE USED TO REFUSE a list with a hole in it (`sum expects a numeric
# array`), which made the projection increment 1 shipped useless on the first
# ragged row -- and real business data IS ragged.
#
# SQL'S ANSWER, WITH SQL'S WARNING, and both halves are a standard rather than a
# convention: SUM ignores NULLs, AVG divides by the NON-NULL count, and the SQL
# standard raises SQLSTATE 01003, "null value eliminated in set function" -- class
# 01 being the warning class. PostgreSQL does not emit it; gBASIC does, as 2111.
#
# WHY A WARNING IS AFFORDABLE HERE WHEN THE SAME SHAPE WAS WITHDRAWN ON 2026-10-03:
# the absence-coercion warning fired on correct code at 8 sites of 10, because
# SHOWING that something is absent is an idiom. There is no counterpart here --
# nobody writes `sum(rows.amount)` in order to skip nulls -- so the expected
# false-positive rate is near zero, which is the bar warning_model_design.md row 9
# shipped at rather than row 7's, which was reverted.
#
# SELF-CHECKING, NOT GOLDEN, AND FORCED: every defect here is a PLAUSIBLE NUMBER. A
# `mean` dividing by the full count is quietly too low on every average; a `mode`
# returning the first element of a price list looks exactly like a real mode; an
# all-absent total reported as 0 reads as "they bought nothing". A golden would
# record each as expected and defend it.
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

echo "--- TIER 1: skip, warn, all-absent is unknown, mode, median, sort order ---"
run tests/absence/absence_test.bas 57 "absence semantics"

echo "--- TIER 2: the warning reaches stderr and --json-diagnostics ---"
# A WARNING NOBODY SEES IS A SILENT SKIP, which is the thing §2a exists to prevent.
# The fixture above reads the warning IN LANGUAGE through `on warning goto next`;
# this tier checks the two channels an author and an editor actually watch, which
# no in-language assertion can see.
cat >"$tmp/warn.bas" <<'BAS'
print sum([10, unknown, 7])
BAS
err="$({ ./gbasic "$tmp/warn.bas" 2>&1 >/dev/null </dev/null || true; })"
case "$err" in
    *"skipped 1 absent value of 3"*[2111]*) printf 'ok   stderr carries the warning and its code\n' ;;
    *) printf 'MISMATCH stderr\n  got: %s\n' "$err"; status=1 ;;
esac
# LOCATED, or an editor cannot put it anywhere and a reader cannot find the line.
case "$err" in
    *"$tmp/warn.bas:1:"*) printf 'ok   and it is located\n' ;;
    *) printf 'MISMATCH the warning is not located\n  got: %s\n' "$err"; status=1 ;;
esac
json="$({ ./gbasic --json-diagnostics "$tmp/warn.bas" 2>&1 >/dev/null </dev/null || true; })"
if printf '%s' "$json" | grep -q '"subcode":2111' && printf '%s' "$json" | grep -q '"severity":"warning"'; then
    printf 'ok   --json-diagnostics carries it as a warning\n'
else
    printf 'MISMATCH --json-diagnostics\n  got: %s\n' "$json"; status=1
fi
# AND NOTHING ELSE REACHES THE STREAM, which is the defect run_parse_exit.sh exists
# for: one unlocated bare line in a JSON stream makes the stream unparseable.
if printf '%s\n' "$json" | grep -qvE '^\{|^\[|^\s*$|^\]'; then
    printf 'MISMATCH a non-JSON line reached the JSON stream\n'
    printf '%s\n' "$json" | grep -vE '^\{|^\[|^\s*$|^\]' | sed 's/^/  /'
    status=1
else
    printf 'ok   nothing but JSON on the JSON stream\n'
fi
# THE CONTROL: no absence, no warning. Without it, every check above is satisfied
# by a rule that fires on every aggregate -- which is how a warning becomes noise
# people turn off.
clean="$({ printf 'print sum([10, 7])\n' >"$tmp/clean.bas"; ./gbasic "$tmp/clean.bas" 2>&1 >/dev/null </dev/null || true; })"
if [ -z "$clean" ]; then
    printf 'ok   CONTROL: a list with no absence is silent\n'
else
    printf 'MISMATCH a clean aggregate warned\n  got: %s\n' "$clean"; status=1
fi

echo "--- TIER 3: the migrations the ruling unblocked ---"
# `frame.sort_by` ranked an absence LAST and core `sort` ranked it FIRST --
# increment 2 found that and PINNED both answers rather than resolving it, because
# `ORDER BY x ASC` puts NULLs last in PostgreSQL and Oracle and first in MySQL and
# SQLite. Matthew ruled LAST on 2026-10-07, so core moved and the library is built
# on it. THEY MUST NOW AGREE, which is the pin that tier replaced.
cat >"$tmp/agree.bas" <<'BAS'
load frame
rows = [ { x: 3, n: "c" }, { x: unknown, n: "u" }, { x: 1, n: "a" }, { x: nothing, n: "z" } ]
df = frame.from_rows(rows)
print "frame: " + string(frame.to_rows(frame.sort_by(df, "x")).n)
print "core:  " + string(sort(rows, { by: "x" }).n)
BAS
out="$({ GBASIC_PATH=stdlib ./gbasic "$tmp/agree.bas" 2>&1 </dev/null || true; })"
f="$(printf '%s\n' "$out" | sed -n 's/^frame: //p')"
c="$(printf '%s\n' "$out" | sed -n 's/^core:  //p')"
if [ -n "$f" ] && [ "$f" = "$c" ]; then
    printf 'ok   frame.sort_by and core sort agree: %s\n' "$f"
else
    printf 'MISMATCH frame and core disagree\n  frame: %s\n  core:  %s\n' "$f" "$c"; status=1
fi
# AND THEY AGREE ON THE RULED ANSWER, not merely with each other -- without this,
# both moving to absences-FIRST together would pass.
if [ "$f" = '["a","c","u","z"]' ]; then
    printf 'ok   and on the ruled order: ordinaries, then absences in entry order\n'
else
    printf 'MISMATCH the ruled order moved\n  got: %s\n' "$f"; status=1
fi
# A DEFECT THE MIGRATION FIXED, asserted because nothing else would notice it came
# back: `frame._less` guarded `is_unknown` and `is_unknown(nothing)` is FALSE, so a
# column holding `nothing` fell through to `a < b` and RAISED mid-sort. A frame
# built from JSON, a database or read_csv holds `nothing` wherever a value was
# absent, so that column could not be sorted at all.
if printf '%s\n' "$out" | grep -q 'support only = and !='; then
    printf 'MISMATCH sorting a column holding `nothing` raises again\n'; status=1
else
    printf 'ok   a column holding `nothing` sorts instead of raising\n'
fi
# AND THE HAND-ROLLED VERSIONS ARE GONE, read from the source: a library that kept
# its own sort or its own serialize-and-search would pass every check above.
for pair in "sort_by:sort(rows, { by:" "dedupe:unique(to_rows"; do
    fn="${pair%%:*}"; needle="${pair##*:}"
    body="$(awk -v f="$fn" '$0 ~ ("function " f "\\(") {on=1} on {print} on && /end function/ {exit}' stdlib/frame.bas)"
    if printf '%s' "$body" | grep -qF "$needle"; then
        printf 'ok   frame.%s is built on core\n' "$fn"
    else
        printf 'MISMATCH frame.%s does not call core\n' "$fn"; status=1
    fi
    if printf '%s' "$body" | grep -qE '^\s*while '; then
        printf 'MISMATCH frame.%s still has a loop; the hand-rolled version is not gone\n' "$fn"
        status=1
    else
        printf 'ok   frame.%s has no loop left\n' "$fn"
    fi
done
# `_less` existed ONLY to serve the insertion sort, and its absence rule is the one
# that disagreed with core. A leftover copy is a second rule that can drift back.
if grep -q 'function _less' stdlib/frame.bas; then
    printf 'MISMATCH stdlib/frame.bas still defines _less, the rule that disagreed\n'; status=1
else
    printf 'ok   _less is gone, so there is one absence rule rather than two\n'
fi

echo '--- TIER 3b: the loops excluding and unique replaced ---'
# MEASURED PROPERLY, which corrected this release's own count: the design said "20
# append-if-not-in-another-list loops", and separating the two shapes gives 12 that
# are a genuine SET DIFFERENCE (the haystack is a DIFFERENT list) and 12 that are a
# DEDUPE-ACCUMULATE (the haystack IS the list being appended to). Those are
# different operators -- `excluding` and `unique` -- and conflating them is how a
# migration replaces one with the other.
#
# SEVEN LOOPS MIGRATED across three libraries, read from the SOURCE because their
# goldens pass either way:
for pair in "stdlib/discovery.bas:ta excluding tb"             "stdlib/discovery.bas:ra.reads excluding rb.reads"             "stdlib/nlq.bas:excluding allowed"             "stdlib/stats.bas:unique(concat("; do
    file="${pair%%:*}"; needle="${pair#*:}"
    if grep -qF "$needle" "$file"; then
        printf 'ok   %s uses `%s`\n' "$file" "$needle"
    else
        printf 'MISMATCH %s no longer uses `%s`\n' "$file" "$needle"; status=1
    fi
done
# AND THE HAND-ROLLED GUARDS ARE GONE from the places that moved. A library that
# kept its loop and merely gained an operator elsewhere would pass the greps above.
if grep -q 'if not contains(tb, x)' stdlib/discovery.bas; then
    printf 'MISMATCH discovery still hand-rolls its set difference\n'; status=1
else
    printf 'ok   discovery has no hand-rolled set difference left\n'
fi
# THE REST ARE LEFT, AND THAT IS STATED RATHER THAN SILENT: the remaining loops
# append a TRANSFORM of the element, or test against a list holding a transform of
# it, which `excluding` cannot express without a `map` -- and §5 prices `map`
# deliberately after this release. The count is asserted so it cannot drift
# unnoticed in either direction.
# Counted by SHAPE, not by grepping for the guard: a bare `if not contains(...)`
# appears 136 times in stdlib and most of them are not this pattern at all, so a
# line count would be measuring something else and would never be re-measured.
remaining="$(for f in stdlib/*.bas; do awk '
  /for each/ { inloop=1; hay=""; dst=""; n=0 }
  inloop && match($0, /if +not +contains\(([A-Za-z_][A-Za-z0-9_.]*)/, m) { hay=m[1] }
  inloop && match($0, /append\(([A-Za-z_][A-Za-z0-9_.]*)/, m) { dst=m[1]; n++ }
  inloop && (/^[ \t]*next/ || /end for/) {
      if (hay != "" && n == 1 && dst != "") { print "x" }
      inloop=0; hay=""; dst=""; n=0 }' "$f"; done | wc -l | tr -d " ")"
if [ "$remaining" -ge 10 ] && [ "$remaining" -le 20 ]; then
    printf 'ok   %s of the 24 measured loops remain, each appending a TRANSFORM\n' "$remaining"
else
    printf 'MISMATCH %s loops of that shape remain; expected 10..20 -- re-measure, do not just move the bound\n' "$remaining"
    status=1
fi

echo "--- TIER 4: the frame goldens, which must not move ---"
# Every suite that drives a frame through sort_by or dedupe. Byte-identical output
# is what says the primitive MATCHES the hand-rolled behaviour rather than merely
# replacing it.
for s in run_dbframe run_consolidate run_grid run_compile run_market run_scoring; do
    if timeout -k 5 1800 "./tests/$s.sh" >"$tmp/$s.log" 2>&1; then
        printf 'ok   %s\n' "$s"
    else
        tail -12 "$tmp/$s.log" | sed 's/^/  /'
        printf 'FAIL %s\n' "$s"; status=1
    fi
done

echo "--- TIER 5: valgrind ---"
# The absence filter allocates a new array per aggregate call and `mode` builds a
# tie list out of shared refcounted storage; a borrow freed once too often there
# gives the right answer and a corrupted heap.
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    GBASIC_PATH=stdlib vg_run ./gbasic tests/absence/absence_test.bas \
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
