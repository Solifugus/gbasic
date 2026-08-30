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

    ' Day-count conventions need calendar arithmetic. Loaded INSIDE the library
    ' and by explicit filename, which is the stdlib idiom (grid.bas and
    ' consolidate.bas load frame.bas the same way) -- a bare top-level `load`
    ' searches the calling program's own directory first, so naming the file is
    ' what keeps a stray dates.bas beside an application from replacing this one.
    load dates from "dates.bas"

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
        for i = 1 to 200
            mid = (lo + hi) / 2
            f_mid = _tvm_residual(mid, nper, a, p, f, t)
            if f_lo * f_mid <= 0 then
                hi = mid
            else
                lo = mid
                f_lo = f_mid
            end if
        end for
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

    ' --- day count conventions ---------------------------------------------
    '
    ' What fraction of a year lies between two dates. Every accrual, coupon and
    ' interest calculation rests on this, and the conventions DISAGREE by enough
    ' to matter -- the same two dates are 0.163889 of a year under Actual/360 and
    ' 0.166667 under 30/360, which on a million-dollar balance is real money.
    '
    ' THERE IS NO DEFAULT. No convention is dominant across products, and one
    ' guessed silently would be wrong somewhere without saying so -- the same
    ' rule the period-rate decision follows.

    ' `mod(a, b)`, not an infix `%` -- gBASIC has no `%` operator at all, and
    ' `y % 4` lexes as something else entirely.
    function _is_leap(y)
        if mod(y, 400) = 0 then
            return true
        end if
        if mod(y, 100) = 0 then
            return false
        end if
        return mod(y, 4) = 0
    end function

    function _days_in_year(y)
        if _is_leap(y) then
            return 366
        end if
        return 365
    end function

    function _jan1(y)
        d {date}= string(y) + "-01-01"
        return d
    end function

    ' The 30/360 US (bond basis) day adjustment, which is where this convention
    ' earns its reputation: a month is 30 days by fiat, and the end-of-month
    ' rules decide what happens when the calendar disagrees.
    function _thirty_360_us(d_from, d_to)
        d1 = d_from.day
        d2 = d_to.day
        ' Both ends on the last day of February collapse to 30.
        if _is_eom_feb(d_from) and _is_eom_feb(d_to) then
            d2 = 30
        end if
        if _is_eom_feb(d_from) then
            d1 = 30
        end if
        if d1 = 31 then
            d1 = 30
        end if
        if d2 = 31 and d1 = 30 then
            d2 = 30
        end if
        return (360 * (d_to.year - d_from.year) + 30 * (d_to.month - d_from.month) + (d2 - d1)) / 360
    end function

    function _is_eom_feb(d)
        if d.month != 2 then
            return false
        end if
        if _is_leap(d.year) then
            return d.day = 29
        end if
        return d.day = 28
    end function

    ' Actual/Actual ISDA: each day is weighted by the length of the year it
    ' falls in, so a period spanning a leap year is not the same as one that
    ' does not. Split at the year boundaries and weight each piece.
    function _act_act_isda(d_from, d_to)
        if d_from.year = d_to.year then
            return dates.days_between(d_from, d_to) / _days_in_year(d_from.year)
        end if
        total = dates.days_between(d_from, _jan1(d_from.year + 1)) / _days_in_year(d_from.year)
        for y = d_from.year + 1 to d_to.year - 1
            total = total + 1
        end for
        return total + (dates.days_between(_jan1(d_to.year), d_to) / _days_in_year(d_to.year))
    end function

    ' `convention` is one of "actual/360", "actual/365", "actual/actual" or
    ' "30/360". Spelled out rather than numbered: Excel's YEARFRAC takes a
    ' `basis` of 0 to 4 and nobody remembers which is which.
    function year_fraction(d_from, d_to, convention)
        if type(d_from) != "datetime" or type(d_to) != "datetime" then
            error "finance.year_fraction expects two dates"
        end if
        if convention = "actual/360" then
            return dates.days_between(d_from, d_to) / 360
        end if
        if convention = "actual/365" then
            return dates.days_between(d_from, d_to) / 365
        end if
        if convention = "actual/actual" then
            return _act_act_isda(d_from, d_to)
        end if
        if convention = "30/360" then
            return _thirty_360_us(d_from, d_to)
        end if
        error ("finance.year_fraction: unknown convention \"" + string(convention)
               + "\" -- use actual/360, actual/365, actual/actual or 30/360")
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
        for i = 0 to count(flows) - 1
            f = flows[i]
            if type(f) != "money" then
                error "finance.npv expects every flow to be money"
            end if
            total = total + (f / _growth(r, i + 1))
        end for
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
    ' The rate at which a series of period-indexed flows breaks even.
    '
    ' EXCEL'S SHAPE (design §7): `values[0]` is the period-0 flow, normally the
    ' outlay and normally negative. The outlay used to be a separate argument,
    ' which reads well but is not what a spreadsheet does and does not line up
    ' with `xirr`, where the dates array must match the values array including
    ' period 0.
    function irr(values)
        nums = _flow_numbers(values, "finance.irr")
        _warn_if_multiple_irr(nums, "finance.irr")
        return _solve_rate(nums, _period_offsets(count(nums)), "finance.irr")
    end function

    ' Periodic flows are dated flows whose offsets happen to be 0, 1, 2, ...
    ' so both rate searches are the same function.
    function _period_offsets(n)
        offsets = []
        for i = 0 to n - 1
            append(offsets, i)
        end for
        return offsets
    end function

    function _flow_numbers(values, who)
        if type(values) != "array" or count(values) = 0 then
            error who + " expects a non-empty array of money"
        end if
        nums = []
        for each f in values
            if type(f) != "money" then
                error who + " expects every flow to be money"
            end if
            append(nums, number(string(f)))
        next
        return nums
    end function

    function _npv_number(r, nums, offsets)
        total = 0.0
        for i = 0 to count(nums) - 1
            total = total + nums[i] / pow(1 + r, offsets[i])
        end for
        return total
    end function

    ' Find a rate where the present value is zero.
    '
    ' SCANS FOR A BRACKET FIRST, and that is not defensive padding -- it is
    ' required for correctness whenever the flows change sign more than once.
    ' Testing only the two endpoints assumes the curve crosses zero an odd
    ' number of times: [-1000, 5000, -6000] has roots at 100% AND 200%, dips
    ' back, and is NEGATIVE at both ends, so an endpoint-only test concludes
    ' "no rate makes these flows break even" -- a confident refusal about
    ' something that demonstrably exists. Walking a grid finds a neighbouring
    ' pair that straddles a root, then bisects inside it.
    '
    ' Bisection rather than Newton: it cannot diverge, and a wrong rate is a
    ' plausible-looking percentage that would be acted on.
    function _solve_rate(nums, offsets, who)
        lo = -0.999999
        hi = 10.0
        steps = 400
        width = (hi - lo) / steps
        a = lo
        f_a = _npv_number(a, nums, offsets)
        found = false
        for i = 1 to steps
            b = lo + width * i
            f_b = _npv_number(b, nums, offsets)
            if f_a = 0 then
                return a
            end if
            if f_a * f_b < 0 then
                lo = a
                hi = b
                found = true
                break
            end if
            a = b
            f_a = f_b
        end for
        if not found then
            error who + ": no rate between -100% and 1000% makes these flows break even"
        end if
        f_lo = _npv_number(lo, nums, offsets)
        for i = 1 to 200
            mid = (lo + hi) / 2
            f_mid = _npv_number(mid, nums, offsets)
            if f_lo * f_mid <= 0 then
                hi = mid
            else
                lo = mid
                f_lo = f_mid
            end if
        end for
        return (lo + hi) / 2
    end function

    ' MORE THAN ONE SIGN CHANGE ADMITS MORE THAN ONE RATE.
    '
    ' Descartes' rule of signs: a polynomial has at most as many positive roots
    ' as its coefficients have sign changes. A conventional project -- pay once,
    ' receive thereafter -- changes sign once and has exactly one IRR. A project
    ' that changes sign again (a mid-life reinvestment, a decommissioning cost)
    ' can have several, and the search returns whichever the first bracket
    ' contains. That is a plausible percentage someone would act on, so it is
    ' said out loud.
    '
    ' A WARNING, NOT A REFUSAL: the answer returned is a real root and is often
    ' the one wanted. Refusing would block a legitimate calculation; staying
    ' silent would hand over a number with a hidden assumption. `on warning
    ' ignore` is there for a caller who has already thought about it.
    function _warn_if_multiple_irr(nums, who)
        changes = 0
        previous = 0
        for each v in nums
            if v > 0 then
                if previous = -1 then
                    changes += 1
                end if
                previous = 1
            end if
            if v < 0 then
                if previous = 1 then
                    changes += 1
                end if
                previous = -1
            end if
        next
        if changes > 1 then
            warning(who + ": these flows change sign " + string(changes)
                    + " times, so more than one rate can satisfy them"
                    + " -- the one returned is a root, not necessarily the only one")
        end if
        return nothing
    end function

    ' --- dated cash flows ---------------------------------------------------
    '
    ' XNPV and XIRR: flows that fall on ACTUAL DATES rather than on regular
    ' periods, which is what a real project throws off. Discounting uses
    ' Actual/365 from the FIRST date, which is Excel's definition -- not a
    ' choice this library gets to make, since the whole point is to agree with
    ' the spreadsheet the answer will be checked in.

    function _check_dated(values, dates_list, who)
        if type(values) != "array" or count(values) = 0 then
            error who + " expects a non-empty array of money"
        end if
        if type(dates_list) != "array" or count(dates_list) != count(values) then
            error who + " expects one date per flow"
        end if
        for i = 0 to count(values) - 1
            if type(values[i]) != "money" then
                error who + " expects every flow to be money"
            end if
            if type(dates_list[i]) != "datetime" then
                error who + " expects every date to be a date"
            end if
            if i > 0 and dates_list[i] < dates_list[i - 1] then
                error who + " expects the dates in ascending order"
            end if
        end for
        return nothing
    end function

    ' Year offsets from the first date, Actual/365.
    function _date_offsets(dates_list)
        offsets = []
        for i = 0 to count(dates_list) - 1
            append(offsets, dates.days_between(dates_list[0], dates_list[i]) / 365)
        end for
        return offsets
    end function

    ' Net present value of dated flows. Returns money, in the flows' currency.
    function xnpv(rate, values, dates_list)
        _check_rate(rate, "finance.xnpv")
        _check_dated(values, dates_list, "finance.xnpv")
        offsets = _date_offsets(dates_list)
        total = values[0] * 0
        for i = 0 to count(values) - 1
            total = total + (values[i] / pow(1 + rate, offsets[i]))
        end for
        return total
    end function

    ' The rate at which dated flows break even.
    function xirr(values, dates_list)
        _check_dated(values, dates_list, "finance.xirr")
        nums = _flow_numbers(values, "finance.xirr")
        _warn_if_multiple_irr(nums, "finance.xirr")
        return _solve_rate(nums, _date_offsets(dates_list), "finance.xirr")
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
