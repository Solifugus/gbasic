' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' deposits.bas — deposit interest, crediting and certificates
' (docs/lending_design.md §7).
'
' THE MIRROR OF LENDING, and a separate library on purpose: `waterfall`,
' `delinquency` and per-diem mean nothing to a savings account, and `tier`,
' `crediting schedule` and early-withdrawal penalty mean nothing to a mortgage.
' What the two genuinely share is simple interest over a day count, and that
' lives in `finance` where both can reach it -- a deposit does not borrow.
'
' THE ACCOUNT DECLARES ITS BALANCE METHOD and the library never assumes, the
' same rule lending follows for accrual basis. Daily, average-daily and minimum
' balance give DIFFERENT interest on the same money and the same activity,
' which is the whole reason the choice is the product's and not ours.
library deposits

    load finance from "finance.bas"
    load dates from "dates.bas"

    function _known_method(m)
        return m = "daily" or m = "average_daily" or m = "minimum"
    end function

    ' Validate an account. A missing balance method is REFUSED rather than
    ' defaulted: the three differ on the same activity, and picking one for the
    ' caller would be inventing the product's terms.
    function account(spec)
        if type(spec) != "record" then
            error "deposits.account expects a record"
        end if
        for each field in ["opened", "balance", "rate", "day_count",
                           "balance_method", "crediting"]
            if is_unknown(spec[field]) then
                error "deposits.account needs a " + field
            end if
        next
        if type(spec["balance"]) != "money" then
            error "deposits.account expects the opening balance as money"
        end if
        if type(spec["rate"]) != "number" or spec["rate"] < 0 then
            error "deposits.account expects an annual rate as a number, 0 or more"
        end if
        if type(spec["opened"]) != "datetime" then
            error "deposits.account expects an opening date"
        end if
        if not _known_method(spec["balance_method"]) then
            error ("deposits.account: balance_method must be \"daily\","
                   + " \"average_daily\" or \"minimum\" -- they give different"
                   + " interest on the same activity and none is the default")
        end if
        if spec["crediting"] <= 0 or spec["crediting"] != floor(spec["crediting"]) then
            error "deposits.account expects crediting as a whole number of days"
        end if
        return { opened: spec["opened"], balance: spec["balance"],
                 rate: spec["rate"], day_count: spec["day_count"],
                 balance_method: spec["balance_method"],
                 crediting: spec["crediting"] }
    end function

    ' --- the balance a period earns on --------------------------------------
    '
    ' Given the balance at each day of a period, the three methods disagree:
    '   daily          every day earns on its own balance
    '   average_daily  the mean balance earns for the whole period
    '   minimum        the lowest balance earns for the whole period
    '
    ' A withdrawal late in the period therefore costs nothing under `minimum`
    ' if the balance never dipped, and costs the full period under it if it
    ' did -- which is exactly why banks name the method in the account terms.

    ' Segment the period by event, returning { from, to, balance } runs.
    function _runs(acct, events, start, finish)
        out = []
        balance = acct.balance
        at = start
        for i = 0 to count(events) - 1
            e = events[i]
            if e["on"] > finish then
                break
            end if
            ' EVENTS BEFORE THE WINDOW ARE ALREADY IN THE OPENING BALANCE and
            ' must not be applied again. Without this guard a withdrawal in an
            ' earlier crediting period was replayed in every later one, and the
            ' second replay raised "exceeds the balance" against a balance the
            ' first replay had already reduced.
            if e["on"] >= start then
                if e["on"] > at then
                    append(out, { start: at, finish: e["on"], balance: balance })
                    at = e["on"]
                end if
                balance = _after(balance, e)
            end if
        end for
        if finish > at then
            append(out, { start: at, finish: finish, balance: balance })
        end if
        return out
    end function

    function _after(balance, e)
        kind = e["kind"]
        if kind = "deposit" then
            return balance + e["amount"]
        end if
        if kind = "withdrawal" then
            if e["amount"] > balance then
                error ("deposits: a withdrawal of " + string(e["amount"])
                       + " exceeds the balance of " + string(balance))
            end if
            return balance - e["amount"]
        end if
        error "deposits: unknown event kind " + string(kind) + " -- use deposit or withdrawal"
    end function

    ' Interest for one crediting period under the account's method.
    function _period_interest(acct, events, start, finish)
        runs = _runs(acct, events, start, finish)
        if count(runs) = 0 then
            return acct.balance * 0
        end if
        if acct.balance_method = "daily" then
            total = acct.balance * 0
            for each r in runs
                total = total + finance.accrue(r.balance, acct.rate, r.start,
                                               r.finish, acct.day_count)
            next
            return total
        end if
        if acct.balance_method = "minimum" then
            lowest = runs[0].balance
            for each r in runs
                if r.balance < lowest then
                    lowest = r.balance
                end if
            next
            return finance.accrue(lowest, acct.rate, start, finish, acct.day_count)
        end if
        ' average_daily: each run's balance weighted by its own days.
        days = dates.days_between(start, finish)
        if days <= 0 then
            return acct.balance * 0
        end if
        weighted = acct.balance * 0
        for each r in runs
            span = dates.days_between(r.start, r.finish)
            weighted = weighted + (r.balance * span)
        next
        return finance.accrue(weighted / days, acct.rate, start, finish,
                              acct.day_count)
    end function

    ' --- crediting ------------------------------------------------------------
    '
    ' COMPOUNDING AND CREDITING ARE SEPARATE, and conflating them is the common
    ' error. Interest is computed per crediting period and ADDED to the balance
    ' at the end of it, so the next period earns on the larger balance -- that
    ' is the compounding. `crediting` is how many days a period is.

    function apply(acct, events, through)
        if type(events) != "array" then
            error "deposits.apply expects an array of events"
        end if
        if through < acct.opened then
            error "deposits.apply: the date is before the account opened"
        end if
        balance = acct.balance
        credited = acct.balance * 0
        at = acct.opened
        guard = 0
        while at < through and guard < 100000
            finish = at + (acct.crediting * 1 days)
            if finish > through then
                break
            end if
            working = { opened: at, balance: balance, rate: acct.rate,
                        day_count: acct.day_count,
                        balance_method: acct.balance_method,
                        crediting: acct.crediting }
            earned = _period_interest(working, events, at, finish)
            balance = balance + earned
            credited = credited + earned
            ' Roll the balance forward through this period's own events.
            for each e in events
                if e["on"] > at and e["on"] <= finish then
                    balance = _after(balance, e)
                end if
            next
            at = finish
            guard = guard + 1
        end while
        ' Interest earned since the last crediting date, not yet credited.
        working = { opened: at, balance: balance, rate: acct.rate,
                    day_count: acct.day_count,
                    balance_method: acct.balance_method,
                    crediting: acct.crediting }
        pending = _period_interest(working, events, at, through)
        for each e in events
            if e["on"] > at and e["on"] <= through then
                balance = _after(balance, e)
            end if
        next
        return { balance: balance, credited: credited, accrued: pending,
                 available: balance + pending, last_credited: at }
    end function

    ' --- tiered rates ---------------------------------------------------------
    '
    ' `tiers` is an array of { from, rate }, lowest first. `mode` decides which
    ' rate a balance earns, and it is a PRODUCT DECISION rather than a detail:
    '
    '   "whole"    the whole balance earns the tier it lands in
    '   "portion"  each slice earns its own tier, like a tax bracket
    '
    ' They differ by a lot at a boundary, and an account that used the wrong one
    ' would pay a plausible amount of the wrong interest.
    function tiered_rate(tiers, balance, mode)
        if type(tiers) != "array" or count(tiers) = 0 then
            error "deposits.tiered_rate expects a non-empty array of tiers"
        end if
        if mode != "whole" then
            error ("deposits.tiered_rate: only the whole-balance mode returns a"
                   + " single rate; use deposits.tiered_interest for portions")
        end if
        rate = tiers[0].rate
        for each t in tiers
            if balance >= t.from then
                rate = t.rate
            end if
        next
        return rate
    end function

    ' Interest for one year under "portion", where each slice earns its own
    ' tier. Returned as money so the slices stay exact.
    function tiered_interest(tiers, balance, start, finish, convention)
        total = balance * 0
        for i = 0 to count(tiers) - 1
            floor_amt = tiers[i].from
            if balance > floor_amt then
                ceiling_amt = balance
                if i + 1 < count(tiers) then
                    if tiers[i + 1].from < balance then
                        ceiling_amt = tiers[i + 1].from
                    end if
                end if
                slice = ceiling_amt - floor_amt
                total = total + finance.accrue(slice, tiers[i].rate, start,
                                               finish, convention)
            end if
        end for
        return total
    end function

    ' --- certificates ----------------------------------------------------------

    function certificate(spec)
        base = account(spec)
        for each field in ["term_days", "penalty_days"]
            if is_unknown(spec[field]) then
                error "deposits.certificate needs a " + field
            end if
        next
        return { opened: base.opened, balance: base.balance, rate: base.rate,
                 day_count: base.day_count, balance_method: base.balance_method,
                 crediting: base.crediting, term_days: spec["term_days"],
                 penalty_days: spec["penalty_days"] }
    end function

    function matures(cert)
        return cert.opened + (cert.term_days * 1 days)
    end function

    ' Redeem on a date. Before maturity the penalty is that many days of
    ' interest on the principal.
    '
    ' THE PENALTY MAY EXCEED THE INTEREST EARNED, and when it does it reduces
    ' PRINCIPAL. That is how the products are actually written, and clamping it
    ' at zero would report a redemption value the account holder will not
    ' receive -- a plausible number, and the wrong one.
    function redeem(cert, as_of)
        if as_of < cert.opened then
            error "deposits.redeem: the date is before the certificate opened"
        end if
        earned = finance.accrue(cert.balance, cert.rate, cert.opened, as_of,
                                cert.day_count)
        due = matures(cert)
        penalty = cert.balance * 0
        early = as_of < due
        if early then
            penalty = finance.accrue(cert.balance, cert.rate, cert.opened,
                                     cert.opened + (cert.penalty_days * 1 days),
                                     cert.day_count)
        end if
        return { principal: cert.balance, interest: earned, penalty: penalty,
                 early: early,
                 proceeds: cert.balance + earned - penalty,
                 principal_reduced: penalty > earned }
    end function

end library
