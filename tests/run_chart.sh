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
#   GDASH7     ordinal x, axis label formatting, and identifiable marks --
#              self-checking, because the defect it starts from was a
#              plausible picture: a categorical x drew a complete, empty
#              chart. Asserted as DIFFERENCES (a categorical x draws what a
#              numeric x draws; a line marker sits on its bar's centre).
#   VALGRIND   over the golden program (skips if valgrind absent).
#
# Negatives (refusals by name) live in run_negative.sh: negative_chart_*.
set -u
cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
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
if [ "$n" -lt 33 ]; then
    printf 'FAIL oracle coverage: %s checks ran, expected 33\n' "$n"
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

printf -- '-- golden 3: area, pie, heatmap, sparkline (Phase 4)\n'
out3=$(mktemp)
if timeout 60 ./gbasic examples/chart_extra_test.bas >"$out3" 2>&1 && diff -q examples/chart_extra_test.out "$out3" >/dev/null; then
    printf 'PASS examples/chart_extra_test.bas\n'
else
    printf 'FAIL examples/chart_extra_test.bas\n'
    diff examples/chart_extra_test.out "$out3" | head -5 || true
    status=1
fi
rm -f "$out3"

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
hostile title is TEXT 1
pie slices 4
pie swatches 4
heatmap cells 4
heatmap cell texts 3'
    if [ "$sout" = "$want" ]; then
        printf 'PASS structure: well-formed, census exact, hostile title stayed text\n'
    else
        printf 'FAIL structure\n'
        diff <(printf '%s\n' "$want") <(printf '%s\n' "$sout") || true
        status=1
    fi
fi

# ORDINAL X, AXIS LABELS AND MARK IDENTITY -- the three asks in gdash's
# docs/gdash7_platform_ask_charts.md, and SELF-CHECKING rather than golden
# because the defect they start from was itself a plausible picture. A
# categorical x used to produce a complete SVG -- axes, gridlines, fourteen
# text elements -- containing no data at all, so a golden would have recorded
# it as expected. gdash's own test asserted `contains(svg, "<svg")`, WHICH AN
# EMPTY FRAME SATISFIES, and a blank chart survived six phases behind it.
# The load-bearing check is therefore a DIFFERENCE: the same series over a
# numeric x and a categorical x must draw the same number of marks.
printf -- '-- gdash7: ordinal x, axis formatting, identifiable marks\n'
gout="$(mktemp)"
if ! GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic tests/chart_gdash_test.bas >"$gout" 2>&1; then
    cat "$gout"; printf 'FAIL the fixture did not run\n'; status=1
elif grep -q MISMATCH "$gout"; then
    grep MISMATCH "$gout"; printf 'FAIL the renderer disagreed with what was asserted\n'; status=1
else
    gn=$(sed -n 's/^checks: //p' "$gout")
    # A coverage floor: a fixture that stops running its checks otherwise
    # passes by asserting nothing.
    if [ -z "$gn" ] || [ "$gn" -lt 35 ]; then
        printf 'FAIL only %s checks ran, wanted at least 35\n' "${gn:-0}"; status=1
    else
        printf 'PASS gdash7: %s checks (ordinal x, band alignment, axis labels, mark keys)\n' "$gn"
    fi
fi
rm -f "$gout"

if vg_available; then
    printf -- '-- valgrind over the golden program\n'
    if vg_run ./gbasic examples/chart_test.bas >/dev/null 2>/tmp/chart_vg.txt \
       && GBASIC_PATH=stdlib vg_run ./gbasic tests/chart_gdash_test.bas >/dev/null 2>/tmp/chart_vg.txt; then
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
