' Design laboratory — Recipe 10: what price?
'
' Every recipe so far has been the SAME SHAPE: a measure moved, where, and
' which of a handful of actions to take. This one is deliberately different,
' because that sameness was the largest untested assumption in the design:
'
'   * the answer is a CONTINUOUS QUANTITY, not a choice from a list;
'   * there is no decomposition -- "where did it happen" is not the question;
'   * the cost of INACTION dominates: a wrong price bleeds every day;
'   * and it needs a MODEL, whose uncertainty has to reach the answer.
'
' The last of those is what breaks things, and it is the point.
program main()
  load stats
  load decision

  print "A product costing 10.00 to make, and a year of weekly prices and"
  print "volumes. What price maximises profit?"
  print ""
  print "For constant-elasticity demand the answer is  p* = cost * b/(1+b),"
  print "which exists only while the elasticity b is below -1. At -1 revenue"
  print "is flat in price and profit rises without bound; above it, the model"
  print "has no maximum at all."
  print ""

  study("CASE A: clearly elastic", 0 - 2.5)
  study("CASE B: elastic, but close to the edge", 0 - 1.25)
  study("CASE C: barely elastic", 0 - 1.15)

  print "  THE SHAPE OF THE ANSWER IS THE FINDING. In case A the price is"
  print "  known about as well as the elasticity is. In case B the SAME"
  print "  quality of estimate -- an interval a shade over 1.5x wide -- gives"
  print "  a price anywhere from 27.6 to 373, because the model divides by"
  print "  (1 + b) and b is near -1. In case C there is no answer at all, and"
  print "  a point estimate would have named one anyway."
end program

function study(label, true_b)
  load stats
  load decision
  print label
  d = observe_market(true_b)
  m = stats.ols(d.logq, [d.logp])
  b = m.coefficients[1]
  crit = stats.t_quantile(0.975, count(d.logq) - 2)
  se = m.std_errors[1]

  r = decision.quantity({
        parameter: { estimate: b, low: b - crit * se, high: b + crit * se },
        map: price_star,
        model: "constant-elasticity profit maximum, p* = cost*b/(1+b)" })

  print ("  elasticity  " + f2(b) + "   95% interval "
         + f2(r.parameter.low) + " to " + f2(r.parameter.high))
  if r.defined then
    print ("  price       " + f2(r.recommended) + "   from the interval "
           + f2(r.low) + " to " + f2(r.high))
    print ("  the elasticity interval is " + f2(r.parameter_spread)
           + "x wide and the price interval " + f2(r.quantity_spread) + "x")
    print "  amplification " + f2(r.amplification) + "   (1 would be proportional)"
  else
    print "  REFUSED"
    print "    " + r.why
  end if
  print ""
  return nothing
end function

' The model. It must be a plain function of the parameter alone: gBASIC has NO
' CLOSURES, so this cannot capture the cost from its caller and the 10.00 is
' written in. That is a real constraint on the shape of this API and it is
' recorded in the recipe rather than worked around quietly.
function price_star(b)
  if b >= 0 - 1 then
    return unknown
  end if
  return 10.0 * b / (1 + b)
end function

' Fifty-two weeks. Price is set within a band and volume follows a
' constant-elasticity curve with multiplicative noise.
function observe_market(true_b)
  load fake
  logp = []
  logq = []
  for w = 1 to 52
    p = 18 + fake.between(31, w * 3, 0, 1400) / 100
    shock = (fake.between(97, w * 5, 0, 600) - 300) / 1000
    q = 40000 * exp(true_b * log(p)) * exp(shock)
    append(logp, log(p))
    append(logq, log(q))
  next
  return { logp: logp, logq: logq }
end function

function f2(x)
  if is_unknown(x) then
    return "none"
  end if
  return string(round(x, 2))
end function
