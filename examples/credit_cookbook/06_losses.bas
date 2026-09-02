' Recipe 6 — Charge-offs and recoveries: gross and net are different numbers.
'
' A CHARGE-OFF IS A DECISION, not a threshold crossing. The servicer wrote the
' balance off, on a date, and that is a fact in the record. `losses` READS the
' `charged_off` status; it never infers one from days past due. Inferring it
' would produce a loss figure the servicer's own books disagree with — and the
' books are the ones that get audited.
'
' GROSS AND NET ARE REPORTED SEPARATELY and never netted silently. They are
' different numbers used for different purposes, and quietly reporting one as
' the other halves a loss rate.
program main()
  load credit
  load lending

  jan {date}= "2026-01-01"
  amount {USD}= "12000.00"

  book = []
  for i = 1 to 6
    append(book, plain("G" + string(i), jan, amount, 11))
  next
  append(book, plain("X1", jan, amount, 2))
  append(book, plain("X2", jan, amount, 2))

  observed_on = []
  for k = 0 to 11
    append(observed_on, jan + (1 month) * k)
  next
  observed = credit.observe(book, observed_on, "mba")

  ' The servicer's decisions, applied on top of what the loans did. X1 was
  ' written off in October, X2 in November — both after months in the 120+
  ' bucket, but the DATE is the servicer's, not a rule's.
  oct {date}= "2026-10-01"
  nov {date}= "2026-11-01"
  dec {date}= "2026-12-01"
  recovered {USD}= "1500.00"

  table = []
  for each r in observed
    row = r
    if r.id = "X1" and r.as_of >= oct then
      row = { id: r.id, opened: r.opened, as_of: r.as_of,
              status: "charged_off", balance: r.balance }
    end if
    if r.id = "X2" and r.as_of >= nov then
      row = { id: r.id, opened: r.opened, as_of: r.as_of,
              status: "charged_off", balance: r.balance }
    end if
    ' A collection agency returned 1,500 on X1 in December. A recovery is
    ' attached to the row it arrived on.
    if r.id = "X1" and r.as_of = dec then
      row = { id: r.id, opened: r.opened, as_of: r.as_of,
              status: "charged_off", balance: r.balance, recovery: recovered }
    end if
    append(table, row)
  next

  l = credit.losses(table, { cohort_by: "quarter" })

  print "charge-offs by cohort"
  for each c in l.cohorts
    s = l.by_cohort[c]
    print "  " + c
    print "    accounts written off:  " + string(s.count)
    print "    gross charge-offs:     " + string(s.gross)
    print "    recoveries:            " + string(s.recoveries)
    print "    net charge-offs:       " + string(s.net)
  next

  print ""
  print "gross and net differ by the recoveries: "
  s = l.by_cohort[l.cohorts[0]]
  print "  " + string(s.gross - s.recoveries) + " = " + string(s.net)

  ' THE AMOUNT WRITTEN OFF IS THE BALANCE ON THE CHARGE-OFF ROW — the first
  ' one, so a loan carried at charged_off for months is counted once and at
  ' the figure the servicer actually wrote off.
  print ""
  print "each account is counted once: " + string(l.count) + " accounts"

  ' Two refusals worth seeing, because both would otherwise produce a
  ' plausible loss figure.
  on error goto next

  ' A charge-off with no balance. The amount written off IS the number; there
  ' is nothing to substitute for it.
  bare = []
  for each r in table
    if r.id = "X2" and r.as_of = nov then
      append(bare, { id: r.id, opened: r.opened, as_of: r.as_of,
                     status: "charged_off" })
    else
      append(bare, r)
    end if
  next
  x = credit.losses(bare, { })
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if

  ' Money arriving BEFORE the write-off is a payment, not a recovery. Calling
  ' it a recovery would understate the loss and overstate collections at once.
  mar {date}= "2026-03-01"
  early = []
  for each r in table
    if r.id = "X1" and r.as_of = mar then
      append(early, { id: r.id, opened: r.opened, as_of: r.as_of,
                      status: r.status, balance: r.balance, recovery: recovered })
    else
      append(early, r)
    end if
  next
  x = credit.losses(early, { })
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program

function plain(id, opened, amount, how_many)
  load lending
  l = lending.loan({ principal: amount, rate: 0.09, term: 24,
                     opened: opened, basis: "amortized",
                     waterfall: "fees_interest_principal",
                     day_count: "30/360" })
  due = lending.payment(l)
  evs = []
  for k = 1 to how_many
    append(evs, { on: l.opened + (1 month) * k, kind: "payment", amount: due })
  next
  return { id: id, loan: l, events: evs }
end function
