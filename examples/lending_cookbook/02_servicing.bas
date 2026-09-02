' Recipe 2 — Servicing: what actually happened.
'
' `lending.apply` is a FOLD over the event list, not an incremental step. The
' state is a pure function of the loan and everything that has happened to it,
' so "why is this balance what it is" is answerable by replaying the record.
' Stored incremental state makes the number itself the answer, and if it is
' ever wrong there is nothing left to reconstruct it from.
'
' An event is { on: date, kind: "payment"|"fee"|"rate_change", amount: ... },
' and events must be in date order.
program main()
  load lending

  opened {date}= "2026-01-01"
  amount {USD}= "10000.00"
  l = lending.loan({ principal: amount, rate: 0.12, term: 12,
                     opened: opened, basis: "amortized",
                     waterfall: "fees_interest_principal",
                     day_count: "actual/365" })

  feb {date}= "2026-02-01"
  mar15 {date}= "2026-03-15"
  apr {date}= "2026-04-01"
  may {date}= "2026-05-01"

  scheduled {USD}= "888.49"
  late_fee {USD}= "25.00"
  partial {USD}= "400.00"
  catch_up {USD}= "1400.00"

  events = [
    { on: feb,   kind: "payment", amount: scheduled },
    { on: mar15, kind: "fee",     amount: late_fee },
    { on: mar15, kind: "payment", amount: scheduled },
    { on: apr,   kind: "payment", amount: partial },
    { on: may,   kind: "payment", amount: catch_up }
  ]

  ' History is OPT-IN. Returning it always would make a portfolio scan O(n) in
  ' memory per loan for a field it never reads.
  st = lending.apply(l, events, true)

  print "date         event         balance     owed interest   fees"
  for each h in st.history
    print (string(h.on.year) + "-" + pad(h.on.month) + "-" + pad(h.on.day)
           + "   " + fill(h.kind, 12) + "  " + fill(string(h.balance), 10)
           + "  " + fill(string(h.accrued), 8) + "     " + string(h.fees_due))
  next

  print ""
  print "principal repaid:  " + string(st.paid_principal)
  print "interest paid:     " + string(st.paid_interest)
  print "fees paid:         " + string(st.paid_fees)
  print "still owed:        " + string(st.balance)

  ' The fold's own invariant, and worth asserting rather than trusting: every
  ' dollar received went somewhere.
  received {USD}= "0.00"
  for each e in events
    if e.kind = "payment" then
      received = received + e.amount
    end if
  next
  print ""
  print "received:          " + string(received)
  print "accounted for:     " + string(st.paid_principal + st.paid_interest + st.paid_fees)
  print "nothing lost:      " + string(received = st.paid_principal + st.paid_interest + st.paid_fees)
end program

function pad(n)
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
