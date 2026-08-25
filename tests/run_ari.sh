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
# Headless, GI-independent, no display, no network. Never skips (bar valgrind).
set -u

cd "$(dirname "$0")/.."

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

# --- Tier 4: valgrind ----------------------------------------------------------
if command -v valgrind >/dev/null 2>&1; then
    if GBASIC_PATH=stdlib valgrind --error-exitcode=9 --leak-check=full \
            --errors-for-leak-kinds=definite ./gbasic examples/ari_teller_test.bas \
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
