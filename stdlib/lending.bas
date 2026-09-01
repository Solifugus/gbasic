' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' lending.bas — loans, servicing and payoff (docs/lending_design.md).
'
' PHASE 1 ANSWERS WHAT THE PAYMENT IS; this answers what happens next. A
' borrower pays late, partly or extra; a rate changes; the loan is paid off on
' the 14th. Every one of those is a POLICY decision and every one moves the
' balance, which is why this is a library and not a function: the arithmetic is
' small and the conventions are the whole subject.
'
' THE LOAN DECLARES ITS CONVENTIONS AND THE LIBRARY NEVER ASSUMES -- the same
' rule `finance` set for period rates. Accrual basis, payment waterfall and day
' count each change the answer, and none has a dominant default.
'
' `apply` IS A FOLD over the event list, not an incremental step. State is
' derived from the record of what happened, so "why is this balance what it is"
' is answerable by replaying it. Stored incremental state makes the number
' itself the answer, and if it is ever wrong there is nothing to reconstruct it
' from.
library lending

    load finance from "finance.bas"
    load dates from "dates.bas"

    ' --- the loan record -----------------------------------------------------

    function _known_basis(b)
        return b = "amortized" or b = "daily_simple"
    end function

    function _known_waterfall(w)
        return w = "fees_interest_principal" or w = "interest_principal_fees"
    end function

    ' Validate a loan and fill nothing in. A missing convention is REFUSED
    ' rather than defaulted: amortized and daily-simple accrual are different
    ' loans, and a guess is wrong by a few dollars a month, compounding, with
    ' nothing to show for it on screen.
    function loan(spec)
        if type(spec) != "record" then
            error "lending.loan expects a record"
        end if
        for each field in ["principal", "rate", "term", "opened", "basis",
                           "waterfall", "day_count"]
            if is_unknown(spec[field]) then
                error "lending.loan needs a " + field
            end if
        next
        if type(spec["principal"]) != "money" then
            error "lending.loan expects the principal as money"
        end if
        if type(spec["rate"]) != "number" or spec["rate"] < 0 then
            error "lending.loan expects an annual rate as a number, 0 or more"
        end if
        if (type(spec["term"]) != "number" or spec["term"] <= 0
            or spec["term"] != floor(spec["term"])) then
            error "lending.loan expects a whole number of periods"
        end if
        if type(spec["opened"]) != "datetime" then
            error "lending.loan expects an opening date"
        end if
        if not _known_basis(spec["basis"]) then
            error ("lending.loan: basis must be \"amortized\" or \"daily_simple\""
                   + " -- they are different loans and neither is the default")
        end if
        if not _known_waterfall(spec["waterfall"]) then
            error ("lending.loan: waterfall must be \"fees_interest_principal\""
                   + " or \"interest_principal_fees\"")
        end if
        ' The day count is validated by finance.year_fraction, which already
        ' refuses an unknown one by name.
        probe_a {date}= "2026-01-01"
        probe_b {date}= "2026-02-01"
        checked = finance.year_fraction(probe_a, probe_b, spec["day_count"])
        periods = 12
        if not is_unknown(spec["periods_per_year"]) then
            periods = spec["periods_per_year"]
        end if
        return { principal: spec["principal"], rate: spec["rate"],
                 term: spec["term"], opened: spec["opened"],
                 basis: spec["basis"], waterfall: spec["waterfall"],
                 day_count: spec["day_count"], periods_per_year: periods }
    end function

    ' The period rate this loan accrues at.
    function period_rate(l)
        return finance.periodic(l.rate, l.periods_per_year)
    end function

    ' The scheduled payment, and the full amortization schedule, both in the
    ' loan's own terms. `finance.schedule` already adjusts the final payment so
    ' the balance lands exactly on zero.
    function payment(l)
        return finance.pmt(period_rate(l), l.term, l.principal) * -1
    end function

    function schedule(l)
        return finance.schedule(period_rate(l), l.term, l.principal)
    end function

    ' --- servicing -----------------------------------------------------------
    '
    ' `apply` is a FOLD: the state is a pure function of the loan and its event
    ' list, so "why is this balance what it is" is answerable by replaying the
    ' record. An event is
    '
    '     { on: date, kind: "payment"|"fee"|"rate_change", amount: money|number }
    '
    ' and events are applied in date order.

    function _zero(l)
        return l.principal * 0
    end function

    ' Interest owed for the days between two dates, on a balance.
    '
    ' THE TWO BASES ARE DIFFERENT LOANS, which is the whole reason the loan has
    ' to name one. Daily simple interest accrues on the ACTUAL balance for the
    ' ACTUAL days, so paying five days early saves five days of interest.
    ' Amortized accrues the scheduled period interest regardless of when the
    ' payment lands, so paying early saves nothing.
    function _accrue(l, balance, from_d, to_d)
        if balance <= _zero(l) then
            return _zero(l)
        end if
        if l.basis = "daily_simple" then
            fraction = finance.year_fraction(from_d, to_d, l.day_count)
            return balance * (l.rate * fraction)
        end if
        ' Amortized: ONE PERIOD'S INTEREST PER WHOLE PERIOD ELAPSED, and
        ' nothing for a part period. That is the distinction from daily simple
        ' interest and the entire reason the loan must name a basis -- a
        ' payment five days early saves five days on a daily-simple loan and
        ' NOTHING on an amortized one.
        '
        ' Prorating by days here would make the two algebraically identical
        ' (balance * rate * days/365 either way), which is exactly what the
        ' first version of this function did: the bases agreed to the cent and
        ' the basis was decorative. Caught by the test the design asked for --
        ' the same loan and payments under the two bases must give DIFFERENT
        ' balances, and the difference must be the days.
        ' Periods are counted from ORIGINATION and differenced, not counted
        ' within the gap. Counting per gap means two events 14 and 17 days
        ' apart each round down to zero periods and NO INTEREST EVER ACCRUES --
        ' a weekly-payment loan would run free forever. That was the second
        ' defect in this function, and like the first it was invisible to a
        ' single-payment test.
        elapsed = _periods_at(l, to_d) - _periods_at(l, from_d)
        if elapsed <= 0 then
            return _zero(l)
        end if
        return balance * (period_rate(l) * elapsed)
    end function

    ' Whole periods elapsed between origination and a date.
    function _periods_at(l, d)
        days = dates.days_between(l.opened, d)
        if days <= 0 then
            return 0
        end if
        return floor(days / (365 / l.periods_per_year))
    end function

    ' Apply one payment through the loan's declared waterfall.
    '
    ' THE ORDER CHANGES THE BALANCE, the interest that accrues next period, and
    ' whether the loan reports delinquent -- so it is declared, never assumed.
    function _apply_payment(l, st, amount)
        left = amount
        fees = st.fees_due
        interest = st.accrued
        principal_paid = _zero(l)
        fees_paid = _zero(l)
        interest_paid = _zero(l)

        if l.waterfall = "fees_interest_principal" then
            take = _min_money(left, fees)
            fees_paid = take
            left = left - take
            take = _min_money(left, interest)
            interest_paid = take
            left = left - take
        else
            take = _min_money(left, interest)
            interest_paid = take
            left = left - take
            ' interest_principal_fees: principal before fees.
            take = _min_money(left, st.balance)
            principal_paid = take
            left = left - take
            take = _min_money(left, fees)
            fees_paid = take
            left = left - take
        end if

        if l.waterfall = "fees_interest_principal" then
            take = _min_money(left, st.balance)
            principal_paid = take
            left = left - take
        end if

        ' Anything still left overpays the loan, which is refused rather than
        ' becoming a negative balance -- an overpayment is a refund.
        if left > _zero(l) then
            error ("lending.apply: payment exceeds what is owed by "
                   + string(left) + " -- an overpayment is a refund, not a"
                   + " negative balance")
        end if

        return { balance: st.balance - principal_paid,
                 accrued: st.accrued - interest_paid,
                 fees_due: st.fees_due - fees_paid,
                 paid_principal: st.paid_principal + principal_paid,
                 paid_interest: st.paid_interest + interest_paid,
                 paid_fees: st.paid_fees + fees_paid }
    end function

    function _min_money(a, b)
        if a < b then
            return a
        end if
        return b
    end function

    ' Fold the events. `want_history` is opt-in: returning it always would make
    ' a portfolio scan O(n) in memory per loan for a field it does not read.
    function apply(l, events, want_history)
        if type(events) != "array" then
            error "lending.apply expects an array of events"
        end if
        st = { balance: l.principal, accrued: _zero(l), fees_due: _zero(l),
               paid_principal: _zero(l), paid_interest: _zero(l),
               paid_fees: _zero(l) }
        at = l.opened
        history = []
        rate_now = l.rate
        working = l
        for i = 0 to count(events) - 1
            e = events[i]
            if is_unknown(e["on"]) or type(e["on"]) != "datetime" then
                error "lending.apply: event " + string(i + 1) + " needs a date"
            end if
            if e["on"] < at then
                error ("lending.apply: event " + string(i + 1)
                       + " is dated before the previous one; events must be in order")
            end if
            ' Accrue up to this event, then act on it.
            st.accrued = st.accrued + _accrue(working, st.balance, at, e["on"])
            at = e["on"]
            kind = e["kind"]
            if kind = "payment" then
                st = _apply_payment(working, st, e["amount"])
            else
                if kind = "fee" then
                    st.fees_due = st.fees_due + e["amount"]
                else
                    if kind = "rate_change" then
                        if e["on"] < l.opened then
                            error "lending.apply: a rate change cannot predate the loan"
                        end if
                        working = { principal: working.principal, rate: e["amount"],
                                    term: working.term, opened: working.opened,
                                    basis: working.basis, waterfall: working.waterfall,
                                    day_count: working.day_count,
                                    periods_per_year: working.periods_per_year }
                    else
                        error ("lending.apply: unknown event kind "
                               + string(kind) + " -- use payment, fee or rate_change")
                    end if
                end if
            end if
            if want_history then
                append(history, { on: at, kind: kind, balance: st.balance,
                                  accrued: st.accrued, fees_due: st.fees_due })
            end if
        end for
        out = { balance: st.balance, accrued: st.accrued, fees_due: st.fees_due,
                paid_principal: st.paid_principal, paid_interest: st.paid_interest,
                paid_fees: st.paid_fees, last_accrued_to: at, rate: working.rate }
        if want_history then
            out["history"] = history
        end if
        return out
    end function

    ' What closes the loan on a date, including interest accrued since the last
    ' event -- the per-diem a payoff quote is made of.
    ' `as_of`, not `on`: `on` is a reserved word and cannot be a parameter name.
    function payoff(l, events, as_of)
        st = apply(l, events, false)
        if as_of < st.last_accrued_to then
            error "lending.payoff: the date is before the last event"
        end if
        extra = _accrue(l, st.balance, st.last_accrued_to, as_of)
        return { principal: st.balance, interest: st.accrued + extra,
                 fees: st.fees_due,
                 total: st.balance + st.accrued + extra + st.fees_due,
                 per_diem: _accrue(l, st.balance, as_of, _one_day_after(as_of)) }
    end function

    function _one_day_after(d)
        return d + (1 * 1 days)
    end function

    ' --- underwriting --------------------------------------------------------
    '
    ' A MISSING INPUT RETURNS `unknown`, NOT ZERO. These feed credit decisions,
    ' and an absent income figure that silently became zero would make every
    ' ratio look either perfect or catastrophic. A caller who wants a refusal
    ' asks for one.

    function _ratio(numerator, denominator)
        if type(numerator) != "money" or type(denominator) != "money" then
            return unknown
        end if
        if denominator = (denominator * 0) then
            return unknown
        end if
        return number(string(numerator)) / number(string(denominator))
    end function

    function ltv(loan_amount, value)
        return _ratio(loan_amount, value)
    end function

    function dti(monthly_debt, monthly_income)
        return _ratio(monthly_debt, monthly_income)
    end function

    function payment_to_income(monthly_payment, monthly_income)
        return _ratio(monthly_payment, monthly_income)
    end function

    ' Debt service coverage: income over debt service, so ABOVE one is healthy
    ' -- the inverse orientation of the three above, which is a real trap and
    ' the reason it is named rather than folded into `dti`.
    function dscr(net_operating_income, debt_service)
        return _ratio(net_operating_income, debt_service)
    end function

    ' --- the accounting boundary ---------------------------------------------
    '
    ' EMITS entries; never posts them. The caller owns the ledger, so an
    ' application posts to its own chart, batches, or discards. `accounts` maps
    ' the roles onto the caller's own codes.
    '
    ' This is also the test that proves the arithmetic: a loan's whole life
    ' posted to a real ledger must leave it balanced, with receivables equal to
    ' the outstanding balance. An unbalanced entry or a phantom account is
    ' refused where it is posted, so a loan that posts cleanly has DEMONSTRATED
    ' its arithmetic rather than asserted it.
    function entries(chart_v, l, events, accounts)
        for each role in ["receivable", "cash", "interest_income", "fee_income"]
            if is_unknown(accounts[role]) then
                error "lending.entries needs an account for " + role
            end if
        next
        load accounting from "accounting.bas"
        out = []
        ' Origination: the borrower owes us the principal, and cash goes out.
        append(out, accounting.entry(chart_v, l.opened, "Loan advance",
                 [{ account: accounts["receivable"], debit: l.principal },
                  { account: accounts["cash"], credit: l.principal }]))
        ' One entry per event, each derived by folding the events UP TO that
        ' point, so the entries and the balance cannot disagree -- they come
        ' from the same function.
        '
        ' KNOWN COST: that is O(n^2) in the event count, since each prefix is
        ' folded from the start. At a loan's scale it is nothing (360 payments
        ' is 65k cheap operations); at a portfolio's it would matter, and the
        ' remedy is the checkpoint the design already names -- `apply` taking a
        ' starting state. Recorded rather than optimised, because the shape is
        ' right and the measurement does not yet justify complicating it.
        previous = { paid_principal: l.principal * 0,
                     paid_interest: l.principal * 0,
                     paid_fees: l.principal * 0 }
        for i = 0 to count(events) - 1
            slice = []
            for k = 0 to i
                append(slice, events[k])
            end for
            now = apply(l, slice, false)
            e = events[i]
            if e["kind"] = "payment" then
                lines = [{ account: accounts["cash"], debit: e["amount"] }]
                principal_part = now.paid_principal - previous.paid_principal
                interest_part = now.paid_interest - previous.paid_interest
                fee_part = now.paid_fees - previous.paid_fees
                if principal_part != (principal_part * 0) then
                    append(lines, { account: accounts["receivable"], credit: principal_part })
                end if
                if interest_part != (interest_part * 0) then
                    append(lines, { account: accounts["interest_income"], credit: interest_part })
                end if
                if fee_part != (fee_part * 0) then
                    append(lines, { account: accounts["fee_income"], credit: fee_part })
                end if
                append(out, accounting.entry(chart_v, e["on"], "Payment received", lines))
            end if
            previous = { paid_principal: now.paid_principal,
                         paid_interest: now.paid_interest,
                         paid_fees: now.paid_fees }
        end for
        return out
    end function

end library
