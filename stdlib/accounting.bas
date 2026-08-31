' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' accounting.bas — double-entry accounting (docs/accounting_design.md).
'
' ONE INVARIANT: every entry balances, in every currency. Everything else here
' follows from it. The balance-sheet identity is not a second rule to enforce —
' it falls out of this one plus correct account typing, which is why it makes
' such a good test.
'
' TWO gBASIC PROPERTIES DO MOST OF THE WORK. `money` refuses to add different
' currencies, so the commonest class of accounting bug raises rather than
' producing a nonsense total; and money is an exact integer, so a ledger that
' balances stays balanced with no accumulated float to sweep into a rounding
' account.
'
' NOTE `post` RETURNS the new ledger. Inside a function, `append` mutates a
' local copy, so a mutate-in-place API would silently do nothing to the
' caller's value — write `ledger = accounting.post(ledger, e)`.
library accounting

    ' --- chart of accounts --------------------------------------------------

    function _known_kind(kind)
        return (kind = "asset" or kind = "liability" or kind = "equity"
                or kind = "revenue" or kind = "expense")
    end function

    ' Assets and expenses increase on the DEBIT side; the other three increase
    ' on the credit side. This one fact decides statement placement and the
    ' sign a reader expects, so it is written once and read everywhere.
    function normal_side(kind)
        if kind = "asset" or kind = "expense" then
            return "debit"
        end if
        if _known_kind(kind) then
            return "credit"
        end if
        error "accounting: unknown account kind '" + string(kind) + "'"
    end function

    ' Validate a list of accounts and return the chart: a record from code to
    ' account, so a lookup is one hash probe rather than a scan.
    function chart(accounts)
        if type(accounts) != "array" or count(accounts) = 0 then
            error "accounting.chart expects a non-empty array of accounts"
        end if
        out = { }
        for i = 0 to count(accounts) - 1
            a = accounts[i]
            if type(a) != "record" then
                error "accounting.chart expects every account to be a record"
            end if
            if is_unknown(a["code"]) or type(a["code"]) != "string" or a["code"] = "" then
                error "accounting.chart: every account needs a non-empty code"
            end if
            if is_unknown(a["name"]) or type(a["name"]) != "string" or a["name"] = "" then
                error "accounting.chart: account " + a["code"] + " needs a name"
            end if
            if is_unknown(a["kind"]) or not _known_kind(a["kind"]) then
                error ("accounting.chart: account " + a["code"]
                       + " needs a kind of asset, liability, equity, revenue or expense")
            end if
            if not is_unknown(out[a["code"]]) then
                error "accounting.chart: duplicate account code " + a["code"]
            end if
            out[a["code"]] = { code: a["code"], name: a["name"], kind: a["kind"],
                               side: normal_side(a["kind"]) }
        end for
        return out
    end function

    ' --- entries ------------------------------------------------------------

    ' Normalise one line to { account, amount, currency } with DEBIT POSITIVE.
    ' The public form names the side because a signed amount makes "is a
    ' negative debit a credit" a question every reader has to re-answer, and
    ' getting it wrong yields a statement that balances and is backwards.
    function _line(chart_v, ln, index)
        where = "line " + string(index + 1)
        if type(ln) != "record" then
            error "accounting.entry: " + where + " must be a record"
        end if
        if is_unknown(ln["account"]) then
            error "accounting.entry: " + where + " has no account"
        end if
        acct = chart_v[ln["account"]]
        if is_unknown(acct) then
            error ("accounting.entry: " + where + " names account "
                   + string(ln["account"]) + ", which is not in the chart")
        end if
        has_d = not is_unknown(ln["debit"])
        has_c = not is_unknown(ln["credit"])
        if has_d and has_c then
            error "accounting.entry: " + where + " has both a debit and a credit"
        end if
        if not has_d and not has_c then
            error "accounting.entry: " + where + " has neither a debit nor a credit"
        end if
        amount = ln["debit"]
        if has_c then
            amount = ln["credit"]
        end if
        if type(amount) != "money" then
            error "accounting.entry: " + where + " needs a money amount"
        end if
        signed = amount
        if has_c then
            signed = amount * -1
        end if
        return { account: ln["account"], amount: signed,
                 currency: money.currency(amount), side: _side_of(has_d) }
    end function

    function _side_of(has_debit)
        if has_debit then
            return "debit"
        end if
        return "credit"
    end function

    ' Build and VALIDATE one entry. Separate from `post` so a caller importing
    ' rows can inspect a failure and decide whether to skip the row or stop.
    function entry(chart_v, when, memo, lines)
        if type(when) != "datetime" then
            error "accounting.entry expects a date"
        end if
        if type(lines) != "array" or count(lines) < 2 then
            error "accounting.entry expects at least two lines"
        end if
        normalised = []
        for i = 0 to count(lines) - 1
            append(normalised, _line(chart_v, lines[i], i))
        end for

        ' BALANCE PER CURRENCY. An entry mixing currencies must balance in each
        ' one separately; `money` refuses to add across currencies anyway, so
        ' summing per currency is both the correct rule and the only thing the
        ' type permits.
        totals = { }
        for i = 0 to count(normalised) - 1
            ln = normalised[i]
            running = totals[ln.currency]
            if is_unknown(running) then
                running = ln.amount * 0
            end if
            totals[ln.currency] = running + ln.amount
        end for
        for each code in keys(totals)
            zero = totals[code] * 0
            if totals[code] != zero then
                error ("accounting.entry does not balance in " + code
                       + ": debits minus credits is " + string(totals[code]))
            end if
        next

        return { date: when, memo: memo, lines: normalised }
    end function

    ' --- the ledger ---------------------------------------------------------

    function ledger()
        return { entries: [], closed_through: nothing }
    end function

    ' Append an entry. RETURNS the new ledger -- see the note at the top.
    function post(lg, e)
        if type(lg) != "record" or is_unknown(lg["entries"]) then
            error "accounting.post expects a ledger from accounting.ledger()"
        end if
        if type(e) != "record" or is_unknown(e["lines"]) then
            error "accounting.post expects an entry from accounting.entry()"
        end if
        if not is_nothing(lg.closed_through) and e.date <= lg.closed_through then
            error ("accounting.post: the period through " + string(lg.closed_through)
                   + " is closed; an entry dated " + string(e.date) + " cannot be added")
        end if
        return { entries: append(lg.entries, e), closed_through: lg.closed_through }
    end function

    ' --- reporting ----------------------------------------------------------

    ' A money zero in the ledger's own currency. Statements need one to start
    ' a total from, and money has no literal, so it is taken from the data.
    function _zero(lg)
        if count(lg.entries) = 0 then
            error "accounting: an empty ledger has nothing to report"
        end if
        return lg.entries[0].lines[0].amount * 0
    end function

    ' Balance per account, DEBIT POSITIVE -- the raw ledger truth, before any
    ' presentation sign is applied. `upto` limits to entries on or before a
    ' date; pass `nothing` for the whole ledger.
    function balances(lg, upto)
        out = { }
        for each e in lg.entries
            if is_nothing(upto) or e.date <= upto then
                for each ln in e.lines
                    running = out[ln.account]
                    if is_unknown(running) then
                        running = ln.amount * 0
                    end if
                    out[ln.account] = running + ln.amount
                next
            end if
        next
        return out
    end function

    ' Every account with its total on the side it fell. Debits and credits must
    ' come to the same figure -- which they always will if every entry
    ' balanced, so this is a check on the IMPLEMENTATION, not on the data.
    function trial_balance(lg)
        zero = _zero(lg)
        bal = balances(lg, nothing)
        rows = []
        total_d = zero
        total_c = zero
        for each code in sort(keys(bal))
            amount = bal[code]
            d = zero
            c = zero
            if amount > zero then
                d = amount
                total_d = total_d + amount
            end if
            if amount < zero then
                c = amount * -1
                total_c = total_c + (amount * -1)
            end if
            append(rows, { account: code, debit: d, credit: c })
        next
        return { rows: rows, debits: total_d, credits: total_c,
                 balanced: total_d = total_c }
    end function

    ' Sum the balances of every account of one kind, signed so a NORMAL balance
    ' reads positive: assets and expenses as they are, the credit-normal kinds
    ' negated.
    function _kind_total(chart_v, bal, kind, zero)
        total = zero
        for each code in keys(bal)
            acct = chart_v[code]
            if not is_unknown(acct) and acct.kind = kind then
                if acct.side = "debit" then
                    total = total + bal[code]
                else
                    total = total - bal[code]
                end if
            end if
        next
        return total
    end function

    ' Assets, liabilities and equity as at a date.
    '
    ' `equity` here is the equity ACCOUNTS only; `earnings` is revenue minus
    ' expenses not yet closed into them. The identity that must hold is
    ' assets = liabilities + equity + earnings, and `balanced` states it.
    function balance_sheet(chart_v, lg, as_of)
        zero = _zero(lg)
        bal = balances(lg, as_of)
        assets = _kind_total(chart_v, bal, "asset", zero)
        liabilities = _kind_total(chart_v, bal, "liability", zero)
        equity = _kind_total(chart_v, bal, "equity", zero)
        revenue = _kind_total(chart_v, bal, "revenue", zero)
        expenses = _kind_total(chart_v, bal, "expense", zero)
        earnings = revenue - expenses
        return { assets: assets, liabilities: liabilities, equity: equity,
                 earnings: earnings,
                 balanced: assets = (liabilities + equity + earnings) }
    end function

    ' Revenue and expenses over a window, and the net between them.
    function income_statement(chart_v, lg, start, finish)
        zero = _zero(lg)
        window = { entries: [], closed_through: nothing }
        kept = []
        for each e in lg.entries
            if e.date >= start and e.date <= finish then
                append(kept, e)
            end if
        next
        window = { entries: kept, closed_through: nothing }
        if count(kept) = 0 then
            return { revenue: zero, expenses: zero, net: zero }
        end if
        bal = balances(window, nothing)
        revenue = _kind_total(chart_v, bal, "revenue", zero)
        expenses = _kind_total(chart_v, bal, "expense", zero)
        return { revenue: revenue, expenses: expenses, net: revenue - expenses }
    end function

    ' --- closing ------------------------------------------------------------

    ' Move revenue and expenses into an equity account and seal the period.
    '
    ' REFUSED ON AN ALREADY-CLOSED PERIOD rather than made idempotent. Running
    ' a close twice doubles the transfer and leaves the ledger perfectly
    ' balanced while doing it, which is the shape of mistake this whole library
    ' is arranged to make impossible rather than merely unlikely.
    function close(chart_v, lg, through, equity_account)
        zero = _zero(lg)
        if is_unknown(chart_v[equity_account]) then
            error "accounting.close: " + string(equity_account) + " is not in the chart"
        end if
        if chart_v[equity_account].kind != "equity" then
            error ("accounting.close: " + string(equity_account)
                   + " is of kind " + chart_v[equity_account].kind + ", not equity")
        end if
        if not is_nothing(lg.closed_through) and through <= lg.closed_through then
            error ("accounting.close: the period through "
                   + string(lg.closed_through) + " is already closed")
        end if
        bal = balances(lg, through)
        lines = []
        net = zero
        for each code in sort(keys(bal))
            acct = chart_v[code]
            if not is_unknown(acct) and (acct.kind = "revenue" or acct.kind = "expense") then
                amount = bal[code]
                if amount != zero then
                    ' Post the OPPOSITE of the balance, which zeroes it.
                    if amount > zero then
                        append(lines, { account: code, credit: amount })
                    else
                        append(lines, { account: code, debit: amount * -1 })
                    end if
                    net = net - amount
                end if
            end if
        next
        if count(lines) = 0 then
            error "accounting.close: nothing to close in that period"
        end if
        ' The balancing line into equity.
        if net > zero then
            append(lines, { account: equity_account, credit: net })
        else
            append(lines, { account: equity_account, debit: net * -1 })
        end if
        closing = entry(chart_v, through, "Closing entry", lines)
        return { entries: append(lg.entries, closing), closed_through: through }
    end function

end library
