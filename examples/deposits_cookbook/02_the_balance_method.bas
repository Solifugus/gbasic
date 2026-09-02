' Recipe 2 — Which balance earns? Three answers, and banks name theirs.
'
'   daily          every day earns on its own balance
'   average_daily  the mean balance earns for the whole period
'   minimum        the LOWEST balance the account ever held earns for the
'                  whole period
'
' Same money, same activity, different interest. That is why the account
' declares its method and this library never picks one for you.
program main()
  load deposits

  opened {date}= "2026-01-01"
  eom {date}= "2026-01-31"
  mid {date}= "2026-01-21"
  balance {USD}= "10000.00"
  big {USD}= "9000.00"

  daily   = acct("daily")
  average = acct("average_daily")
  lowest  = acct("minimum")

  ' A large withdrawal LATE in the period: 10,000 for twenty days, then 1,000
  ' for ten. The three methods see three different accounts.
  events = [{ on: mid, kind: "withdrawal", amount: big }]

  d = deposits.apply(daily, events, eom)
  a = deposits.apply(average, events, eom)
  n = deposits.apply(lowest, events, eom)

  print "10,000 held for 20 days, then 9,000 withdrawn:"
  print "  daily          " + string(d.credited)
  print "  average_daily  " + string(a.credited)
  print "  minimum        " + string(n.credited)
  print ""
  print ("minimum pays "
         + string(round(number(string(d.credited)) / number(string(n.credited)), 1))
         + " times less on identical activity.")

  ' AND A PAIR THAT DOES NOT DIFFER — asserted as EQUAL, and explained,
  ' because asserting a difference here would assert something false. Simple
  ' interest is LINEAR in the balance, so the mean balance earns exactly what
  ' each day's own balance earns. `daily` and `average_daily` agree whenever
  ' the rate is constant, and part company only under tiers (recipe 3).
  print ""
  print "daily and average_daily are equal here: " + string(d.credited = a.credited)
  print "  not a coincidence — simple interest is linear in the balance."

  ' And the control: with no activity, all three agree, because there is only
  ' ever one balance to disagree about.
  q = deposits.apply(daily, [], eom)
  r = deposits.apply(lowest, [], eom)
  print ""
  print "with no activity at all, daily and minimum agree: " + string(q.credited = r.credited)
end program

function acct(method)
  load deposits
  opened {date}= "2026-01-01"
  balance {USD}= "10000.00"
  return deposits.account({ opened: opened, balance: balance, rate: 0.05,
                            day_count: "actual/365", balance_method: method,
                            crediting: 30 })
end function
