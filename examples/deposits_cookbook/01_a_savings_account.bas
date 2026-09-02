' Recipe 1 — A savings account, and the difference between compounding
' and crediting.
'
' They are not the same thing, and conflating them is the common error.
' Interest is COMPUTED over a crediting period and ADDED to the balance at the
' end of it — so the next period earns on the larger balance. That addition is
' the compounding. `crediting` is simply how many days a period is.
'
' Interest earned since the last crediting date is reported SEPARATELY, in
' `accrued`. The holder has earned it and has not been paid it, and rolling it
' into the balance would say otherwise.
program main()
  load deposits

  opened {date}= "2026-01-01"
  balance {USD}= "10000.00"

  acct = deposits.account({ opened: opened, balance: balance, rate: 0.05,
                            day_count: "actual/365",
                            balance_method: "daily",
                            crediting: 30 })

  ' No deposits or withdrawals — just the account, left alone for a year.
  year_end {date}= "2026-12-31"
  st = deposits.apply(acct, [], year_end)

  print "opening balance:     " + string(acct.balance)
  print "credited over 2026:  " + string(st.credited)
  print "closing balance:     " + string(st.balance)
  print ""
  print "last crediting date: " + ymd(st.last_credited)
  print "  earned since then, not yet paid: " + string(st.accrued)
  print "available today:     " + string(st.available)
  print "  which is balance + accrued: " + string(st.available = st.balance + st.accrued)

  ' THAT is the compounding, and here is the proof. Simple interest on 10,000
  ' at 5% for a year is 500. Twelve 30-day periods, each credited, come to
  ' more — and the excess is what the earlier periods' interest itself earned.
  simple {USD}= "500.00"
  print ""
  print "simple interest would be:  " + string(simple)
  print "credited interest was:     " + string(st.credited)
  print "compounding is worth:      " + string(st.credited - simple)

  ' Deposits and withdrawals are events, in date order.
  mar {date}= "2026-03-01"
  sep {date}= "2026-09-01"
  paycheck {USD}= "2500.00"
  rent {USD}= "1800.00"
  active = deposits.apply(acct, [{ on: mar, kind: "deposit",    amount: paycheck },
                                 { on: sep, kind: "withdrawal", amount: rent }], year_end)
  print ""
  print "with a 2,500 deposit in March and an 1,800 withdrawal in September:"
  print "  closing balance:  " + string(active.balance)
  print "  interest credited " + string(active.credited)
  print "  more than idle:   " + string(active.credited > st.credited)
end program

function ymd(d)
  return string(d.year) + "-" + two(d.month) + "-" + two(d.day)
end function

function two(n)
  if n < 10 then
    return "0" + string(n)
  end if
  return string(n)
end function
