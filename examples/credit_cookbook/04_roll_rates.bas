' Recipe 4 — Roll rates, and the denominator that decides them.
'
' `roll_rates` is `migration` divided. The one decision it makes is the
' denominator, and it is the whole recipe:
'
'     THE DENOMINATOR IS THE WHOLE STARTING BUCKET, INCLUDING THE LOANS
'     THAT WENT UNOBSERVED.
'
' Dropping them is the commonest defect in roll-rate work, and it does not
' produce an error. It produces an ordinary-looking percentage that says the
' book is curing, because the loans that disappeared were disproportionately
' the ones that stopped being worth servicing.
'
' A bucket with nothing in it reports `unknown`, never zero. A zero roll rate
' out of an empty bucket reads as "nothing went bad" when the truth is "there
' was nothing there".
program main()
  load credit
  load lending

  jan {date}= "2026-01-01"
  amount {USD}= "12000.00"

  book = []
  for i = 1 to 8
    append(book, plain("C" + string(i), jan, amount, 11))
  next
  append(book, plain("D1", jan, amount, 3))
  append(book, plain("D2", jan, amount, 4))

  observed_on = []
  for k = 0 to 11
    append(observed_on, jan + (1 month) * k)
  next
  full = credit.observe(book, observed_on, "mba")

  ' Two of the eight paying loans drop out of the extract after September.
  sep {date}= "2026-09-01"
  oct {date}= "2026-10-01"
  table = []
  for each r in full
    gone = (r.id = "C7" or r.id = "C8") and r.as_of > sep
    if not gone then
      append(table, r)
    end if
  next

  rr = credit.roll_rates(table, sep, oct)
  m = credit.migration(table, sep, oct)

  print "from `current`, 2026-09-01 to 2026-10-01"
  print "  loans in the bucket:  " + string(rr.starting["current"])
  print "  stayed current:       " + pct(rr.rates["current"]["current"])
  print "  went unobserved:      " + pct(rr.unobserved["current"])

  ' What the wrong denominator would have said. Same data, one decision
  ' different, and the difference is a book that looks flawless.
  stayed = m.counts["current"]["current"]
  survivors = m.starting["current"] - m.unobserved["current"]
  print ""
  print "counting only the loans still visible:"
  print ("  " + string(stayed) + " of " + string(survivors) + " = "
         + pct(stayed / survivors) + " stayed current")
  print "counting the whole starting bucket, as the library does:"
  print ("  " + string(stayed) + " of " + string(m.starting["current"]) + " = "
         + pct(rr.rates["current"]["current"]))
  print "The first number is not wrong arithmetic. It answers a different"
  print "question, and nothing on a report distinguishes them."

  ' An empty bucket, which every real book has several of.
  print ""
  print "nothing was charged off in September:"
  print "  loans in the bucket:  " + string(rr.starting["charged_off"])
  print "  roll to current:      " + pct(rr.rates["charged_off"]["current"])
  print "  is that unknown?      " + string(is_unknown(rr.rates["charged_off"]["current"]))
  print "  is that zero?         " + string(rr.rates["charged_off"]["current"] = 0)
end program

function terms(opened, amount)
  load lending
  return lending.loan({ principal: amount, rate: 0.09, term: 24,
                        opened: opened, basis: "amortized",
                        waterfall: "fees_interest_principal",
                        day_count: "30/360" })
end function

function plain(id, opened, amount, how_many)
  load lending
  l = terms(opened, amount)
  due = lending.payment(l)
  evs = []
  for k = 1 to how_many
    append(evs, { on: l.opened + (1 month) * k, kind: "payment", amount: due })
  next
  return { id: id, loan: l, events: evs }
end function

function pct(r)
  if is_unknown(r) then
    return "unknown"
  end if
  return string(round(r * 100, 1)) + "%"
end function
