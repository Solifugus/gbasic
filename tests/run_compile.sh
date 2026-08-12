#!/usr/bin/env bash
# L4 proper -- the FORMULA COMPILER (xlsx.to_sql, docs/xlsx_design.md §7 and
# §13.Q). A column formula is one transformation over a column, so it is
# lowered to SQL and run once over the whole table rather than evaluated per
# cell.
#
# Tiers:
#   1. GOLDEN -- what lowers, and what is refused with which reason.
#   2. THE ORACLE -- the tier that matters. The same workbook is evaluated cell
#      by cell by xlsx.evaluate AND loaded into SQLite and computed by the
#      compiled expression, then compared ROW BY ROW. This is the only check
#      that can catch a compiler which is self-consistently wrong, and it has
#      already earned its keep: it caught the compiler lowering AND to SQL
#      while the evaluator still reported AND as unimplemented, so the two
#      disagreed on every row of that column.
#   3. REFUSALS -- asserted individually, because each exists to prevent a
#      SPECIFIC plausible wrong answer: a row-spanning range is an aggregate
#      and changes cardinality; a reference to another row is a window
#      function; VLOOKUP is a join; a volatile function has no set-based
#      meaning. A compiler that quietly lowered any of these would produce a
#      number that looks right.
#   4. VALGRIND.
#
# Needs GBASIC_PATH (grid/dbframe) and sqlite.
set -u
cd "$(dirname "$0")/.."

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_compile: build failed\n'
    exit 1
fi

out=$(mktemp); err=$(mktemp); tmp=$(mktemp -d)
trap 'rm -rf "$out" "$err" "$tmp"' EXIT
export GBASIC_PATH=stdlib
status=0

printf 'program main(args)\n  load sqlite\n  print sqlite.connect(":memory:")\nend program\n' >"$tmp/probe.bas"
if ! ./gbasic "$tmp/probe.bas" >/dev/null 2>"$err"; then
    printf 'SKIP run_compile (sqlite not available in this build)\n'
    exit 0
fi

printf -- '-- golden: what lowers, what is refused\n'
if timeout 120 ./gbasic examples/xlsx_compile_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_compile_test.out "$out"; then
        printf 'PASS examples/xlsx_compile_test.bas\n'
    else
        printf 'FAIL examples/xlsx_compile_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_compile_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# ZERO disagreements against the interpreter, asserted apart from the golden --
# a golden records whatever we produce, including a regression.
cat >"$tmp/oracle.bas" <<'EOF'
program main(args)
  load grid
  load dbframe
  wb = xlsx.open("examples/fixtures/xlsx/formulacol.xlsx")
  r = grid.extract(grid.of(wb, "Loans"), { header_row: 1 })
  db = sqlite.connect(":memory:")
  dbframe.to_table(r.frame, db, "loans", { replace: true })
  m = { A: "id", B: "balance", C: "rate", D: "term", _row: 2 }
  bad = 0
  for each col in ["E", "F", "G"]
    c = xlsx.to_sql(xlsx.cell(wb, "Loans", col + "2").formula, m)
    if not c.ok then
      bad = bad + 1000
      continue
    end if
    rows = sqlite.query(db, "select id, (" + c.sql + ") v from loans order by id", [])
    i = 0
    while i < count(rows)
      a = xlsx.evaluate(wb, "Loans", col + (i + 2))
      b = rows[i].v
      same = string(a) = string(b)
      if is_number(a) and is_number(b) then
        same = abs(number(a) - number(b)) < 0.0000001
      end if
      if not same then
        bad = bad + 1
      end if
      i = i + 1
    end while
  end for
  print bad
  print sqlite.close(db)
end program
EOF
o_res=$(timeout 60 ./gbasic "$tmp/oracle.bas" 2>/dev/null | head -1)
if [ "$o_res" = "0" ]; then
    printf 'PASS compiled SQL agrees with the interpreter on every row of every column\n'
else
    printf 'FAIL compiler and interpreter disagree on %s row(s)\n' "$o_res"
    status=1
fi

# Each refusal, asserted by the SHAPE it must reject rather than by message
# text, so rewording a diagnostic does not silently disable the guard.
cat >"$tmp/ref.bas" <<'EOF'
program main(args)
  m = { A: "id", B: "balance", C: "rate", D: "term", _row: 2 }
  n = 0
  for each f in ["SUM(B2:B99)", "B3*2", "B1*2", "VLOOKUP(A2,X2:Y9,2,0)",
                 "SUMIF(B2:B99,\">0\",C2:C99)", "NOW()", "TODAY()",
                 "Other!B2", "MIN(B2)", "SomeName*2"]
    if xlsx.to_sql(f, m).ok then
      print "LOWERED WHEN IT SHOULD NOT: " + f
      n = n + 1
    end if
  end for
  ' and the supported subset must still lower
  for each f in ["B2*0.02", "IF(C2>0.05,B2*2,0)", "SUM(B2:D2)", "ROUND(B2*C2,2)"]
    if not xlsx.to_sql(f, m).ok then
      print "REFUSED WHEN IT SHOULD NOT: " + f
      n = n + 1
    end if
  end for
  print n
end program
EOF
r_res=$(timeout 60 ./gbasic "$tmp/ref.bas" 2>/dev/null | tail -1)
if [ "$r_res" = "0" ]; then
    printf 'PASS every out-of-phase shape refused; the supported subset still lowers\n'
else
    printf 'FAIL %s refusal/acceptance mismatch(es)\n' "$r_res"
    timeout 60 ./gbasic "$tmp/ref.bas" 2>/dev/null | head -5
    status=1
fi

if command -v valgrind >/dev/null 2>&1; then
    if valgrind --error-exitcode=9 --leak-check=full --errors-for-leak-kinds=definite -q \
        ./gbasic examples/xlsx_compile_test.bas >/dev/null 2>"$err"; then
        printf 'PASS valgrind examples/xlsx_compile_test.bas\n'
    else
        printf 'FAIL valgrind\n'; head -20 "$err"; status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
