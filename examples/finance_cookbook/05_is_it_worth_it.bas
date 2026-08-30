' Recipe 5 — Is this project worth doing?
'
' NPV asks "what are these future flows worth today"; IRR asks "what rate would
' make them break even". Both take the flows as ONE array with the outlay first
' and negative, the way a spreadsheet lays them out in a column.
program main()
  load finance

  ' A machine costing 50,000 that saves 15,000 a year for five years.
  spent {USD}= "-50000.00"
  saved {USD}= "15000.00"
  flows = [spent, saved, saved, saved, saved, saved]

  ' `npv` discounts flows one period apart starting at period 1, so the
  ' period-0 outlay is added separately -- Excel's NPV works the same way.
  later = [saved, saved, saved, saved, saved]
  print "savings are worth today: " + string(finance.npv(0.10, later))
  print "the machine costs:       " + string(spent * -1)
  print "net at 10%:              " + string(finance.npv(0.10, later) + spent)

  print ""
  print "internal rate of return: " + string(round(finance.irr(flows) * 100, 2)) + "%"

  ' Discounting harder makes future money worth less, which is the whole idea.
  print ""
  print "net at 5%:  " + string(finance.npv(0.05, later) + spent)
  print "net at 20%: " + string(finance.npv(0.20, later) + spent)

  ' Above your cost of capital the project earns its keep; below it, it does not.
  print ""
  print "worth doing at a 10% cost of capital: " + string(finance.irr(flows) > 0.10)
  print "worth doing at 20%:                   " + string(finance.irr(flows) > 0.20)
end program
