#!/usr/bin/env bash
# ARI Phase 2 — stdlib/ari.bas, the anchor-relative report parser.
# Design: docs/text_design.md §4-§5, §5.1, §13.H. Syntax: docs/ari_spec_language.md.
#
# Tiers:
#   1. GOLDEN -- examples/ari_teller_test.bas parses BOTH committed fixtures.
#      The hand-made one carries the irregularities (recurring page header,
#      summary fields in a different ORDER per teller, a heading 4 columns
#      adrift of its data, a negative one column wider than the positives,
#      `Teller #:` vs `Teller#:`, a malformed amount); the generated one carries
#      form feeds, three branches, and a DIFFERENT MONEY DIALECT PER BRANCH that
#      the spec never names. Money is asserted as integer CENTS, because `print`
#      renders ~6 significant digits and would hide a lost cent above $9,999.99
#      (/DOGFOOD.md 2026-08-01).
#   2. GENERATOR DRIFT -- regenerate the committed sample and require it
#      byte-identical. Promised in examples/fixtures/ari/MANIFEST.md: without it
#      the generator and the fixture it produced can silently diverge, and the
#      golden would then be testing a file nothing can reproduce.
#   3. FURNITURE INDEPENDENCE -- the same report, paginated three different ways,
#      must yield the SAME parse. This is the §13.H claim stated as a test: a
#      page break falls wherever lines-per-page puts it, including mid-section,
#      so if furniture removal were entangled with section location at all, page
#      height would change the answer. Nothing else in the suite would catch
#      that, because any single pagination looks fine on its own.
#   4. VALGRIND -- the golden under valgrind.
#
# Plus two tiers that are not about a fixture: the LIMITATIONS register held to
# the truth by running it, and `ari.trace` -- the span-level claimed/unclaimed
# surface (tests/ari_trace_test.bas), which is what closed C1 and with it the
# two §8 scoring measures `ari_discover` had reported `unknown` since Phase 1.
#
# Headless, GI-independent, no display, no network. Never skips (bar valgrind).
set -u

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_ari: build failed\n'
    exit 1
fi

out=$(mktemp)
err=$(mktemp)
tmp=$(mktemp -d)
trap 'rm -rf "$out" "$err" "$tmp"' EXIT

status=0

# --- Tier 1: golden ------------------------------------------------------------
printf -- '-- golden\n'
if GBASIC_PATH=stdlib timeout 120 ./gbasic examples/ari_teller_test.bas \
        >"$out" 2>"$err" </dev/null; then
    if diff -u examples/ari_teller_test.out "$out"; then
        printf 'PASS examples/ari_teller_test.bas\n'
    else
        printf 'FAIL examples/ari_teller_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/ari_teller_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# --- Tier 1b: the delinquency fixture ------------------------------------------
# The constructs teller_totals cannot reach: down/up line offsets, distance
# RANGES (the gap after REMARKS: is 1, 2, 2 and 3 lines across four branches, so
# no exact distance matches all of them), flush, and using/custom `type`. Also
# the date contract: run undeclared, the ambiguous minority must come back
# `unknown` WITH diagnostics rather than guessed; run with `using date:`, every
# date must settle AND the ones already settled undeclared must not change.
printf -- '-- golden: delinquency (down/up/ranges/flush/using)\n'
if GBASIC_PATH=stdlib timeout 120 ./gbasic examples/ari_delinquency_test.bas \
        >"$out" 2>"$err" </dev/null; then
    if diff -u examples/ari_delinquency_test.out "$out"; then
        printf 'PASS examples/ari_delinquency_test.bas\n'
    else
        printf 'FAIL examples/ari_delinquency_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/ari_delinquency_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# A guess must never reach the value. If ambiguous dates ever start resolving
# without a declaration, this fails -- and nothing in the golden would say why,
# because a plausible wrong date looks exactly like a right one.
if grep -q 'ambiguous-date' examples/ari_delinquency_test.out; then
    printf 'PASS ambiguous dates refused rather than guessed\n'
else
    printf 'FAIL ambiguous dates no longer reported -- is something guessing?\n'
    status=1
fi

# --- Tier 2: the generator and its committed sample must not drift -------------
printf -- '-- generator drift\n'
if timeout 120 ./gbasic tools/gen_teller_report.bas 3 3 66 1 42 >"$out" 2>"$err" </dev/null; then
    if diff -q examples/fixtures/ari/teller_totals_generated.rpt "$out" >/dev/null; then
        printf 'PASS teller fixture reproduces byte-identically\n'
        if timeout 120 ./gbasic tools/gen_delinquency_report.bas 2 2 60 1 7 >"$out" 2>"$err" </dev/null \
           && diff -q examples/fixtures/ari/delinquency.rpt "$out" >/dev/null; then
            printf 'PASS delinquency fixture reproduces byte-identically\n'
        else
            printf 'FAIL delinquency fixture differs from its generator\n'
            status=1
        fi
    else
        printf 'FAIL generated fixture differs from tools/gen_teller_report.bas output\n'
        printf '     regenerate with: ./gbasic tools/gen_teller_report.bas 3 3 66 1 42 > examples/fixtures/ari/teller_totals_generated.rpt\n'
        status=1
    fi
else
    printf 'FAIL generator (exit)\n'
    cat "$err"
    status=1
fi

# --- Tier 3: the parse must not depend on where the pages break ----------------
#
# Same content, three page heights, plus an unpaginated control. Page furniture
# is stripped before anchoring (§13.H), so all four must agree. A page height of
# 20 guarantees breaks land inside teller blocks and detail tables rather than
# tidily between them.
printf -- '-- furniture independence (same report, different pagination)\n'
cat >"$tmp/probe.bas" <<'EOF'
program main(args)
  load ari
  sp = []
  append(sp, "page:")
  append(sp, "    break: formfeed")
  append(sp, "    drop: 2")
  append(sp, "")
  append(sp, "section report:")
  append(sp, "    section branches repeats starts(/^Branch: /):")
  append(sp, "        field branch_no: right of \"Branch:\" as integer")
  append(sp, "        section tellers repeats starts(/^Teller: /):")
  append(sp, "            field teller_no: right of \"Teller #:\" as integer")
  append(sp, "            field beginning_cash: right of \"Beginning Cash\" as money")
  append(sp, "            field ending_cash: right of \"Ending Cash\" as money")
  spec = join(sp, "\n")

  f{file} = args[0]
  report = join(read_lines(f), "\n")
  r = ari.parse(report, spec)
  if not r.ok then
    print "PARSE-FAILED " + r.message
    return
  end if
  for each b in r.value.branches
    for each t in b.tellers
      print b.branch_no + "/" + t.teller_no + "/" + (t.beginning_cash * 100) + "/" + (t.ending_cash * 100)
    end for
  end for
end program
EOF

base=""
for page in 0 20 45 66; do
    if [ "$page" = "0" ]; then
        ./gbasic tools/gen_teller_report.bas 3 3 100000 0 42 >"$tmp/r.rpt" 2>/dev/null
        label="unpaginated"
    else
        ./gbasic tools/gen_teller_report.bas 3 3 "$page" 1 42 >"$tmp/r.rpt" 2>/dev/null
        label="page=$page"
    fi
    if ! GBASIC_PATH=stdlib timeout 120 ./gbasic "$tmp/probe.bas" "$tmp/r.rpt" \
            >"$out" 2>"$err" </dev/null; then
        printf 'FAIL furniture %-12s (exit)\n' "$label"
        cat "$err"
        status=1
        continue
    fi
    got=$(md5sum <"$out" | cut -d' ' -f1)
    n=$(wc -l <"$out")
    if grep -q PARSE-FAILED "$out"; then
        printf 'FAIL furniture %-12s parse failed\n' "$label"
        status=1
        continue
    fi
    if [ -z "$base" ]; then
        base=$got
        printf 'PASS furniture %-12s %s rows (reference)\n' "$label" "$n"
    else
        if [ "$got" = "$base" ]; then
            printf 'PASS furniture %-12s identical parse\n' "$label"
        else
            printf 'FAIL furniture %-12s parse DIFFERS -- page height changed the answer\n' "$label"
            status=1
        fi
    fi
done

# --- Tier 3b: the limitations register, held to the truth by RUNNING it --------
#
# docs/ari_limitations.md records what `ari` does NOT do, or does wrongly. Every
# entry is a NEGATIVE CONTROL: the probe asserts the limitation still holds and
# goes RED WHEN IT IS FIXED, naming the entry to strike.
#
# WHY A REGISTER RATHER THAN FIXES. `ari_discover` is being built against a
# corpus this project generated, so every form discovery trips on is a
# temptation to change `ari` -- and after enough of those, `ari` is shaped to one
# invented corpus rather than to real reports. The register is what lets a
# deficiency be RECORDED without being reactively fixed, and swept generally at
# the end.
#
# WHY IT MUST BE EXECUTABLE. Five of fourteen entries in DOGFOOD.md were FALSE
# when run_limitations.sh was written -- fixed by shipped work, still cited as
# design justification, and not catchable by reading. A register that is only
# prose rots the same way.
#
# THE COVERAGE TRIPWIRE below is the other half: an entry added to the document
# and not probed is an entry that can quietly become false, which is the exact
# failure this tier exists to prevent.
if GBASIC_PATH=stdlib ./gbasic tests/ari_limitations_test.bas >"$out" 2>"$err"; then
    mism="$(sed -n 's/^mismatches: //p' "$out")"
    checks="$(sed -n 's/^checks: //p' "$out")"
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 30 ]; then
        printf 'PASS limitations   %s probes, every recorded limitation still holds\n' "$checks"
    else
        printf 'FAIL limitations   %s checks, %s mismatches\n' "$checks" "$mism"
        grep MISMATCH "$out" | sed 's/^/     /'
        printf '     A limitation that no longer holds is FIXED -- strike its entry\n'
        printf '     from docs/ari_limitations.md and remove its probe.\n'
        status=1
    fi
else
    printf 'FAIL limitations   probe program exited nonzero\n'
    tail -10 "$err"
    status=1
fi

# PROBE L0 -- "no corpus of real print-image reports exists".
#
# NOT A BEHAVIOUR, so it cannot be probed by running `ari`; it is a fact about
# what this project HOLDS. Asserted structurally instead, and it is a real
# negative control: examples/fixtures/ari/MANIFEST.md opens by declaring every
# file in it SYNTHETIC, so this check goes RED the day somebody adds a report
# produced by a real system -- which is exactly when L0 should be struck.
if grep -q 'Every file in this directory is SYNTHETIC' examples/fixtures/ari/MANIFEST.md    && grep -q 'Every file in this directory is SYNTHETIC' examples/fixtures/ari_discover/MANIFEST.md; then
    printf 'PASS limitations   L0 still holds: every ARI fixture is declared synthetic
'
else
    printf 'FAIL limitations   an ARI fixture corpus no longer declares itself synthetic --
'
    printf '                   if a real report has been added, strike L0 from
'
    printf '                   docs/ari_limitations.md and remove this probe.
'
    status=1
fi

# --- Tier 3c: `using`, the scoped override, and the remedy `ari` itself prints -
#
# SEPARATE FROM THE REGISTER TIER because these are struck, not recorded: the
# probes above assert a limitation STILL HOLDS, and these assert three that no
# longer do. A struck entry with nothing behind it is just a deleted one.
out2=$(mktemp)
if GBASIC_PATH=stdlib ./gbasic tests/ari_using_test.bas >"$out2" 2>&1; then
    mism2="$(sed -n 's/^mismatches: //p' "$out2")"
    checks2="$(sed -n 's/^checks: //p' "$out2")"
    if [ "$mism2" = "0" ] && [ "${checks2:-0}" -ge 24 ]; then
        printf 'PASS using         %s checks, 0 mismatches\n' "$checks2"
    else
        printf 'FAIL using         %s checks, %s mismatches\n' "$checks2" "$mism2"
        grep MISMATCH "$out2" || true
        status=1
    fi
else
    printf 'FAIL using         probe program exited nonzero\n'
    tail -10 "$out2"
    status=1
fi

# --- Tier 3d: the span report ---------------------------------------------------
#
# `ari.trace` -- which source spans the specification claimed, and what stayed
# unclaimed. Limitation C1, struck 2026-09-18. Self-checking rather than golden,
# and here that is forced: every defect this surface can have is a PLAUSIBLE
# SUBSTRING, so a golden would record an offset one column out as expected.
out3=$(mktemp)
if GBASIC_PATH=stdlib timeout 300 ./gbasic tests/ari_trace_test.bas >"$out3" 2>&1; then
    mism3="$(grep -c '^MISMATCH' "$out3")"
    checks3="$(grep -c '^ok' "$out3")"
    if [ "$mism3" = "0" ] && [ "${checks3:-0}" -ge 39 ]; then
        printf 'PASS trace         %s checks, 0 mismatches\n' "$checks3"
    else
        printf 'FAIL trace         %s checks, %s mismatches\n' "$checks3" "$mism3"
        grep MISMATCH "$out3" || true
        status=1
    fi
else
    printf 'FAIL trace         probe program exited nonzero\n'
    tail -10 "$out3"
    status=1
fi
rm -f "$out3"

# COVERAGE, both directions: every id in the register must have a probe, and
# every probe must name an id that is in the register.
# `[0-9]+`, not `[0-9]`: with a single digit, B10 reads as `B1` followed by `0`
# and the row never matches, so a two-digit entry would escape coverage
# entirely -- found adding B10 and B11.
reg_ids=$(grep -oE '^\| \*\*[A-Z][0-9]+\*\*' docs/ari_limitations.md | tr -d '|* ' | sort -u)
if [ -z "$reg_ids" ]; then
    printf 'FAIL limitations   docs/ari_limitations.md lists no entries -- has it been emptied?\n'
    status=1
else
    missing=""
    for id in $reg_ids; do
        if ! grep -qE "\"$id " tests/ari_limitations_test.bas \
           && ! grep -qE "^# PROBE $id\b" "$0"; then
            missing="$missing $id"
        fi
    done
    if [ -n "$missing" ]; then
        printf 'FAIL limitations   recorded but never probed:%s\n' "$missing"
        status=1
    else
        printf 'PASS limitations   all %s recorded entries are probed\n' "$(printf '%s\n' $reg_ids | wc -l)"
    fi
fi

# --- Tier 4: valgrind ----------------------------------------------------------
#
# The trace fixture goes through it as well as the golden, because the claims
# machinery is the only part of this library that allocates per FIELD PER ROW
# and the golden never calls it. It costs almost nothing -- 0.18s native.
if vg_available; then
    if GBASIC_PATH=stdlib vg_run ./gbasic tests/ari_trace_test.bas \
            >"$out" 2>"$err" </dev/null; then
        if [ "$(grep -c '^MISMATCH' "$out")" = "0" ]; then
            printf 'PASS valgrind tests/ari_trace_test.bas\n'
        else
            printf 'FAIL valgrind trace fixture mismatched under valgrind\n'
            status=1
        fi
    else
        printf 'FAIL valgrind tests/ari_trace_test.bas\n'
        tail -20 "$err"
        status=1
    fi
    if GBASIC_PATH=stdlib vg_run ./gbasic examples/ari_teller_test.bas \
            >"$out" 2>"$err" </dev/null; then
        if diff -q examples/ari_teller_test.out "$out" >/dev/null; then
            printf 'PASS valgrind examples/ari_teller_test.bas\n'
        else
            printf 'FAIL valgrind (output differs under valgrind)\n'
            status=1
        fi
    else
        printf 'FAIL valgrind examples/ari_teller_test.bas\n'
        tail -20 "$err"
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit "$status"
