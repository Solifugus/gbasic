' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finance.bas — the time value of money (docs/money_design.md §8).
'
' gBASIC's statistics library covers SECURITIES ANALYTICS — returns, Sharpe,
' drawdown, VaR, CAPM, event studies. What it has never had is the other half
' of finance, the half a line-of-business application actually computes: what
' a loan payment is, what a lease is worth today, whether a project earns its
' cost of capital, how an asset depreciates.
'
' AMOUNTS ARE MONEY, RATES ARE NUMBERS. A rate is a pure ratio with no
' currency and no minor unit, so 0.05 is five percent; an amount is `money`,
' which is exact and carries its currency. Keeping them distinct is what stops
' `pmt` returning a plausible number in no particular currency.
'
' PERIOD RATES, NOT ANNUAL ONES. Every function here takes the rate FOR ONE
' PERIOD. A 6% annual loan paid monthly is `0.06 / 12`, and making that the
' caller's arithmetic rather than a hidden convention is deliberate: the two
' differ, compounding conventions vary by product and jurisdiction, and a
' library that guessed would be wrong somewhere without saying so.
library finance

    ' --- internal ---------------------------------------------------------

    function _check_rate(r, who)
        if type(r) != "number" then
            error who + " expects the period rate as a number (0.05 is 5%)"
        end if
        if r <= -1 then
            error who + " needs a period rate above -100%"
        end if
        return nothing
    end function

    function _check_periods(n, who)
        if type(n) != "number" or n <= 0 or n != floor(n) then
            error who + " expects a whole number of periods, 1 or more"
        end if
        return nothing
    end function

    ' (1 + r)^n, with the r = 0 case exact rather than left to pow().
    function _growth(r, n)
        if r = 0 then
            return 1
        end if
        return pow(1 + r, n)
    end function

    ' --- the five time-value quantities ------------------------------------
    '
    ' Each solves the same equation for a different unknown:
    '
    '     pv * (1+r)^n  +  pmt * ((1+r)^n - 1) / r  +  fv  =  0
    '
    ' SIGN CONVENTION follows the spreadsheet one, because that is what a
    ' finance person will check the answer against: money you RECEIVE is
    ' positive and money you PAY is negative. Borrowing 250,000 and repaying
    ' it means a positive pv and a negative pmt.

    ' The payment per period that repays `principal` over `n` periods at `r`.
    function pmt(principal, r, n)
        _check_rate(r, "finance.pmt")
        _check_periods(n, "finance.pmt")
        if type(principal) != "money" then
            error "finance.pmt expects the principal as money"
        end if
        if r = 0 then
            return (principal / n) * -1
        end if
        g = _growth(r, n)
        ' principal * r * g / (g - 1), negated: a repayment leaves the borrower
        return ((principal * (r * g)) / (g - 1)) * -1
    end function

    ' What a stream of `n` payments of `pmt_amount` is worth today at `r`.
    function pv(pmt_amount, r, n)
        _check_rate(r, "finance.pv")
        _check_periods(n, "finance.pv")
        if type(pmt_amount) != "money" then
            error "finance.pv expects the payment as money"
        end if
        if r = 0 then
            return (pmt_amount * n) * -1
        end if
        g = _growth(r, n)
        return ((pmt_amount * ((g - 1) / r)) / g) * -1
    end function

    ' What `amount` today is worth after `n` periods at `r`.
    function fv(amount, r, n)
        _check_rate(r, "finance.fv")
        _check_periods(n, "finance.fv")
        if type(amount) != "money" then
            error "finance.fv expects an amount as money"
        end if
        return amount * _growth(r, n)
    end function

    ' How many periods of `payment` clear `principal` at `r`. Returns a number,
    ' which is usually fractional -- the last payment is smaller.
    function nper(principal, payment, r)
        _check_rate(r, "finance.nper")
        if type(principal) != "money" or type(payment) != "money" then
            error "finance.nper expects money for the principal and the payment"
        end if
        p = number(string(principal))
        a = abs(number(string(payment)))
        if a <= 0 then
            error "finance.nper needs a non-zero payment"
        end if
        if r = 0 then
            return p / a
        end if
        if a <= p * r then
            ' The payment never covers the interest, so the balance grows.
            error "finance.nper: the payment never repays the principal at that rate"
        end if
        return log(a / (a - p * r)) / log(1 + r)
    end function

    ' --- project appraisal --------------------------------------------------

    ' Net present value of `flows` (an array of money, one per period,
    ' starting at period 1) discounted at `r`, plus an optional period-0 sum.
    function npv(r, flows)
        _check_rate(r, "finance.npv")
        if type(flows) != "array" or count(flows) = 0 then
            error "finance.npv expects a non-empty array of money"
        end if
        total = flows[0] * 0
        i = 0
        while i < count(flows)
            f = flows[i]
            if type(f) != "money" then
                error "finance.npv expects every flow to be money"
            end if
            total = total + (f / _growth(r, i + 1))
            i += 1
        end while
        return total
    end function

    ' NPV over plain numbers, for the rate search. Kept separate from the
    ' money-valued `npv` so neither has to compromise: one is exact, one can
    ' visit rates that would overflow the money type.
    function _npv_number(r, nums)
        total = 0.0
        i = 0
        while i < count(nums)
            total = total + nums[i] / pow(1 + r, i + 1)
            i += 1
        end while
        return total
    end function

    ' The rate at which the flows repay `outlay` exactly.
    '
    ' WORKS IN PLAIN NUMBERS, not money, and that is deliberate rather than
    ' lazy: the search visits rates near -100%, where the discount factor is
    ' about 1e-12 and dividing money by it overflows the type outright. A rate
    ' is a ratio anyway -- the money only matters at the ends, and the answer
    ' is a number. (Found by the first version raising "money value is out of
    ' range" on an ordinary three-year project.)
    '
    ' Bisection rather than Newton: it cannot diverge, and a wrong IRR is a
    ' plausible-looking percentage that would be acted on.
    function irr(outlay, flows)
        if type(outlay) != "money" then
            error "finance.irr expects the outlay as money"
        end if
        if type(flows) != "array" or count(flows) = 0 then
            error "finance.irr expects a non-empty array of money"
        end if
        target = number(string(outlay))
        if target <= 0 then
            error "finance.irr expects a positive outlay"
        end if
        nums = []
        for each f in flows
            if type(f) != "money" then
                error "finance.irr expects every flow to be money"
            end if
            append(nums, number(string(f)))
        next

        lo = -0.99
        hi = 10.0
        f_lo = _npv_number(lo, nums) - target
        f_hi = _npv_number(hi, nums) - target
        if f_lo * f_hi > 0 then
            error "finance.irr: no rate between -99% and 1000% makes these flows break even"
        end if
        i = 0
        while i < 200
            mid = (lo + hi) / 2
            f_mid = _npv_number(mid, nums) - target
            if f_lo * f_mid <= 0 then
                hi = mid
            else
                lo = mid
                f_lo = f_mid
            end if
            i += 1
        end while
        return (lo + hi) / 2
    end function

    ' --- schedules ----------------------------------------------------------

    ' A full amortization schedule: one record per period with the payment
    ' split into interest and principal, and the balance after it.
    '
    ' THE LAST PAYMENT IS ADJUSTED so the balance lands exactly on zero. Every
    ' period's payment is rounded to whole minor units -- you cannot pay a
    ' third of a cent -- and those roundings accumulate, so a schedule that
    ' used the same figure throughout would end owing a few cents or having
    ' overpaid. Lenders do the same thing.
    function schedule(principal, r, n)
        _check_rate(r, "finance.schedule")
        _check_periods(n, "finance.schedule")
        if type(principal) != "money" then
            error "finance.schedule expects the principal as money"
        end if
        payment = pmt(principal, r, n) * -1
        rows = []
        balance = principal
        i = 1
        while i <= n
            interest = balance * r
            due = payment
            if i = n then
                ' Final period: clear whatever is actually left.
                due = balance + interest
            end if
            principal_part = due - interest
            balance = balance - principal_part
            append(rows, { period: i,
                           payment: due,
                           interest: interest,
                           principal: principal_part,
                           balance: balance })
            i += 1
        end while
        return rows
    end function

    ' --- depreciation --------------------------------------------------------

    ' Straight line: the same amount every period.
    function sln(cost, salvage, life)
        _check_periods(life, "finance.sln")
        if type(cost) != "money" or type(salvage) != "money" then
            error "finance.sln expects cost and salvage as money"
        end if
        return (cost - salvage) / life
    end function

    ' Sum-of-years-digits: front-loaded, and the digits sum to life*(life+1)/2.
    function syd(cost, salvage, life, period)
        _check_periods(life, "finance.syd")
        _check_periods(period, "finance.syd")
        if period > life then
            error "finance.syd: the period is past the asset's life"
        end if
        if type(cost) != "money" or type(salvage) != "money" then
            error "finance.syd expects cost and salvage as money"
        end if
        digits = life * (life + 1) / 2
        return (cost - salvage) * ((life - period + 1) / digits)
    end function

    ' Double-declining balance, floored at the salvage value so an asset is
    ' never depreciated below what it is worth.
    function ddb(cost, salvage, life, period)
        _check_periods(life, "finance.ddb")
        _check_periods(period, "finance.ddb")
        if type(cost) != "money" or type(salvage) != "money" then
            error "finance.ddb expects cost and salvage as money"
        end if
        rate = 2 / life
        book = cost
        i = 1
        result = cost * 0
        while i <= period
            charge = book * rate
            floor_left = book - salvage
            if charge > floor_left then
                charge = floor_left
            end if
            if charge < (cost * 0) then
                charge = cost * 0
            end if
            book = book - charge
            result = charge
            i += 1
        end while
        return result
    end function

end library
