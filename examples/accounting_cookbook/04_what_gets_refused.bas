' Recipe 4 — What is refused, and why each one matters.
'
' Every mistake below leaves a ledger that BALANCES and a statement that looks
' perfectly ordinary. That is the whole reason each is refused where the entry
' is written, rather than reported by something downstream that cannot say
' which entry did it.
'
' NOTE THE SHAPE OF THE ERROR HANDLING. `on error goto next` is FRAME-SCOPED
' and so is acknowledging one: `if error then` in a helper function does not
' claim the error pending in its caller. Writing this recipe with a `show()`
' helper left every error unacknowledged, and the second raise then escaped the
' frame — which is gBASIC's anti-silence rule working exactly as designed.
' Acknowledge in the same frame that armed the handler.
program main()
  load accounting

  books = accounting.chart([
    { code: "1000", name: "Cash", kind: "asset" },
    { code: "4000", name: "Fees", kind: "revenue" }
  ])
  when {date}= "2026-05-01"
  amt {USD}= "100.00"
  other {USD}= "60.00"
  eur {EUR}= "100.00"

  on error goto next

  ' Debits and credits that do not agree — reported WITH the difference, so you
  ' can see what is missing instead of hunting for it.
  x = accounting.entry(books, when, "Off by forty",
        [{ account: "1000", debit: amt }, { account: "4000", credit: other }])
  print "unbalanced:"
  print "  " + error.message
  error.clear()

  ' An account not in the chart. Without this a typo creates a phantom account
  ' that appears in no statement anyone reads.
  x = accounting.entry(books, when, "Typo",
        [{ account: "1oo0", debit: amt }, { account: "4000", credit: amt }])
  print "account not in the chart:"
  print "  " + error.message
  error.clear()

  ' A line names exactly one side.
  x = accounting.entry(books, when, "Both",
        [{ account: "1000", debit: amt, credit: amt }, { account: "4000", credit: amt }])
  print "a line with both sides:"
  print "  " + error.message
  error.clear()

  x = accounting.entry(books, when, "Neither",
        [{ account: "1000" }, { account: "4000", credit: amt }])
  print "a line with neither:"
  print "  " + error.message
  error.clear()

  ' Currencies do not mix. An entry must balance in EACH currency separately —
  ' and the money type refuses to add across them in the first place, so the
  ' commonest accounting bug cannot even be expressed.
  x = accounting.entry(books, when, "Mixed",
        [{ account: "1000", debit: amt }, { account: "4000", credit: eur }])
  print "mixed currencies:"
  print "  " + error.message
  error.clear()

  ' Two accounts on one code.
  x = accounting.chart([{ code: "1000", name: "A", kind: "asset" },
                        { code: "1000", name: "B", kind: "asset" }])
  print "duplicate account code:"
  print "  " + error.message
  error.clear()

  on error stop

  ' THE CONTROL: the nearest legal neighbour of every refusal above still
  ' works. A library that refused everything would satisfy the list.
  good = accounting.entry(books, when, "Fine",
           [{ account: "1000", debit: amt }, { account: "4000", credit: amt }])
  print ""
  print "and a correct entry is accepted: " + string(count(good.lines)) + " lines"
end program
