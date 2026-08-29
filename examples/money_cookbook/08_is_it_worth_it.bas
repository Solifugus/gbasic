' Recipe 8 — Is a project worth doing?
'
' Two questions, and they are the same equation solved for different unknowns:
' what is a stream of future money worth today (NPV), and what return does it
' represent (IRR)?

program main(args)
  load finance

  ' A machine costing 50,000 that saves 15,000 a year for five years.
  outlay {USD}= "50000.00"
  saving {USD}= "15000.00"
  flows = [saving, saving, saving, saving, saving]

  ' At a 10% cost of capital, what are those savings worth now?
  worth = finance.npv(0.10, flows)
  print "savings are worth today: " + string(worth)
  print "the machine costs:       " + string(outlay)
  print "net:                     " + string(worth - outlay)

  ' The rate at which it exactly breaks even. Compare it to what the money
  ' costs you: above your cost of capital, the project earns its keep.
  print ""
  print "internal rate of return: " + string(round(finance.irr(outlay, flows) * 100, 2)) + "%"

  ' Discounting harder makes future money worth less, which is the whole idea.
  print ""
  print "at 5%:  " + string(finance.npv(0.05, flows))
  print "at 20%: " + string(finance.npv(0.20, flows))

  ' Flows that can never repay the outlay are refused rather than returning a
  ' meaningless rate.
  print ""
  on error goto next
  penny {USD}= "1.00"
  r = finance.irr(outlay, [penny])
  if error then
    print "one dollar against 50,000: " + error.message
    error.clear()
  end if
  on error stop
end program
