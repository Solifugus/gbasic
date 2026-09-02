' Recipe 6 — Underwriting ratios, and the `unknown` that keeps them honest.
'
' Four ratios, and two traps.
'
' TRAP ONE: a missing input returns `unknown`, never zero. These feed credit
' decisions. An absent income figure that silently became zero would make
' every ratio look either perfect or catastrophic, and nothing on the page
' would say which.
'
' TRAP TWO: `dscr` is oriented the OTHER WAY UP. Low is good for the first
' three; above one is good for dscr. That is a real trap, which is why it is
' its own named function rather than folded into `dti`.
program main()
  load lending

  loan_amount {USD}= "240000.00"
  value {USD}= "300000.00"
  monthly_debt {USD}= "2000.00"
  monthly_income {USD}= "6000.00"
  monthly_payment {USD}= "1498.88"
  noi {USD}= "130000.00"
  debt_service {USD}= "100000.00"

  print "loan to value:       " + pct(lending.ltv(loan_amount, value))
  print "debt to income:      " + pct(lending.dti(monthly_debt, monthly_income))
  print "payment to income:   " + pct(lending.payment_to_income(monthly_payment, monthly_income))
  print ""
  print "debt service coverage: " + string(round(lending.dscr(noi, debt_service), 4))
  print "  above 1 is healthy — the inverse of the three above."
  print "  healthy here?        " + string(lending.dscr(noi, debt_service) > 1)

  ' A missing input is `unknown`, and a caller can SEE that it is missing.
  ' Compare with the alternative: a zero income gives a debt-to-income of
  ' infinity or of nothing, and either reads as a number somebody can act on.
  no_income = lending.dti(monthly_debt, nothing)
  print ""
  print "with no income on file:"
  print "  is it unknown?  " + string(is_unknown(no_income))
  print "  is it zero?     " + string(no_income = 0)

  ' A zero denominator is the same case for the same reason.
  nothing_earned {USD}= "0.00"
  print "with an income of exactly zero:"
  print "  is it unknown?  " + string(is_unknown(lending.dti(monthly_debt, nothing_earned)))
end program

function pct(r)
  if is_unknown(r) then
    return "unknown"
  end if
  return string(round(r * 100, 2)) + "%"
end function
