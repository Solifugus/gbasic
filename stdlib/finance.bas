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
    ' Each solves the SAME equation for a different unknown (design §3):
    '
    '     pv * (1+r)^n  +  pmt * ((1+r)^n - 1)/r * (1 + r*t)  +  fv  =  0
    '
    ' where t is 0 for payments at the END of each period and 1 for the
    ' BEGINNING. The r = 0 case is computed exactly rather than left to a limit.
    '
    ' ARGUMENT ORDER IS EXCEL'S (design §2): rate first, then periods, then the
    ' amount. The people who check these answers check them in a spreadsheet,
    ' and every reference example they own is written that way. The values and
    ' signs already agreed with Excel before the order did.
    '
    ' SIGN CONVENTION follows the spreadsheet one: money you RECEIVE is
    ' positive and money you PAY is negative. Borrowing 250,000 and repaying it
    ' means a positive pv and a negative pmt.

    function _check_timing(t, who)
        if type(t) != "string" or (t != "end" and t != "begin") then
            error who + " expects timing of \"end\" or \"begin\""
        end if
        return nothing
    end function

    ' 1 when payments fall at the START of the period, else 0.
    function _timing_flag(t)
        if t = "begin" then
            return 1
        end if
        return 0
    end function

    ' The annuity factor ((1+r)^n - 1)/r, adjusted for timing. At r = 0 the
    ' factor is exactly n -- taking the limit rather than dividing by zero.
    function _annuity_factor(r, n, t)
        if r = 0 then
            return n
        end if
        return ((_growth(r, n) - 1) / r) * (1 + r * _timing_flag(t))
    end function

    ' Every solver takes the same trailing pair, so a caller who does not care
    ' about a balloon balance or beginning-of-period payments writes neither.
    function _check_tvm(r, n, timing, who)
        _check_rate(r, who)
        _check_periods(n, who)
        _check_timing(timing, who)
        return nothing
    end function

    ' The payment per period. `pv` is what you receive now, `fv` what is still
    ' owed (or wanted) at the end.
    function pmt(rate, nper, pv, fv = 0, timing = "end")
        _check_tvm(rate, nper, timing, "finance.pmt")
        amounts = _pair(pv, fv, "finance.pmt")
        present = amounts[0]
        balloon = amounts[1]
        af = _annuity_factor(rate, nper, timing)
        return ((present * _growth(rate, nper) + balloon) / af) * -1
    end function

    ' What a stream of `pmt` payments (plus any `fv`) is worth today.
    function pv(rate, nper, pmt, fv = 0, timing = "end")
        _check_tvm(rate, nper, timing, "finance.pv")
        amounts = _pair(pmt, fv, "finance.pv")
        payment = amounts[0]
        balloon = amounts[1]
        af = _annuity_factor(rate, nper, timing)
        return ((payment * af + balloon) / _growth(rate, nper)) * -1
    end function

    ' What `pv` today plus `pmt` per period is worth after `nper` periods.
    '
    ' NOTE THE SHAPE CHANGED (design §7). This used to be the future value of a
    ' LUMP SUM -- fv(amount, r, n). It is now Excel's annuity form, so the old
    ' fv(c, 0.05, 10) is written fv(0.05, 10, 0, c).
    function fv(rate, nper, pmt, pv = 0, timing = "end")
        _check_tvm(rate, nper, timing, "finance.fv")
        amounts = _pair(pmt, pv, "finance.fv")
        payment = amounts[0]
        present = amounts[1]
        af = _annuity_factor(rate, nper, timing)
        return ((present * _growth(rate, nper) + payment * af) * -1)
    end function

    ' How many periods are needed. Returns a number, usually fractional -- the
    ' last payment is smaller than the rest.
    function nper(rate, pmt, pv, fv = 0, timing = "end")
        _check_rate(rate, "finance.nper")
        _check_timing(timing, "finance.nper")
        amounts = _pair(pmt, pv, "finance.nper")
        balloon = _pair(amounts[1], fv, "finance.nper")[1]
        a = number(string(amounts[0]))
        p = number(string(amounts[1]))
        f = number(string(balloon))
        if a = 0 then
            error "finance.nper needs a non-zero payment"
        end if
        if rate = 0 then
            return (0 - (p + f)) / a
        end if
        k = (1 + rate * _timing_flag(timing)) / rate
        denom = p + a * k
        numer = a * k - f
        if denom = 0 or (numer / denom) <= 0 then
            error "finance.nper: the payment never repays the present value at that rate"
        end if
        return log(numer / denom) / log(1 + rate)
    end function

    ' The rate per period that makes the equation balance -- the fifth solver,
    ' and the one that did not exist before.
    '
    ' BISECTION, NOT NEWTON, for the reason `irr` gives: it cannot diverge, and
    ' a wrong rate is a plausible-looking percentage someone would act on. Works
    ' in plain numbers because the search visits rates near -100% where a
    ' discount factor is about 1e-12 and dividing money by it overflows the
    ' type; a rate is a ratio anyway.
    function rate(nper, pmt, pv, fv = 0, timing = "end")
        _check_periods(nper, "finance.rate")
        _check_timing(timing, "finance.rate")
        amounts = _pair(pmt, pv, "finance.rate")
        balloon = _pair(amounts[1], fv, "finance.rate")[1]
        a = number(string(amounts[0]))
        p = number(string(amounts[1]))
        f = number(string(balloon))
        t = _timing_flag(timing)

        lo = -0.999999
        hi = 10.0
        f_lo = _tvm_residual(lo, nper, a, p, f, t)
        f_hi = _tvm_residual(hi, nper, a, p, f, t)
        if f_lo * f_hi > 0 then
            error "finance.rate: no rate between -100% and 1000% balances these terms"
        end if
        i = 0
        while i < 200
            mid = (lo + hi) / 2
            f_mid = _tvm_residual(mid, nper, a, p, f, t)
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

    ' The equation's left-hand side in plain numbers, for the rate search.
    function _tvm_residual(r, n, a, p, f, t)
        if r = 0 then
            return p + a * n + f
        end if
        g = pow(1 + r, n)
        return p * g + a * ((g - 1) / r) * (1 + r * t) + f
    end function

    ' The two amount arguments of a TVM call, with a bare 0 allowed for either.
    '
    ' Money has no literal form and a currency cannot be guessed, so `0` is read
    ' as "zero in the same currency as the OTHER amount" -- the only reading
    ' that cannot invent one. At least one of the pair must be real money.
    ' Excel writes the lump-sum case as FV(0.05, 10, 0, -1000) and this is what
    ' lets the same thing be written here.
    function _pair(x, y, who)
        xm = (type(x) = "money")
        ym = (type(y) = "money")
        if xm and ym then
            return [x, y]
        end if
        if xm and type(y) = "number" and y = 0 then
            return [x, x * 0]
        end if
        if ym and type(x) = "number" and x = 0 then
            return [y * 0, y]
        end if
        if not xm and not ym then
            error who + " needs at least one amount as money"
        end if
        error who + " expects money, or 0, for each amount"
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
    function schedule(rate, nper, pv)
        _check_rate(rate, "finance.schedule")
        _check_periods(nper, "finance.schedule")
        if type(pv) != "money" then
            error "finance.schedule expects the present value as money"
        end if
        principal = pv
        r = rate
        n = nper
        payment = pmt(rate, nper, pv) * -1
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
