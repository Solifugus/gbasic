' Recipe 5 — Converting currency, with the date it happened.
'
' A rate is a DATED fact. Converting without an as-of date gives a number
' nobody can reproduce: re-run last quarter's report and you silently get
' today's rate, and the figure that comes out looks perfectly defensible.

program main(args)
  jan {date}= "2026-01-01"
  jun {date}= "2026-06-01"
  mar {date}= "2026-03-01"
  dec {date}= "2026-12-01"

  ' The rate is decimal TEXT: FX rates carry more significant figures than a
  ' double reliably holds.
  money.rate("USD", "EUR", "0.92", jan)
  money.rate("USD", "EUR", "0.95", jun)
  money.rate("USD", "JPY", "151.25", jan)

  u {USD}= "1000.00"

  ' `convert` applies the rate EFFECTIVE on the date — the latest one on or
  ' before it — so a report run for March sees March.
  print "on 2026-03-01: " + string(money.convert(u, "EUR", mar))
  print "on 2026-12-01: " + string(money.convert(u, "EUR", dec)) + "   (June's rate, still current)"

  ' Across different minor units, in one exact operation.
  print "in yen:        " + string(money.convert(u, "JPY", jan))

  ' Which rate was applied? That is the whole point of dating them.
  print ""
  r = money.rate_on("USD", "EUR", mar)
  print "March used rate " + r.rate + " dated " + string(r.as_of)

  ' Inversion is refused: the two sides of a quote differ by a spread, so
  ' EUR->USD is not 1/0.95. The refusal says a rate exists the other way.
  print ""
  on error goto next
  e {EUR}= "500.00"
  back = money.convert(e, "USD", jun)
  if error then
    print "the other way: " + error.message
    error.clear()
  end if
  on error stop

  ' Converting to the currency you already hold needs no rate, so code that
  ' normalises a mixed list into one reporting currency just works.
  print ""
  print "USD to USD:    " + string(money.convert(u, "USD", jun))
end program
