' xlsx.to_sql -- the formula compiler (docs/xlsx_design.md §7, §13.Q).
'
' §7's headline: a business user's COLUMN formula is not a million cell
' evaluations, it is one transformation over a column. So
'     IF(C2>0.05, ROUND(B2*C2,2), 0)
' becomes
'     CASE WHEN "rate" > 0.05 THEN ROUND("balance" * "rate", 2) ELSE 0 END
' and the database runs it once over the whole table.
'
' The compiler is a SECOND PASS over the same lexer as the evaluator, with the
' same precedence chain, emitting text instead of computing values. Sharing the
' lexer is the point: a compiler that parsed the dialect slightly differently
' would disagree with the interpreter on inputs neither test covers -- and the
' interpreter is the oracle it is checked against.
'
' THE REFUSALS ARE THE FEATURE. §7's promise is deliberately narrow: lower the
' subset business users actually write for tabular data, and refuse the rest
' with a diagnostic rather than fall back to slow per-row evaluation. A range
' that spans ROWS is an aggregate and changes cardinality; a reference to
' another row is a window function. Lowering either as if it were a row
' expression produces a confident wrong number.

program main(args)
  load grid
  load dbframe
  load frame

  mapping = { A: "id", B: "balance", C: "rate", D: "term", F1: 0.02, _row: 2 }

  print "== what lowers =="
  for each f in ["B2*0.02",
                 "IF(C2>0.05, B2*0.02, 0)",
                 "ROUND(B2*C2, 2)",
                 "IF(AND(C2>0.04, B2>1000), \"high\", \"low\")",
                 "SUM(B2:D2)",
                 "B2*$F$1",
                 "IFERROR(B2/C2, 0)",
                 "-B2 + 50%",
                 "2^10",
                 "B2 & \"-\" & A2",
                 "MAX(B2, D2)",
                 "UPPER(A2)"]
    r = xlsx.to_sql(f, mapping)
    print "  " + f
    print "      " + r.sql
  end for

  print ""
  print "== what is refused, and why =="
  ' Each of these could be lowered to something plausible. That is exactly why
  ' they are refused instead.
  for each f in ["SUM(B2:B99)",
                 "B3*2",
                 "B1*2",
                 "VLOOKUP(A2, X:Y, 2, 0)",
                 "SUMIF(B2:B99, \">0\", C2:C99)",
                 "NOW()",
                 "Other!B2",
                 "MIN(B2)",
                 "SUM(B2:D2, B3:D3)"]
    r = xlsx.to_sql(f, mapping)
    print "  " + f
    print "      " + r.reason
  end for

  print ""
  print "== THE ORACLE: the compiled SQL must agree with the interpreter =="
  ' The assertion that matters, and the only one that can catch a compiler
  ' which is self-consistently wrong. The same workbook is (a) evaluated cell
  ' by cell by xlsx.evaluate and (b) loaded into SQLite and computed by the
  ' compiled expression, and the two are compared ROW BY ROW.
  '
  ' This tier has already earned its keep: it caught that the compiler lowered
  ' AND to SQL while the evaluator still reported AND as unimplemented, so the
  ' two disagreed on every row of that column.
  wb = xlsx.open("examples/fixtures/xlsx/formulacol.xlsx")
  r = grid.extract(grid.of(wb, "Loans"), { header_row: 1 })
  db = sqlite.connect(":memory:")
  dbframe.to_table(r.frame, db, "loans", { replace: true })

  total_bad = 0
  for each col in ["E", "F", "G"]
    f = xlsx.cell(wb, "Loans", col + "2").formula
    c = xlsx.to_sql(f, mapping)
    rows = sqlite.query(db, "select id, (" + c.sql + ") v from loans order by id", [])
    bad = 0
    i = 0
    while i < count(rows)
      interp = xlsx.evaluate(wb, "Loans", col + (i + 2))
      compiled = rows[i].v
      same = string(interp) = string(compiled)
      if is_number(interp) and is_number(compiled) then
        same = abs(number(interp) - number(compiled)) < 0.0000001
      end if
      if not same then
        bad = bad + 1
        print "    row " + (i + 2) + " DIFFERS: interpreter=" + interp + " sql=" + compiled
      end if
      i = i + 1
    end while
    total_bad = total_bad + bad
    print "  column " + col + " (" + f + ")"
    print "      " + count(rows) + " rows compared, " + bad + " disagreements"
  end for
  print "  total disagreements across all columns = " + total_bad

  print ""
  print "== and it runs set-based: one statement over the whole table =="
  ' The scaling story in one line. Not six evaluations -- one statement.
  fee = xlsx.to_sql(xlsx.cell(wb, "Loans", "E2").formula, mapping)
  q = "select count(*) n, round(sum(" + fee.sql + "), 2) fees from loans"
  row = sqlite.query(db, q, [])[0]
  print "  " + q
  print "  -> " + row.n + " loans, total fees " + row.fees

  print ""
  print "== the OTHER target: the same formula run in memory over a frame =="
  ' §7 compiles to one of two targets. xlsx.apply is the in-memory one: one
  ' call produces a whole column, with no workbook, no sheet snapshot and no
  ' per-cell dispatch. It is a vectorised PASS, not a compiled expression --
  ' the formula is re-lexed per row, because the evaluator has no retained AST
  ' yet -- so the claim made for it is only the one that can be measured.
  three_way = 0
  for each col in ["E", "F", "G"]
    f = xlsx.cell(wb, "Loans", col + "2").formula
    vec = xlsx.apply(f, mapping, r.frame)
    c = xlsx.to_sql(f, mapping)
    sqlrows = sqlite.query(db, "select id, (" + c.sql + ") v from loans order by id", [])
    bad = 0
    i = 0
    while i < count(vec)
      a = xlsx.evaluate(wb, "Loans", col + (i + 2))
      b = vec[i]
      cc = sqlrows[i].v
      same = string(a) = string(b) and string(a) = string(cc)
      if is_number(a) and is_number(b) and is_number(cc) then
        same = abs(number(a) - number(b)) < 0.0000001
        if same then
          same = abs(number(a) - number(cc)) < 0.0000001
        end if
      end if
      if not same then
        bad = bad + 1
        print "    row " + (i + 2) + " interp=" + a + " frame=" + b + " sql=" + cc
      end if
      i = i + 1
    end while
    three_way = three_way + bad
    print "  column " + col + ": " + count(vec) + " rows, interpreter = frame = SQL, " + bad + " disagreements"
  end for
  print "  THREE-WAY disagreements across all columns = " + three_way

  sqlite.close(db)
end program
