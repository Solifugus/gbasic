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
  ' `irr` takes the flows as ONE array with the outlay first and negative, the
  ' way a spreadsheet lays them out in a column.
  spent {USD}= "-50000.00"
  print "internal rate of return: " + string(round(finance.irr([spent, saving, saving, saving, saving, saving]) * 100, 2)) + "%"

  ' Discounting harder makes future money worth less, which is the whole idea.
  print ""
  print "at 5%:  " + string(finance.npv(0.05, flows))
  print "at 20%: " + string(finance.npv(0.20, flows))

  ' A hopeless project does not raise -- it has a rate, and the rate is the
  ' answer. Fifty thousand returning one dollar breaks even at about -99.998%,
  ' which is what "you lost essentially all of it" looks like as a percentage.
  print ""
  penny {USD}= "1.00"
  print "one dollar back on 50,000: " + string(round(finance.irr([spent, penny]) * 100, 3)) + "%"

  ' What genuinely has no answer is a set of flows the search cannot bracket at
  ' all, and that is reported rather than guessed at.
  print ""
  on error goto next
  never = finance.irr([saving, saving])
  if error then
    print "all-positive flows: " + error.message
    error.clear()
  end if
  on error stop
end program
