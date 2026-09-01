#!/usr/bin/env bash
# L2 -- ARI-for-grids (stdlib/grid.bas over docs/xlsx_design.md §5): turning a
# messy worksheet into clean frames.
#
# Needs GBASIC_PATH because grid.bas loads frame.bas, which is why this is its
# own runner rather than an entry in run_examples.sh.
#
# Tiers:
#   1. GOLDEN -- examples/xlsx_grid_test.bas over examples/fixtures/xlsx/messy.xlsx.
#      Every irregularity in that fixture is one §5 names as the reason the
#      layer exists: a title above the table, a two-row header whose parent is
#      written only above the first child (how Excel stores a merged cell),
#      subtotals interleaved with data, a trailing note, a second table of a
#      different width, and an empty spacer column inside the first table's
#      span. The correct answer is deliberately NOT what a naive
#      top-left-to-bottom-right reader produces.
#   2. ARITHMETIC CROSS-CHECK -- the assertion that actually matters. Summing an
#      extracted column must equal the figure the sheet's own TOTAL row claims.
#      A golden would happily record a subtotal absorbed as data; this cannot,
#      because absorbing one makes the sum too big. It is checked against the
#      sheet, not against a number written here.
#   3. HONESTY -- the automatic path must report LOW confidence on the messy
#      table and HIGH on the clean one. A detector that is confident about the
#      messy case is worse than no detector, since "automatic when safe" is the
#      whole contract.
#   4. NEGATIVE -- a spec matching nothing reports ok=false rather than
#      returning an empty frame that reads as "no data".
#   5. VALGRIND.
set -u
cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_grid: build failed\n'
    exit 1
fi

out=$(mktemp); err=$(mktemp); tmp=$(mktemp -d)
trap 'rm -rf "$out" "$err" "$tmp"' EXIT
export GBASIC_PATH=stdlib
status=0

printf 'program main(args)\n  print xlsx.open("examples/fixtures/xlsx/messy.xlsx")\nend program\n' >"$tmp/probe.bas"
if ! ./gbasic "$tmp/probe.bas" >/dev/null 2>"$err"; then
    if grep -q 'not available in this build' "$err"; then
        printf 'SKIP run_grid (built without zlib or libxml2)\n'
        exit 0
    fi
fi

printf -- '-- golden: a messy sheet becomes clean frames\n'
if timeout 120 ./gbasic examples/xlsx_grid_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_grid_test.out "$out"; then
        printf 'PASS examples/xlsx_grid_test.bas\n'
    else
        printf 'FAIL examples/xlsx_grid_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_grid_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# The arithmetic cross-check, run against the SHEET rather than a literal: the
# extracted column must sum to what the sheet's own TOTAL row says. Absorbing
# the subtotal would give 550 instead of 340 -- a plausible number.
cat >"$tmp/sum.bas" <<'EOF'
program main(args)
  load grid
  wb = xlsx.open("examples/fixtures/xlsx/messy.xlsx")
  g = grid.of(wb, "Report")
  r = grid.extract(g, { header_row: 4, header_rows: 2, drop_totals: true })
  print sum(r.frame["Q1 Units"]) = grid.at(g, 11, 1)
  print sum(r.frame["Q2 Value"]) = grid.at(g, 11, 5)
end program
EOF
sum_res=$(timeout 60 ./gbasic "$tmp/sum.bas" 2>/dev/null | tr '\n' ' ')
if [ "$sum_res" = "true true " ]; then
    printf 'PASS extracted columns sum to the sheet own TOTAL row (no subtotal absorbed)\n'
else
    printf 'FAIL extracted sums do not match the sheet TOTAL row: "%s"\n' "$sum_res"
    status=1
fi

# Honesty of the automatic path.
cat >"$tmp/conf.bas" <<'EOF'
program main(args)
  load grid
  wb = xlsx.open("examples/fixtures/xlsx/messy.xlsx")
  messy = ""
  for each t in grid.tables(grid.of(wb, "Report"))
    if t.first_row = 4 then
      messy = t.confidence
    end if
  end for
  clean = ""
  for each t in grid.tables(grid.of(wb, "Clean"))
    clean = t.confidence
  end for
  print messy
  print clean
end program
EOF
conf_res=$(timeout 60 ./gbasic "$tmp/conf.bas" 2>/dev/null | tr '\n' ' ')
if [ "$conf_res" = "low high " ]; then
    printf 'PASS automatic path: low confidence on the messy table, high on the clean one\n'
else
    printf 'FAIL confidence should be "low high", got "%s"\n' "$conf_res"
    status=1
fi

# A spec matching nothing must SAY so.
cat >"$tmp/neg.bas" <<'EOF'
program main(args)
  load grid
  wb = xlsx.open("examples/fixtures/xlsx/messy.xlsx")
  r = grid.extract(grid.of(wb, "Report"), { starts: "No Such Section" })
  print r.ok
  print r.message
end program
EOF
neg_res=$(timeout 60 ./gbasic "$tmp/neg.bas" 2>/dev/null | head -1)
if [ "$neg_res" = "false" ]; then
    printf 'PASS a spec matching nothing reports failure rather than an empty frame\n'
else
    printf 'FAIL a spec matching nothing should report ok=false, got "%s"\n' "$neg_res"
    status=1
fi

if vg_available; then
    if vg_run ./gbasic examples/xlsx_grid_test.bas >/dev/null 2>"$err"; then
        printf 'PASS valgrind examples/xlsx_grid_test.bas\n'
    else
        printf 'FAIL valgrind\n'
        head -20 "$err"
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
