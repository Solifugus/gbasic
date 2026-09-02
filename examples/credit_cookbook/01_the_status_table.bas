' Recipe 1 — The input is a status table, not a list of loans.
'
' `lending` answers questions about ONE loan. `credit` answers questions about
' a BOOK — is this year's lending worse than last year's, where did the 30-day
' bucket go, what did we lose — and none of those is arithmetic about a
' balance. They are questions about STATES OVER TIME.
'
' So the input is one row per loan per observation date:
'
'     { id:, opened:, as_of:, status:, balance: }
'
' Two reasons, and neither is taste. COST: `lending.apply` is a fold, so a
' 5,000-loan book over 36 month-ends is 180,000 folds. PROVENANCE: real
' portfolio data ARRIVES in this shape — it is what a servicer extract and the
' published Fannie Mae and Freddie Mac datasets look like. A library that could
' only read our own `lending` loans could not be pointed at a real book.
'
' `credit.observe` is the bridge, so our own machinery is a producer of the
' table rather than a special case the analytics know about.
program main()
  load credit
  load lending

  jan {date}= "2026-01-01"
  apr {date}= "2026-04-01"
  amount {USD}= "12000.00"

  ' Six loans. Three written in January, three in April.
  book = []
  append(book, entry("L1", loan_from(jan, amount), 11))
  append(book, entry("L2", loan_from(jan, amount), 3))
  append(book, entry("L3", loan_from(jan, amount), 11))
  append(book, entry("L4", loan_from(apr, amount), 8))
  append(book, entry("L5", loan_from(apr, amount), 8))
  append(book, entry("L6", loan_from(apr, amount), 8))

  ' Twelve month-ends.
  observed_on = []
  for k = 0 to 11
    append(observed_on, jan + (1 month) * k)
  next

  table = credit.observe(book, observed_on, "mba")

  ' `check` validates the whole table once, so a table one function accepts
  ' cannot be one another silently mis-reads. It returns the row count.
  print "rows in the table: " + string(credit.check(table))
  print "  not 72 — a loan contributes a row only from the month it was"
  print "  written. Emitting rows before origination would put unwritten"
  print "  loans in `current` and inflate every denominator behind them."

  print ""
  print "the loans, on 2026-12-01:"
  dec {date}= "2026-12-01"
  for each r in table
    if r.as_of = dec then
      print "  " + r.id + "  " + fill(r.status, 14) + string(r.balance)
    end if
  next

  ' L2 stopped paying after three instalments. Watch it walk the ladder.
  print ""
  print "L2, which paid three times and stopped:"
  for each r in table
    if r.id = "L2" then
      print "  " + ymd(r.as_of) + "  " + r.status
    end if
  next
end program

function loan_from(opened, amount)
  load lending
  return lending.loan({ principal: amount, rate: 0.09, term: 24,
                        opened: opened, basis: "amortized",
                        waterfall: "fees_interest_principal",
                        day_count: "30/360" })
end function

function entry(id, l, how_many)
  load lending
  due = lending.payment(l)
  evs = []
  for k = 1 to how_many
    append(evs, { on: l.opened + (1 month) * k, kind: "payment", amount: due })
  next
  return { id: id, loan: l, events: evs }
end function

function ymd(d)
  return string(d.year) + "-" + two(d.month) + "-" + two(d.day)
end function

function two(n)
  if n < 10 then
    return "0" + string(n)
  end if
  return string(n)
end function

function fill(s, w)
  out = s
  while len(out) < w
    out = out + " "
  end while
  return out
end function
