' Recipe 5 — Vintage curves: age, never calendar month.
'
' A vintage curve is a cohort's cumulative bad rate plotted against MONTHS ON
' BOOK, so loans written two years apart can be compared at the same point in
' their lives. Indexed by calendar month instead, every cohort's curve starts
' at a different age, the averages mix cohorts, and the result is a smooth
' meaningless line that does not look like a mistake.
'
' CUMULATIVE MEANS EVER-REACHED: once a loan touches a bad status it counts at
' that age and every later one, even if it cures. That is the convention a
' loss curve is drawn under, and it is declared rather than assumed because
' "currently in" is a different and equally reasonable curve.
program main()
  load credit
  load lending

  jan {date}= "2026-01-01"
  apr {date}= "2026-04-01"
  jul {date}= "2026-07-01"
  amount {USD}= "12000.00"

  ' Three quarterly cohorts of ten. Two in each stop paying after two
  ' instalments; two of the January cohort prepay and leave the book.
  book = []
  for each c in [{ opened: jan, tag: "A", prepay: 2 },
                 { opened: apr, tag: "B", prepay: 0 },
                 { opened: jul, tag: "C", prepay: 0 }]
    for i = 1 to 10
      id = c.tag + string(i)
      if i <= 2 then
        append(book, plain(id, c.opened, amount, 2))
      else if i <= 2 + c.prepay then
        append(book, prepaid(id, c.opened, amount, 4))
      else
        append(book, plain(id, c.opened, amount, 11))
      end if
    next
  next

  observed_on = []
  for k = 0 to 11
    append(observed_on, jan + (1 month) * k)
  next
  table = credit.observe(book, observed_on, "mba")

  v = credit.vintage(table, { basis: "original", cohort_by: "quarter" })

  print "cumulative bad rate by months on book (basis: original)"
  print ""
  header = pad("age", 5)
  for each c in v.cohorts
    header = header + "  " + pad(c, 10)
  next
  print trim(header)
  for age = 0 to 11
    line = pad(string(age), 5)
    any = false
    for each c in v.cohorts
      r = at_age(v, c, age)
      if is_unknown(r) then
        line = line + "  " + pad("-", 10)
      else
        line = line + "  " + pad(pct(r), 10)
        any = true
      end if
    next
    if any then
      print trim(line)
    end if
  next

  ' THE TABLE IS A TRIANGLE, and that is the point of the dashes. A cohort's
  ' curve STOPS at the age it has reached. Carrying every cohort out to the
  ' oldest one's age would report 0% at ages the young cohort has not lived
  ' through — "no losses" where the truth is "no data", which is the same
  ' plausible wrong answer this library exists to refuse.
  print ""
  print "  a dash is NO DATA, not a zero. The 2026-Q3 cohort is five months"
  print "  old; it has no month-nine bad rate to report."

  ' BASIS IS REQUIRED, and the two are different curves. `original` divides by
  ' the cohort as it was written; `outstanding` by what is still on the book.
  ' They part company as soon as anything runs off — which is why the January
  ' cohort has prepayments in it and the others do not.
  o = credit.vintage(table, { basis: "outstanding", cohort_by: "quarter" })
  print ""
  print "the January cohort at month 9:"
  print ("  original     " + pct(at_age(v, "2026-Q1", 9))
         + "   (2 bad out of the 10 written)")
  print ("  outstanding  " + pct(at_age(o, "2026-Q1", 9))
         + "   (2 bad out of the " + string(at_risk(o, "2026-Q1", 9))
         + " still on the book)")
  print "  the two differ: " + string(at_age(v, "2026-Q1", 9) != at_age(o, "2026-Q1", 9))

  ' Omitting the basis is refused. Neither is the default because neither is
  ' the answer to the other's question.
  on error goto next
  x = credit.vintage(table, { cohort_by: "quarter" })
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

function prepaid(id, opened, amount, after)
  load lending
  l = terms(opened, amount)
  evs = stream(l, after)
  clear_on = l.opened + (1 month) * (after + 1)
  quote = lending.payoff(l, evs, clear_on)
  append(evs, { on: clear_on, kind: "payment", amount: quote.total })
  return { id: id, loan: l, events: evs }
end function

function at_age(v, cohort_label, age)
  for each p in v.series[cohort_label]
    if p.age = age then
      return p.rate
    end if
  next
  return unknown
end function

function at_risk(v, cohort_label, age)
  for each p in v.series[cohort_label]
    if p.age = age then
      return p.at_risk
    end if
  next
  return unknown
end function

function pct(r)
  if is_unknown(r) then
    return "-"
  end if
  return string(round(r * 100, 1)) + "%"
end function

function pad(s, w)
  out = s
  while len(out) < w
    out = out + " "
  end while
  return out
end function
