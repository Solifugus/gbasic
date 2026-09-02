' Recipe 3 — Where the book went, in counts.
'
' `migration` reports where every loan that existed at one date had got to by
' the next. It returns COUNTS, never rates — `roll_rates` divides, in recipe 4
' — because the invariant this library rests on is about counts and a rate
' cannot state it:
'
'     EVERY LOAN OBSERVED AT t IS ACCOUNTED FOR AT t+1.
'
' Nothing in the library enforces that. It falls out of correct bucketing,
' which is exactly what makes it worth asserting.
'
' ATTRITION IS A STATE, NOT A HOLE. `paid_off` and `charged_off` are buckets
' and nothing rolls out of them. A loan present at t and MISSING at t+1 is
' reported as `unobserved` rather than dropped — "we stopped seeing it" is a
' fact about the data, not about the borrower, and dropping those loans makes
' a book look like it is curing.
program main()
  load credit
  load lending

  jan {date}= "2026-01-01"
  amount {USD}= "12000.00"

  book = []
  append(book, plain("L1", jan, amount, 11))
  append(book, plain("L2", jan, amount, 11))
  append(book, plain("L3", jan, amount, 11))
  append(book, plain("L4", jan, amount, 3))       ' stops paying
  append(book, paid_off_at("L5", jan, amount, 8)) ' clears the balance
  append(book, plain("L6", jan, amount, 11))      ' leaves the extract

  observed_on = []
  for k = 0 to 11
    append(observed_on, jan + (1 month) * k)
  next

  full = credit.observe(book, observed_on, "mba")

  ' A REAL EXTRACT HAS HOLES. L6 was sold to another servicer in September and
  ' simply stops appearing. `observe` would never do this — it emits a row for
  ' every date once a loan is written — so it is done here by hand, which is
  ' also the honest way to say that this is what your own data will look like.
  sep {date}= "2026-09-01"
  table = []
  for each r in full
    if r.id != "L6" or r.as_of <= sep then
      append(table, r)
    end if
  next

  oct {date}= "2026-10-01"
  m = credit.migration(table, sep, oct)

  print "migration 2026-09-01 to 2026-10-01"
  print "  loans at the start:  " + string(m.total)
  print "  still observed:      " + string(m.observed)
  print "  no longer observed:  " + string(m.total - m.observed)
  print "  new since the start: " + string(m.entered)

  print ""
  print "from            to              n"
  for each b in m.buckets
    if m.starting[b] > 0 then
      for each c in m.buckets
        if m.counts[b][c] > 0 then
          print "  " + fill(b, 14) + fill(c, 14) + string(m.counts[b][c])
        end if
      next
      if m.unobserved[b] > 0 then
        print "  " + fill(b, 14) + fill("(unobserved)", 14) + string(m.unobserved[b])
      end if
    end if
  next

  ' THE INVARIANT, asserted rather than trusted. For each starting bucket, the
  ' loans that went somewhere plus the loans that vanished must be the loans
  ' that were there. A matrix that quietly dropped the vanished ones would
  ' still balance to a smaller total and look perfectly reasonable.
  print ""
  bad = 0
  for each b in m.buckets
    landed = 0
    for each c in m.buckets
      landed = landed + m.counts[b][c]
    next
    if landed + m.unobserved[b] != m.starting[b] then
      bad = bad + 1
    end if
  next
  print "every starting bucket reconciles: " + string(bad = 0)

  ' Comparing against a date with no rows would report 100% attrition, which
  ' is a plausible catastrophe rather than an error. It is refused.
  on error goto next
  ancient {date}= "2020-01-01"
  x = credit.migration(table, ancient, oct)
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program

function terms(opened, amount)
  load lending
  return lending.loan({ principal: amount, rate: 0.09, term: 24,
                        opened: opened, basis: "amortized",
                        waterfall: "fees_interest_principal",
                        day_count: "30/360" })
end function

function stream(l, how_many)
  load lending
  due = lending.payment(l)
  out = []
  for k = 1 to how_many
    append(out, { on: l.opened + (1 month) * k, kind: "payment", amount: due })
  next
  return out
end function

function plain(id, opened, amount, how_many)
  l = terms(opened, amount)
  return { id: id, loan: l, events: stream(l, how_many) }
end function

function paid_off_at(id, opened, amount, after)
  load lending
  l = terms(opened, amount)
  evs = stream(l, after)
  clear_on = l.opened + (1 month) * (after + 1)
  quote = lending.payoff(l, evs, clear_on)
  append(evs, { on: clear_on, kind: "payment", amount: quote.total })
  return { id: id, loan: l, events: evs }
end function

function fill(s, w)
  out = s
  while len(out) < w
    out = out + " "
  end while
  return out
end function
