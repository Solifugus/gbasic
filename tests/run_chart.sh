#!/usr/bin/env bash
# chart.bas Phase 1 (docs/chart_design.md) — charts as deterministic SVG text.
#
# Tiers, each with a different truth standard:
#   GOLDEN     examples/chart_test.bas byte-for-byte (also in run_examples;
#              repeated here so this runner stands alone).
#   ORACLE     tests/chart_oracle_test.bas — under FIXED margins and FIXED axis
#              bounds every pixel coordinate is hand-computable, so the test
#              asserts literals computed on paper, not values the renderer
#              produced. A golden records whatever we emit; this tier knows
#              what we SHOULD emit.
#   DETERMINISM the same program run twice must emit identical bytes — the
#              property the whole SVG-as-string design exists to buy.
#   STRUCTURE  every chart fed back through OUR OWN xml.parse: well-formed
#              (catches escaping bugs generically — the fixture's title is
#              hostile), circles inside the viewBox, element census. SKIPs
#              cleanly when the build has no libxml2.
#   VALGRIND   over the golden program (skips if valgrind absent).
#
# Negatives (refusals by name) live in run_negative.sh: negative_chart_*.
set -u
cd "$(dirname "$0")/.."
status=0

printf -- '-- golden: the four-chart rendering\n'
out=$(mktemp)
if timeout 60 ./gbasic examples/chart_test.bas >"$out" 2>&1 && diff -q examples/chart_test.out "$out" >/dev/null; then
    printf 'PASS examples/chart_test.bas\n'
else
    printf 'FAIL examples/chart_test.bas\n'
    diff examples/chart_test.out "$out" | head -5 || true
    status=1
fi

printf -- '-- oracle: hand-computed pixel coordinates under fixed margins\n'
ora=$(timeout 60 ./gbasic tests/chart_oracle_test.bas 2>&1)
if [ -n "$ora" ] && ! printf '%s' "$ora" | grep -q "MISMATCH"; then
    n=$(printf '%s\n' "$ora" | grep -c "^ok ")
    printf 'PASS oracle: %s hand-computed assertions hold\n' "$n"
else
    printf 'FAIL oracle\n'
    printf '%s\n' "$ora" | grep "MISMATCH" | head -5
    status=1
fi
# a coverage floor: a tier that stops running its checks otherwise passes
# by saying nothing.
n=$(printf '%s\n' "$ora" | grep -c "^ok ")
if [ "$n" -lt 19 ]; then
    printf 'FAIL oracle coverage: %s checks ran, expected 19\n' "$n"
    status=1
fi

printf -- '-- golden 2: bar + histogram (Phase 2)\n'
out2=$(mktemp)
if timeout 60 ./gbasic examples/chart_bar_test.bas >"$out2" 2>&1 && diff -q examples/chart_bar_test.out "$out2" >/dev/null; then
    printf 'PASS examples/chart_bar_test.bas\n'
else
    printf 'FAIL examples/chart_bar_test.bas\n'
    diff examples/chart_bar_test.out "$out2" | head -5 || true
    status=1
fi
rm -f "$out2"

printf -- '-- determinism: two runs, identical bytes\n'
a=$(mktemp); b=$(mktemp)
timeout 60 ./gbasic examples/chart_test.bas >"$a" 2>&1
timeout 60 ./gbasic examples/chart_test.bas >"$b" 2>&1
if cmp -s "$a" "$b"; then
    printf 'PASS two renders are byte-identical\n'
else
    printf 'FAIL renders differ between runs\n'
    status=1
fi
rm -f "$a" "$b"

printf -- '-- structure: our own xml.parse as the second pair of eyes\n'
sout=$(timeout 60 ./gbasic tests/chart_structure_test.bas 2>&1)
if printf '%s' "$sout" | grep -q "not available in this build"; then
    printf 'SKIP structure tier (xml module not in this build)\n'
else
    want='root svg
paths 2
circles 7
circles inside viewBox 1
legend swatches 2
hostile title is TEXT 1'
    if [ "$sout" = "$want" ]; then
        printf 'PASS structure: well-formed, census exact, hostile title stayed text\n'
    else
        printf 'FAIL structure\n'
        diff <(printf '%s\n' "$want") <(printf '%s\n' "$sout") || true
        status=1
    fi
fi

if command -v valgrind >/dev/null 2>&1; then
    printf -- '-- valgrind over the golden program\n'
    if valgrind --error-exitcode=99 --leak-check=full ./gbasic examples/chart_test.bas >/dev/null 2>/tmp/chart_vg.txt; then
        printf 'PASS valgrind clean\n'
    else
        printf 'FAIL valgrind\n'
        grep -E "definitely lost|ERROR SUMMARY" /tmp/chart_vg.txt || true
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

rm -f "$out"
exit "$status"
