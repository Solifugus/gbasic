' Recipe 10 — Merge several sheets that mean the same thing but look different.
'
' Four loan tapes from four sources. Same meaning, different column names,
' different money formatting, one of them missing a column it must have.
' `consolidate.merge` maps them onto one schema you declare.

program main(args)
  load grid
  load consolidate
  load frame

  wb = xlsx.open("examples/fixtures/xlsx/tapes.xlsx")

  sources = []
  for each s in xlsx.sheets(wb)
    r = grid.extract(grid.of(wb, s), { header_row: 1 })
    print s + ": " + join(frame.columns(r.frame), " | ")
    append(sources, { name: s, frame: r.frame })
  end for

  ' `from` lists the aliases a column may arrive under. Matching is fuzzy --
  ' names are reduced to letters and digits -- so "Rate (%)" finds "rate".
  ' `required: true` means a source LACKING that column is rejected outright
  ' rather than emitted with blanks: a tape missing its balance understates
  ' the pool, and an understated pool just looks like a small one.
  spec = { columns: {
             loan_id: { from: ["Loan #", "Note ID", "loan_number"], kind: "text", required: true },
             balance: { from: ["Balance", "Principal", "Amount"], kind: "money", required: true },
             rate:    { from: ["Int Rate", "Interest Rate", "Rate (%)"], kind: "percent" } },
           source_column: "tape" }

  res = consolidate.merge(sources, spec)
  print ""
  print "ok       = " + res.ok + "   (false: a source was rejected)"
  print "accepted = " + join(res.accepted, ", ")
  print "rejected = " + join(res.rejected, ", ")

  print ""
  print "what it decided, and why:"
  for each n in res.notes
    print "  " + n
  end for

  ' THE TRAP is percent scale: 5.25 and 0.0475 are the same kind of thing 100x
  ' apart and nothing in the file says which. A written % settles it; otherwise
  ' the judgement is made per COLUMN from all values at once, and a column that
  ' could be read either way is reported AMBIGUOUS rather than guessed. Pass
  ' `scale:` to remove the guess.
  print ""
  print "every row carries its source, because an untraceable figure is not auditable:"
  for each row in frame.to_rows(res.frame)
    print "  " + row.tape + "  " + row.loan_id + "  balance " + row.balance + "  rate " + row.rate
  end for
end program
