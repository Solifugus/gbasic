' consolidate.bas -- L3: many tapes with the same meaning, one clean frame.
'
' docs/xlsx_design.md §6, the participation-loan / CECL problem. Four sources
' that mean the same thing and look nothing alike:
'
'   CU_A  "Loan #" / "Balance" / "Int Rate"      numbers; rate 5.25  = 5.25%
'   CU_B  "Note ID" / "Principal" / ...          money as "$1,500.00";
'                                                rate 0.0475 = 4.75%
'   CU_C  "loan_number" / "Amount" / "Rate (%)"  "(1,200.00)" negative;
'                                                rate "6.0 %"
'   CU_D  no balance column at all
'
' THE RULE, inherited from ARI: INFER TO ADVISE, DECLARE TO PARSE. The trap
' here is the rate scale -- 5.25 and 0.0475 are the same KIND of thing written
' on scales 100x apart, and nothing in either file says which. A column is
' decidable where a cell is not (a fraction cannot exceed 1), so the judgement
' is made per column and REPORTED; where even the column is ambiguous it says
' so rather than picking quietly.

program main(args)
  load grid
  load consolidate
  load frame

  wb = xlsx.open("examples/fixtures/xlsx/tapes.xlsx")

  print "== the four tapes, as they arrive =="
  sources = []
  for each s in xlsx.sheets(wb)
    g = grid.of(wb, s)
    r = grid.extract(g, { header_row: 1 })
    print "  " + s + ": " + join(frame.columns(r.frame), " | ")
    append(sources, { name: s, frame: r.frame })
  end for

  spec = { columns: {
             loan_id: { from: ["Loan #", "Note ID", "loan_number"], kind: "text", required: true },
             balance: { from: ["Balance", "Principal", "Amount"], kind: "money", required: true },
             rate:    { from: ["Int Rate", "Interest Rate", "Rate (%)"], kind: "percent" } },
           source_column: "tape" }

  print ""
  print "== consolidating =="
  res = consolidate.merge(sources, spec)
  print "ok       = " + res.ok + "   (false because a source was rejected)"
  print "accepted = " + join(res.accepted, ", ")
  print "rejected = " + join(res.rejected, ", ")
  print ""
  print "what it decided, and why:"
  for each n in res.notes
    print "  " + n
  end for

  print ""
  print "== one frame, one set of names, provenance on every row =="
  ' A consolidated figure nobody can trace back to its tape is not auditable,
  ' so the source column is not optional.
  print "columns: " + join(frame.columns(res.frame), " | ")
  for each row in frame.to_rows(res.frame)
    print "  " + row.tape + "  " + row.loan_id + "  balance " + row.balance + "  rate " + row.rate
  end for

  print ""
  print "== every rate is now on ONE scale, whatever the tape wrote =="
  ' The payoff. 5.25, 0.0475 and "6.0 %" all arrive as fractions, so they can
  ' be compared and averaged; before consolidation, averaging them would give
  ' a number roughly 100x too large and entirely plausible.
  for each row in frame.to_rows(res.frame)
    print "  " + row.loan_id + " -> " + round(row.rate * 100, 3) + "%"
  end for

  print ""
  print "== declaring beats inferring, and the report shows the difference =="
  ' CU_B's rates are all below 1, so the column alone cannot say whether they
  ' are fractions or sub-1% whole percents. Declaring the scale removes the
  ' guess -- and the note changes from AMBIGUOUS to declared.
  spec2 = { columns: {
              ' Deliberately on the OLD `names:` spelling: it predates `from`
              ' being a legal record key (2026-08-13) and is still accepted, so
              ' this call is what keeps that compatibility path exercised.
              loan_id: { names: ["Note ID"], kind: "text", required: true },
              rate:    { names: ["Interest Rate"], kind: "percent", scale: "fraction" } } }
  only_b = []
  for each s in sources
    if s.name = "CU_B" then
      append(only_b, s)
    end if
  end for
  r2 = consolidate.merge(only_b, spec2)
  for each n in r2.notes
    print "  " + n
  end for

  print ""
  print "== the money forms, checked one by one =="
  ' The union a real report uses. Each of these is a form that appears in the
  ' fixture or in the wild, and each must land on the same number.
  print "  1500        -> " + consolidate.to_money(1500)
  print "  \"$1,500.00\" -> " + consolidate.to_money("$1,500.00")
  print "  \"(1,200.00)\"-> " + consolidate.to_money("(1,200.00)")
  print "  \"1,200.00-\" -> " + consolidate.to_money("1,200.00-")
  print "  \"9,000\"     -> " + consolidate.to_money("9,000")
  print "  \"n/a\"       -> " + consolidate.to_money("n/a")

  print ""
  print "== name matching is normalised, so punctuation and case do not matter =="
  print "  \"Rate (%)\" ~ \"rate\"        : " + (consolidate.normalize_name("Rate (%)") = consolidate.normalize_name("rate"))
  print "  \"Int. Rate\" ~ \"int rate\"   : " + (consolidate.normalize_name("Int. Rate") = consolidate.normalize_name("int rate"))
  print "  \"Loan #\" ~ \"loan\"          : " + (consolidate.normalize_name("Loan #") = consolidate.normalize_name("loan"))
end program
