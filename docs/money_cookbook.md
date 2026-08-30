# The money cookbook

Nine recipes over gBASIC's `money` type and the `finance` library — exact
amounts, real currencies, splitting a bill so the parts add up, converting at
a dated rate, and the arithmetic a line-of-business application actually does:
loan payments, amortization, project appraisal, depreciation.

The reference is [reference.md](reference.md#values-and-modifiers); the design
and its reasoning are in [money_design.md](money_design.md). This page is the
working tour.

**This page cannot lie.** It owns neither the code nor the output it shows.
`examples/money_cookbook/NN_name.bas` owns the code, its `.out` owns the
output, `tools/sync_money_cookbook.sh` copies both in, and
`tests/run_money_cookbook.sh` fails while any of them disagree. Every block
below is executed on every test run.

> **This page earned that harness immediately.** Writing recipe 7 — an
> ordinary amortization schedule — surfaced a silent defect in `money * scalar`
> that the unit tests had missed. Every payment in the schedule came out
> `0.00`. The unit fixtures all used short scalars like `2` and `1.08`; only
> realistic arithmetic produced one long enough to trip it.

---

## 1. Money is exact, and how you write it matters

<!--CODE:01_exact_by_construction-->

```basic
' Recipe 1 — Money is exact, and the way you write it matters.
'
' A gBASIC number is a double: about fifteen significant digits. Money is not
' a number — it is an exact integer of scaled units — but it still has to be
' WRITTEN somehow, and that is where exactness is won or lost.

program main(args)
  ' Decimal TEXT is parsed digit by digit. Nothing passes through a double.
  price {USD}= "19.95"
  print "from text    : " + string(price)

  ' A literal works too, and is also exact: gBASIC renders the number to its
  ' shortest decimal first, then parses that.
  same {USD}= 19.95
  print "from a literal: " + string(same)
  print "identical     : " + string(price = same)

  ' Exactness is the point. A double cannot add a cent a thousand times.
  total {USD}= "0.00"
  cent {USD}= "0.01"
  i = 0
  while i < 1000
    total = total + cent
    i += 1
  end while
  print ""
  print "0.01 added 1000 times = " + string(total)

  approx = 0.0
  i = 0
  while i < 1000
    approx = approx + 0.01
    i += 1
  end while
  print "the same in numbers   = " + string(approx) + "   <- not 10"

  ' Money stores four guard digits below the minor unit, so USD holds six
  ' decimal places. Sub-cent prices are ordinary -- fuel is posted at $3.459
  ' a gallon -- and they are kept exactly, even though display rounds to
  ' cents. Multiply to see the digits that are really there.
  print ""
  fuel {USD}= "3.459"
  print "posted 3.459/gal     = " + string(fuel) + "   <- display rounds"
  print "  ten gallons        = " + string(fuel * 10) + "  <- the value did not"

  ' Excess precision you WROTE is refused, because you wrote something money
  ' cannot hold -- past the six places, not past the two. Excess precision a
  ' CALCULATION produced is rounded, because `price * 1.08` always has
  ' seventeen digits and refusing it would make the type unusable.
  print ""
  on error goto next
  bad {USD}= "1.23456789"
  if error then
    print "authored 1.23456789 : " + error.message
    error.clear()
  end if
  on error stop
  computed {USD}= 19.95 * 1.08
  print "computed 19.95*1.08 : " + string(computed)
end program
```

<!--OUT:01_exact_by_construction-->

```
from text    : 19.95
from a literal: 19.95
identical     : true

0.01 added 1000 times = 10.00
the same in numbers   = 9.999999999999831   <- not 10

posted 3.459/gal     = 3.46   <- display rounds
  ten gallons        = 34.59  <- the value did not

authored 1.23456789 : USD: money text has more decimal places than the currency can store (USD stores 6)
computed 19.95*1.08 : 21.55
```

A gBASIC number is a double — about fifteen significant digits. Money is not a
number; it is an exact integer of scaled units. Both decimal **text** and a
plain literal are exact, because a literal is rendered to its shortest decimal
before being parsed.

Money stores four **guard digits** below the minor unit, so a USD value holds
six decimal places, not two. Sub-cent prices are ordinary rather than exotic —
fuel is posted at $3.459 a gallon, electricity quoted at $0.10432 a kWh — and
they are stored exactly. Display rounds to cents, so the only way to see the
retained digits is to use them: `fuel * 10` is `34.59`, which a value rounded
at construction could not produce.

The split on excess precision is deliberate: what you **wrote** is refused,
because you wrote something money cannot hold — past the six places, not past
the two; what a **calculation** produced is rounded, because `price * 1.08`
always carries seventeen digits and refusing it would make the type unusable.

---

## 2. Every currency, with its own decimal places

<!--CODE:02_currencies-->

```basic
' Recipe 2 — Every currency, with its own number of decimal places.
'
' Every ISO 4217 code is an assignment modifier. Each carries its own
' minor-unit exponent, which is not decoration: JPY has no decimal places at
' all and KWD has three, so a cents-shaped money type cannot represent either.

program main(args)
  u {USD}= "19.95"
  e {EUR}= "19.95"
  j {JPY}= "1995"
  k {KWD}= "19.950"

  print "USD " + string(u)
  print "EUR " + string(e)
  print "JPY " + string(j) + "     <- no decimal places"
  print "KWD " + string(k) + "  <- three"

  ' A money value KNOWS its currency, so the mistakes that matter are caught.
  print ""
  on error goto next
  x = u + e
  if error then
    print "USD + EUR : " + error.message
    error.clear()
  end if
  y = u < e
  if error then
    print "USD < EUR : " + error.message
    error.clear()
  end if
  on error stop

  ' But equality ANSWERS rather than raising. "Is 19.95 USD the same as 19.95
  ' EUR" is a real question, and the answer is no; "is it less than" is not a
  ' question at all without an exchange rate.
  print "USD = EUR : " + string(u = e)

  ' The built-in table is the CURRENT ISO list, so withdrawn currencies are
  ' not in it. Register what your data needs.
  print ""
  money.register("ITL", 0)
  lira {ITL}= "1000"
  print "registered ITL: " + string(lira)
  print "currencies known: " + string(count(money.currencies()))
end program
```

<!--OUT:02_currencies-->

```
USD 19.95
EUR 19.95
JPY 1995     <- no decimal places
KWD 19.950  <- three

USD + EUR : cannot add money in different currencies (USD and EUR)
USD < EUR : cannot order money in different currencies (USD and EUR)
USD = EUR : false

registered ITL: 1000
currencies known: 179
```

All 178 ISO 4217 codes are assignment modifiers, each with its own minor-unit
exponent. That is not decoration: JPY has no decimal places and KWD has three,
so a cents-shaped money type cannot represent either.

Adding dollars to euros raises, and so does ordering them. Equality *answers*
— "is 19.95 USD the same as 19.95 EUR" is a real question whose answer is no,
while "is it less than" is not a question at all without a rate.

The built-in table is the **current** ISO list, so withdrawn currencies are not
in it; `money.register` is how historical data gets expressed.

---

## 3. Arithmetic that does not quietly lose money

<!--CODE:03_arithmetic_keeps_the_cents-->

```basic
' Recipe 3 — Arithmetic that does not quietly lose money.
'
' Money stores four GUARD DIGITS below the minor unit. Interest, unit costs
' and conversions all produce values below a cent, and rounding each one as it
' appears loses money across a multi-step calculation.

program main(args)
  h {USD}= "100.00"

  ' A third of a hundred dollars has somewhere to live, so it comes back.
  print "(100.00 / 3) * 3 = " + string((h / 3) * 3) + "   <- 99.99 without guard digits"
  print "(1.00 / 7) * 7   = " + string((one_dollar() / 7) * 7)

  ' A third of a CENT is invisible at display precision and still retained.
  cent {USD}= "0.01"
  third = cent / 3
  print ""
  print "0.01 / 3 displays as " + string(third)
  print "  but x3 gives back " + string(third * 3)

  ' Multiplying by a whole number is exact, with no rounding decision at all.
  big {USD}= "50000000000.01"
  print ""
  print "50000000000.01 x 3 = " + string(big * 3)

  ' Those guard digits are real, and `string` does not show them -- it renders
  ' at the cent, which is what you want on screen. `money.text` renders the
  ' whole value, so money survives a trip through a database column or a JSON
  ' document and reads back as the same value.
  third = one_dollar() / 3
  print ""
  print "a third of a dollar  = " + string(third) + "        <- on screen"
  print "  all of it          = " + money.text(third) + "  <- in a file"
  print "  times three        = " + string(third * 3)

  ' Overflow raises rather than wrapping. Guard digits cost range: USD spans
  ' about plus or minus 9.22 trillion, which is generous but not infinite.
  on error goto next
  ceiling {USD}= "9223372036854.77"
  over = ceiling + cent
  if error then
    print ""
    print "past the ceiling: " + error.message
    error.clear()
  end if
  on error stop
end program

function one_dollar()
  d {USD}= "1.00"
  return d
end function
```

<!--OUT:03_arithmetic_keeps_the_cents-->

```
(100.00 / 3) * 3 = 100.00   <- 99.99 without guard digits
(1.00 / 7) * 7   = 1.00

0.01 / 3 displays as 0.00
  but x3 gives back 0.01

50000000000.01 x 3 = 150000000000.03

a third of a dollar  = 0.33        <- on screen
  all of it          = 0.333333  <- in a file
  times three        = 1.00

past the ceiling: money value is out of range
```

Money stores four **guard digits** below the minor unit, so intermediates
below a cent survive and are rounded once, at display. That is why
`(100.00 / 3) * 3` comes back whole where a cents-only representation gives
`99.99`.

`string` shows the cent, which is right on screen and lossy in a file — a
third of a dollar is `0.33` there and `0.333333` in the value. Use
**`money.text`** when the digits have to survive: it renders the whole value,
and the text reads back through `{USD}=` as the same money. That is the form
to put in a database column or a JSON document.

The cost is range — USD spans about ±$9.22 trillion — and overflow **raises**
rather than wrapping.

---

## 4. Splitting a bill into amounts you can pay

<!--CODE:04_splitting_a_bill-->

```basic
' Recipe 4 — Splitting money into amounts you can actually pay.
'
' Division and allocation are different problems. `100.00 / 3` is a perfectly
' good number and keeps its guard digits — but three PAYMENTS cannot each be
' 33.3333. An invoice line, a payroll entry or a dividend has to be a whole
' number of minor units.

program main(args)
  bill {USD}= "100.00"

  parts = money.allocate(bill, 3)
  print "100.00 three ways: " + string(parts)

  ' THE PROPERTY THAT MATTERS: they sum back to the original exactly. Three of
  ' 33.33 would lose a cent; three of 33.34 would invent one. Both look
  ' perfectly reasonable on the page.
  total {USD}= "0.00"
  for each p in parts
    total = total + p
  next
  print "  they sum to:     " + string(total)

  ' Weights, for anything proportional: shares, floor area, headcount.
  print ""
  shares = money.allocate(bill, [1, 1, 2])
  print "split 1:1:2      : " + string(shares)
  awkward = money.allocate(bill, [1, 1, 1, 1, 1, 1, 7])
  print "split six and one: " + string(awkward)
  t2 {USD}= "0.00"
  for each p in awkward
    t2 = t2 + p
  next
  print "  still exact:     " + string(t2 = bill)

  ' Currency is respected: yen split into whole yen.
  print ""
  y {JPY}= "100"
  print "JPY 100 three ways: " + string(money.allocate(y, 3))
end program
```

<!--OUT:04_splitting_a_bill-->

```
100.00 three ways: [33.34,33.33,33.33]
  they sum to:     100.00

split 1:1:2      : [25.00,25.00,50.00]
split six and one: [7.70,7.70,7.69,7.69,7.69,7.69,53.84]
  still exact:     true

JPY 100 three ways: [34,33,33]
```

Division and allocation are different problems. `100.00 / 3` is a fine number
and keeps its guard digits, but three *payments* cannot each be `33.3333` — an
invoice line or payroll entry has to be whole minor units.

So `money.allocate` works at the minor unit and hands out the remainder one
unit at a time, which is the only way **the parts sum back exactly**. Three of
33.33 would lose a cent; three of 33.34 would invent one. Both look perfectly
reasonable on the page, which is why the test asserts the *sum*.

---

## 5. Converting currency, with the date it happened

<!--CODE:05_converting_currency-->

```basic
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
```

<!--OUT:05_converting_currency-->

```
on 2026-03-01: 920.00
on 2026-12-01: 950.00   (June's rate, still current)
in yen:        151250

March used rate 0.92 dated 2026-01-01

the other way: no EUR to USD rate on that date; a USD to EUR rate exists, but inverting it would invent a spread

USD to USD:    1000.00
```

A rate is a **dated fact**. Converting without an as-of date gives a number
nobody can reproduce: re-run last quarter's report and you silently get
today's rate. So `convert` takes the date and applies the rate *effective* on
it — the latest on or before — and `rate_on` tells you which one that was.

Inversion is refused: the two sides of a quote differ by a spread, so EUR→USD
is not `1/0.95`. Converting to the currency you already hold needs no rate, so
code that normalises a mixed list into one reporting currency just works.

---

## 6. What a loan actually costs

<!--CODE:06_a_loan-->

```basic
' Recipe 6 — What a loan actually costs.
'
' Rates here are PER PERIOD, not per year. A 6% annual loan paid monthly is
' `0.06 / 12`. That arithmetic is yours on purpose: compounding conventions
' differ by product and jurisdiction, and a library that guessed would be
' wrong somewhere without telling you.

program main(args)
  load finance

  principal {USD}= "250000.00"
  monthly = 0.06 / 12

  payment = finance.pmt(monthly, 360, principal)
  print "250,000 at 6% over 30 years"
  print "  monthly payment: " + string(payment)

  ' The sign convention is the spreadsheet one — money you pay is negative —
  ' because that is what you will check the answer against.
  print "  paid in total:   " + string(payment * 360)

  ' What is a stream of payments worth today?
  print ""
  rent {USD}= "-2500.00"
  print "a 5-year lease at 2,500/month, 6%/yr, is worth"
  print "  " + string(finance.pv(monthly, 60, rent)) + " today"

  ' How long would a given payment take?
  print ""
  ' Negative: the payment leaves you. Excel's convention throughout.
  pay {USD}= "-2000.00"
  print "paying 2,000/month instead clears it in "
  print "  " + string(round(finance.nper(monthly, pay, principal), 1)) + " months"
end program
```

<!--OUT:06_a_loan-->

```
250,000 at 6% over 30 years
  monthly payment: -1498.88
  paid in total:   -539595.47

a 5-year lease at 2,500/month, 6%/yr, is worth
  129313.90 today

paying 2,000/month instead clears it in 
  196.7 months
```

Rates are **per period**, not per year: a 6% annual loan paid monthly is
`0.06 / 12`. That arithmetic is yours on purpose — compounding conventions
differ by product and jurisdiction, and a library that guessed would be wrong
somewhere without telling you.

**Argument order is Excel's** — rate, then periods, then the amount — for the
same reason: `finance.pmt(0.06 / 12, 360, principal)` is `PMT(6%/12, 360,
250000)`, and every worked example you already own is written that way.

Sign follows the spreadsheet convention (money you pay is negative), because
that is what you will check the answer against. `finance.pmt` of 250,000 at
0.5%/month over 360 periods is `-1498.88` in Excel and LibreOffice too. Note
the payment passed to `finance.nper` above is negative for the same reason —
it leaves you.

Each of the five takes an optional tail you can ignore until you need it:
`fv` for a balloon balance still owed at the end, and `timing` of `"end"` (the
default) or `"begin"` for payments at the start of each period.

---

## 7. The schedule behind the payment

<!--CODE:07_amortization-->

```basic
' Recipe 7 — The schedule behind the payment.
'
' `finance.schedule` returns one record per period: the payment, how much of
' it is interest, how much reduces the balance, and what is left.
'
' THE LAST PAYMENT IS ADJUSTED so the balance lands exactly on zero. Every
' payment is whole minor units — you cannot pay a third of a cent — and those
' roundings accumulate, so a schedule using one figure throughout would end
' owing a few cents or having overpaid. Lenders do the same.

program main(args)
  load finance

  loan {USD}= "10000.00"
  rows = finance.schedule(0.005, 12, loan)

  print "period   payment  interest principal   balance"
  for each r in rows
    print pad(string(r.period), 6) + pad(string(r.payment), 10) + pad(string(r.interest), 9) + pad(string(r.principal), 10) + pad(string(r.balance), 10)
  next

  ' The properties worth checking, and they are arithmetic rather than
  ' eyeballed: the balance ends at zero and the principal parts sum to the loan.
  zero {USD}= "0.00"
  last = rows[count(rows) - 1]
  print ""
  print "final balance is zero:            " + string(last.balance = zero)

  paid {USD}= "0.00"
  interest {USD}= "0.00"
  for each r in rows
    paid = paid + r.principal
    interest = interest + r.interest
  next
  print "principal parts sum to the loan:  " + string(paid = loan)
  print "total interest paid:              " + string(interest)
end program

function pad(s, n)
  out = s
  while len(out) < n
    out = out + " "
  end while
  return out
end function
```

<!--OUT:07_amortization-->

```
period   payment  interest principal   balance
1     860.66    50.00    810.66    9189.34   
2     860.66    45.95    814.72    8374.62   
3     860.66    41.87    818.79    7555.83   
4     860.66    37.78    822.89    6732.94   
5     860.66    33.66    827.00    5905.94   
6     860.66    29.53    831.13    5074.81   
7     860.66    25.37    835.29    4239.52   
8     860.66    21.20    839.47    3400.05   
9     860.66    17.00    843.66    2556.39   
10    860.66    12.78    847.88    1708.50   
11    860.66    8.54     852.12    856.38    
12    860.66    4.28     856.38    0.00      

final balance is zero:            true
principal parts sum to the loan:  true
total interest paid:              327.97
```

**The last payment is adjusted so the balance lands exactly on zero.** Every
payment is whole minor units — you cannot pay a third of a cent — and those
roundings accumulate, so a schedule using one figure throughout would end
owing a few cents or having overpaid. Lenders do the same thing.

The two properties worth checking are arithmetic rather than eyeballed: the
final balance is zero, and the principal parts sum to the loan.

---

## 8. Is a project worth doing?

<!--CODE:08_is_it_worth_it-->

```basic
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
```

<!--OUT:08_is_it_worth_it-->

```
savings are worth today: 56861.80
the machine costs:       50000.00
net:                     6861.80

internal rate of return: 15.24%

at 5%:  64942.15
at 20%: 44859.18

one dollar back on 50,000: -99.998%

all-positive flows: finance.irr: no rate between -100% and 1000% makes these flows break even
```

Two questions, one equation solved for different unknowns: what future money
is worth today (`npv`), and what return it represents (`irr`). Compare the IRR
to what the money costs you.

`irr` uses bisection rather than Newton's method, which cannot diverge —
a wrong IRR is a plausible percentage somebody would act on. Flows that can
never repay the outlay are refused rather than returning a meaningless rate.

---

## 9. Writing an asset down

<!--CODE:09_depreciation-->

```basic
' Recipe 9 — Writing an asset down.
'
' Three conventions, and which you use is an accounting policy rather than a
' calculation: straight line spreads the cost evenly, and the other two
' front-load it.

program main(args)
  load finance

  cost {USD}= "50000.00"
  salvage {USD}= "5000.00"
  life = 10

  print "a 50,000 asset, 5,000 salvage, 10 years"
  print ""
  print "straight line, every year: " + string(finance.sln(cost, salvage, life))

  print ""
  print "year   sum-of-years  declining-balance"
  y = 1
  while y <= 5
    print pad(string(y), 7) + pad(string(finance.syd(cost, salvage, life, y)), 14) + string(finance.ddb(cost, salvage, life, y))
    y += 1
  end while

  ' Sum-of-years-digits spreads the whole depreciable base over the life, so
  ' the yearly charges add up to cost minus salvage exactly.
  total {USD}= "0.00"
  y = 1
  while y <= life
    total = total + finance.syd(cost, salvage, life, y)
    y += 1
  end while
  print ""
  print "sum-of-years total:  " + string(total)
  print "cost minus salvage:  " + string(cost - salvage)

  ' Declining balance is floored at the salvage value, so an asset is never
  ' written below what it is worth.
  print ""
  print "declining balance, year 10: " + string(finance.ddb(cost, salvage, life, 10))
end program

function pad(s, n)
  out = s
  while len(out) < n
    out = out + " "
  end while
  return out
end function
```

<!--OUT:09_depreciation-->

```
a 50,000 asset, 5,000 salvage, 10 years

straight line, every year: 4500.00

year   sum-of-years  declining-balance
1      8181.82       10000.00
2      7363.64       8000.00
3      6545.45       6400.00
4      5727.27       5120.00
5      4909.09       4096.00

sum-of-years total:  45000.00
cost minus salvage:  45000.00

declining balance, year 10: 1342.18
```

Which convention you use is an accounting policy, not a calculation.
Sum-of-years-digits spreads the whole depreciable base over the life, so the
charges add to cost minus salvage exactly. Declining balance is floored at
salvage, so an asset is never written below what it is worth.

---

## What is not here

- **Compound interest with irregular dates** (XNPV/XIRR). `npv` and `irr`
  assume evenly spaced periods.
- **Multi-currency portfolios.** Conversion is per amount; nothing aggregates
  a mixed list for you, though `money.convert` to a reporting currency is the
  building block.
- **Tax.** Rounding rules for tax are jurisdictional, and the default here is
  half-even. Where a jurisdiction mandates half-up, round explicitly.
