#!/usr/bin/env bash
# L3 -- consolidation (stdlib/consolidate.bas over docs/xlsx_design.md §6): many
# tapes with the same MEANING and a different SURFACE, merged into one frame.
#
# Needs GBASIC_PATH because consolidate.bas loads frame.bas.
#
# Tiers:
#   1. GOLDEN -- examples/xlsx_consolidate_test.bas over tapes.xlsx: four
#      sources disagreeing on column names, money representation and rate
#      SCALE, plus one missing a required column.
#   2. ARITHMETIC -- the consolidated balances must sum to a figure derived
#      from the four tapes' own values, which also proves the cents survive
#      (`print` shows 23750.2 for 23750.25, so a display check would miss it).
#   3. THE 100x TRAP -- every normalised rate must land inside a plausible
#      band. This is the tier that matters: 5.25 and 0.0475 are the same kind
#      of thing on scales 100x apart, and a mis-inferred scale produces a
#      number that is entirely plausible in isolation. A band check catches
#      what no golden of a single value would.
#   4. HONESTY -- the ambiguous source must be REPORTED as ambiguous, and
#      declaring the scale must change the report from a guess to a
#      declaration. Inference that does not admit its own uncertainty is worse
#      than none.
#   5. REJECTION -- a source missing a required column is rejected by name, not
#      emitted with unknowns, because a tape silently short a balance column
#      understates the pool.
#   6. VALGRIND.
set -u
cd "$(dirname "$0")/.."

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_consolidate: build failed\n'
    exit 1
fi

out=$(mktemp); err=$(mktemp); tmp=$(mktemp -d)
trap 'rm -rf "$out" "$err" "$tmp"' EXIT
export GBASIC_PATH=stdlib
status=0

printf 'program main(args)\n  print xlsx.open("examples/fixtures/xlsx/tapes.xlsx")\nend program\n' >"$tmp/probe.bas"
if ! ./gbasic "$tmp/probe.bas" >/dev/null 2>"$err"; then
    if grep -q 'not available in this build' "$err"; then
        printf 'SKIP run_consolidate (built without zlib or libxml2)\n'
        exit 0
    fi
fi

printf -- '-- golden: four disagreeing tapes become one frame\n'
if timeout 120 ./gbasic examples/xlsx_consolidate_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_consolidate_test.out "$out"; then
        printf 'PASS examples/xlsx_consolidate_test.bas\n'
    else
        printf 'FAIL examples/xlsx_consolidate_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_consolidate_test.bas (exit)\n'
    cat "$err"
    status=1
fi

cat >"$tmp/sum.bas" <<'EOF'
program main(args)
  load grid
  load consolidate
  wb = xlsx.open("examples/fixtures/xlsx/tapes.xlsx")
  srcs = []
  for each s in xlsx.sheets(wb)
    r = grid.extract(grid.of(wb, s), { header_row: 1 })
    append(srcs, { name: s, frame: r.frame })
  end for
  spec = { columns: {
             loan_id: { from: ["Loan #", "Note ID", "loan_number"], kind: "text", required: true },
             balance: { from: ["Balance", "Principal", "Amount"], kind: "money", required: true },
             rate:    { from: ["Int Rate", "Interest Rate", "Rate (%)"], kind: "percent" } } }
  res = consolidate.merge(srcs, spec)
  print sum(res.frame["balance"]) = 265550.75
  ok = true
  for each v in res.frame["rate"]
    if v < 0.01 or v > 0.15 then
      ok = false
    end if
  end for
  print ok
  print join(res.rejected, ",")
end program
EOF
sum_res=$(timeout 60 ./gbasic "$tmp/sum.bas" 2>/dev/null | tr '\n' ' ')
case "$sum_res" in
  "true true CU_D "*)
    printf 'PASS balances sum exactly (cents survive), rates all in band, CU_D rejected\n' ;;
  *)
    printf 'FAIL expected "true true CU_D", got "%s"\n' "$sum_res"
    status=1 ;;
esac

# The 100x trap, asserted directly rather than inferred from the band: CU_A
# writes 5.25 and CU_B writes 0.0475, and both must arrive as fractions.
cat >"$tmp/scale.bas" <<'EOF'
program main(args)
  load consolidate
  print consolidate.to_percent(5.25, "whole")
  print consolidate.to_percent(0.0475, "fraction")
  print consolidate.to_percent("6.0 %", "fraction")
  print consolidate.infer_percent_scale([5.25, 4.875]).scale
  print consolidate.infer_percent_scale([0.0475, 0.0525]).certain
end program
EOF
sc_res=$(timeout 60 ./gbasic "$tmp/scale.bas" 2>/dev/null | tr '\n' ' ')
if [ "$sc_res" = "0.0525 0.0475 0.06 whole false " ]; then
    printf 'PASS percent scale: whole and fraction both land on the same scale; ambiguity admitted\n'
else
    printf 'FAIL percent scale: got "%s"\n' "$sc_res"
    status=1
fi

if command -v valgrind >/dev/null 2>&1; then
    if valgrind --error-exitcode=9 --leak-check=full --errors-for-leak-kinds=definite -q \
        ./gbasic examples/xlsx_consolidate_test.bas >/dev/null 2>"$err"; then
        printf 'PASS valgrind examples/xlsx_consolidate_test.bas\n'
    else
        printf 'FAIL valgrind\n'; head -20 "$err"; status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
