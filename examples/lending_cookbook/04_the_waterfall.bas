' Recipe 4 — The waterfall: where a short payment lands.
'
' A borrower sends less than is owed. Fees, interest and principal are all
' outstanding. Which gets paid first is a POLICY, it is written in the note,
' and it changes the balance, the interest that accrues next period, and
' whether the loan reports delinquent.
'
' The totals do NOT differ — the same money arrived either way. Only the
' components move, which is exactly why a waterfall bug is hard to see: every
' figure on the statement is a perfectly ordinary amount of money.
program main()
  load lending

  opened {date}= "2026-01-01"
  amount {USD}= "10000.00"
  fee {USD}= "50.00"
  short {USD}= "120.00"

  fip = lending.loan({ principal: amount, rate: 0.12, term: 12,
                       opened: opened, basis: "amortized",
                       waterfall: "fees_interest_principal",
                       day_count: "actual/365" })
  ipf = lending.loan({ principal: amount, rate: 0.12, term: 12,
                       opened: opened, basis: "amortized",
                       waterfall: "interest_principal_fees",
                       day_count: "actual/365" })

  mid {date}= "2026-01-15"
  feb {date}= "2026-02-01"

  ' A late fee on the 15th, then $120 on the 1st against $100 of interest and
  ' a $50 fee. There is not enough to clear both.
  events = [{ on: mid, kind: "fee",     amount: fee },
            { on: feb, kind: "payment", amount: short }]

  a = lending.apply(fip, events, false)
  b = lending.apply(ipf, events, false)

  print "fees_interest_principal — the fee is taken first"
  print "  to fees:       " + string(a.paid_fees)
  print "  to interest:   " + string(a.paid_interest)
  print "  to principal:  " + string(a.paid_principal)
  print "  still owed:    interest " + string(a.accrued) + ", fees " + string(a.fees_due)

  print ""
  print "interest_principal_fees — interest is taken first, the fee waits"
  print "  to fees:       " + string(b.paid_fees)
  print "  to interest:   " + string(b.paid_interest)
  print "  to principal:  " + string(b.paid_principal)
  print "  still owed:    interest " + string(b.accrued) + ", fees " + string(b.fees_due)

  ' The check worth making. Assert the TOTALS are equal — because they are, by
  ' construction — and the COMPONENTS differ. A test that compared totals would
  ' pass on a library that had no waterfall at all.
  print ""
  print ("same money received either way:   "
         + string(a.paid_fees + a.paid_interest + a.paid_principal
                  = b.paid_fees + b.paid_interest + b.paid_principal))
  print "different place on the balance:   " + string(a.balance != b.balance)

  ' An overpayment is REFUSED rather than becoming a negative balance. Money
  ' beyond what is owed is a refund, and a loan does not hold it.
  on error goto next
  huge {USD}= "99999.00"
  x = lending.apply(fip, [{ on: feb, kind: "payment", amount: huge }], false)
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program
