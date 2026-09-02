' Recipe 3 — Tiered rates: whole-balance or bracket?
'
' A tier table is { from, rate }, lowest first. What a balance earns depends on
' which of two products you are selling, and they are far apart at a boundary:
'
'   "whole"    the whole balance earns the tier it LANDS IN
'   "portion"  each slice earns its own tier, like a tax bracket
'
' A deposit product using the wrong one pays a perfectly plausible amount of
' the wrong interest, which is why the mode is declared rather than assumed.
program main()
  load deposits
  load finance

  base {USD}= "0.00"
  mid {USD}= "10000.00"
  top {USD}= "50000.00"

  tiers = [{ from: base, rate: 0.01 },
           { from: mid,  rate: 0.03 },
           { from: top,  rate: 0.05 }]

  small {USD}= "500.00"
  large {USD}= "60000.00"

  print "whole-balance mode — the balance finds its tier:"
  print "     500 earns " + string(deposits.tiered_rate(tiers, small, "whole"))
  print "  60,000 earns " + string(deposits.tiered_rate(tiers, large, "whole"))

  opened {date}= "2026-01-01"
  one_year {date}= "2027-01-01"

  whole_rate = deposits.tiered_rate(tiers, large, "whole")
  whole_amt = finance.accrue(large, whole_rate, opened, one_year, "actual/365")
  portioned = deposits.tiered_interest(tiers, large, opened, one_year, "actual/365")

  print ""
  print "a year on 60,000:"
  print "  whole    " + string(whole_amt) + "   (all of it at 5%)"
  print "  portion  " + string(portioned) + "   (10,000 at 1%, 40,000 at 3%, 10,000 at 5%)"
  print "  the gap  " + string(whole_amt - portioned)

  ' Check the bracket arithmetic against its own definition rather than
  ' trusting the total: each slice, priced at its own tier, summed by hand.
  slice_a {USD}= "10000.00"
  slice_b {USD}= "40000.00"
  slice_c {USD}= "10000.00"
  by_hand = (finance.accrue(slice_a, 0.01, opened, one_year, "actual/365")
             + finance.accrue(slice_b, 0.03, opened, one_year, "actual/365")
             + finance.accrue(slice_c, 0.05, opened, one_year, "actual/365"))
  print ""
  print "the slices, priced by hand: " + string(by_hand)
  print "matches tiered_interest:    " + string(by_hand = portioned)

  ' `tiered_rate` refuses the portion mode outright: there IS no single rate a
  ' bracketed balance earns, and returning the top tier's would be a number
  ' that reads like an answer.
  on error goto next
  r = deposits.tiered_rate(tiers, large, "portion")
  if error then
    print ""
    print "refused: " + error.message
    error.clear()
  end if
  on error stop
end program
