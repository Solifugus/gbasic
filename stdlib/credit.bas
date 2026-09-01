' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' credit.bas — credit analytics over a portfolio (docs/credit_analytics_design.md).
'
' `lending` answers questions about ONE loan. This answers questions about a
' BOOK, and they are different questions: is this year's lending worse than
' last year's, where did the 30-day bucket go this month, what did we lose.
' None of them is derivable from a single loan, and none is arithmetic about
' balances -- they are questions about STATES OVER TIME.
'
' THE INPUT IS A STATUS TABLE, NOT A LIST OF LOANS, and that is the design
' decision everything else follows from (§2). One row per loan per observation
' date:
'
'     { id:, opened:, as_of:, status:, balance: }
'
' Two reasons. COST: `lending.apply` is a fold, deliberately, so a 5,000-loan
' book over 36 month-ends is 180,000 folds. PROVENANCE: real portfolio data
' arrives as a monthly performance record per loan -- the shape Fannie Mae's
' and Freddie Mac's published datasets take, and the shape any servicer's
' extract takes. A library that can only read our own `lending` loans cannot be
' pointed at a real book. `credit.observe` is the BRIDGE, so our own machinery
' is a producer and not a special case.
'
' ATTRITION IS A STATE, NOT A HOLE (§4). `paid_off` and `charged_off` are
' buckets in the matrix and nothing rolls out of them; a loan present at t and
' missing at t+1 is reported as `unobserved` rather than dropped, because "we
' stopped seeing it" is a fact about the data and not about the borrower. That
' gives the invariant this library rests on, the counterpart of the accounting
' equation: EVERY LOAN OBSERVED AT t IS ACCOUNTED FOR AT t+1. It is never
' enforced anywhere -- it falls out of correct bucketing, which is what makes
' it a good test.
library credit

    load dates from "dates.bas"
    load lending from "lending.bas"

    ' --- what is declared ----------------------------------------------------

    ' TWO SERVICERS REPORT DIFFERENT DELINQUENCY FOR IDENTICAL LOANS and both
    ' are correct under their own convention, so the method is required and
    ' never inferred -- the rule `lending` already set for accrual basis,
    ' waterfall and day count.
    '
    '   mba -- 30 days delinquent the day after one payment is missed.
    '          The count is PAYMENTS MISSED.
    '   ots -- 30 days delinquent when the oldest unpaid instalment is 30 days
    '          old. The count is DAYS PAST DUE.
    '
    ' On a monthly loan the two differ by about a month for the whole life of a
    ' delinquency, so a book reported one way is not comparable with a book
    ' reported the other.
    function methods()
        return ["mba", "ots"]
    end function

    function buckets()
        return ["current", "dpd_30", "dpd_60", "dpd_90", "dpd_120_plus",
                "paid_off", "charged_off"]
    end function

    ' Absorbing states: nothing rolls out of them. A charged-off loan that
    ' cures is a data defect, not a recovery, and is refused by name.
    function absorbing()
        return ["paid_off", "charged_off"]
    end function

    function _is_absorbing(b)
        return contains(absorbing(), b)
    end function

    ' The delinquency bucket for a set of unpaid instalment due dates. BOTH
    ' CONVENTIONS READ THE SAME INPUT, which is what makes them comparable: one
    ' counts the dues that have come and gone, the other measures the age of
    ' the oldest.
    function bucket(unpaid, as_of, method)
        if not contains(methods(), method) then
            error ("credit.bucket: method must be \"mba\" or \"ots\" -- they are a"
                   + " month apart and neither is the default (got " + string(method) + ")")
        end if
        if type(unpaid) != "array" then
            error "credit.bucket expects an array of unpaid due dates"
        end if
        due = 0
        oldest = unknown
        for each d in unpaid
            if d <= as_of then
                due = due + 1
                if is_unknown(oldest) or d < oldest then
                    oldest = d
                end if
            end if
        next
        if due = 0 then
            return "current"
        end if
        if method = "mba" then
            return _ladder(due * 30)
        end if
        return _ladder(dates.between(oldest, as_of, "days"))
    end function

    function _ladder(days)
        if days >= 120 then
            return "dpd_120_plus"
        else if days >= 90 then
            return "dpd_90"
        else if days >= 60 then
            return "dpd_60"
        else if days >= 30 then
            return "dpd_30"
        end if
        return "current"
    end function

    ' --- the status table ----------------------------------------------------

    ' Validation is shared rather than repeated per entry point, so a table
    ' that one function accepts cannot be one another silently mis-reads.
    function check(table)
        if type(table) != "array" then
            error "credit: the status table must be an array of rows"
        end if
        seen = { }
        for i = 0 to count(table) - 1
            row = table[i]
            if type(row) != "record" then
                error "credit: row " + string(i + 1) + " is not a record"
            end if
            for each field in ["id", "opened", "as_of", "status"]
                if is_unknown(row[field]) then
                    error "credit: row " + string(i + 1) + " has no " + field
                end if
            next
            if not contains(buckets(), row["status"]) then
                error ("credit: row " + string(i + 1) + " has status "
                       + string(row["status"]) + ", which is not a bucket ("
                       + join(buckets(), ", ") + ")")
            end if
            k = string(row["id"]) + "@" + string(row["as_of"])
            if not is_unknown(seen[k]) then
                error ("credit: loan " + string(row["id"]) + " appears twice on "
                       + string(row["as_of"]) + " -- the migration count would be"
                       + " right and the population double")
            end if
            seen[k] = true
        end for
        return count(table)
    end function

    ' --- migration -----------------------------------------------------------

    ' Where the whole book went between two dates. Returns counts, never rates;
    ' `roll_rates` divides. The two are separate because the reconciliation
    ' invariant is about COUNTS, and a rate cannot state it.
    function migration(table, from_date, to_date)
        checked = check(table)
        if from_date >= to_date then
            error "credit.migration: the second date must be after the first"
        end if
        at_from = _snapshot(table, from_date)
        at_to = _snapshot(table, to_date)
        if count(keys(at_from)) = 0 then
            error ("credit.migration: no rows dated " + string(from_date)
                   + " -- comparing against an empty date reports 100% attrition")
        end if
        if count(keys(at_to)) = 0 then
            error ("credit.migration: no rows dated " + string(to_date)
                   + " -- comparing against an empty date reports 100% attrition")
        end if
        counts = { }
        starting = { }
        unobserved = { }
        for each b in buckets()
            starting[b] = 0
            unobserved[b] = 0
            inner = { }
            for each c in buckets()
                inner[c] = 0
            next
            counts[b] = inner
        next
        moved = 0
        for each id in keys(at_from)
            was = at_from[id]
            starting[was] = starting[was] + 1
            now = at_to[id]
            if is_unknown(now) then
                unobserved[was] = unobserved[was] + 1
            else
                if _is_absorbing(was) and now != was then
                    error ("credit.migration: loan " + string(id) + " left the "
                           + was + " bucket for " + string(now)
                           + " -- absorbing states do not roll")
                end if
                row = counts[was]
                row[now] = row[now] + 1
                counts[was] = row
                moved = moved + 1
            end if
        next
        ' Loans present at the later date and not the earlier one. They are
        ' NOT part of the migration -- it is a statement about where the
        ' starting population went -- but a book whose 30-day bucket held
        ' steady because it was refilled by new lending is a different fact
        ' from one where nothing moved, and only this number tells them apart.
        entered = 0
        for each id in keys(at_to)
            if is_unknown(at_from[id]) then
                entered = entered + 1
            end if
        next
        return { from: from_date, to: to_date, buckets: buckets(),
                 counts: counts, starting: starting, unobserved: unobserved,
                 observed: moved, entered: entered, total: count(keys(at_from)) }
    end function

    ' The share of each starting bucket that ended in each other, plus the
    ' share that left the data. THE DENOMINATOR IS THE WHOLE STARTING BUCKET,
    ' including the loans that went unobserved: dropping them makes a book look
    ' like it is curing, which is the commonest defect in roll-rate work and
    ' produces an ordinary-looking percentage rather than an error.
    function roll_rates(table, from_date, to_date)
        m = migration(table, from_date, to_date)
        rates = { }
        lost = { }
        for each b in buckets()
            n = m.starting[b]
            inner = { }
            for each c in buckets()
                if n = 0 then
                    inner[c] = unknown
                else
                    inner[c] = m.counts[b][c] / n
                end if
            next
            rates[b] = inner
            if n = 0 then
                lost[b] = unknown
            else
                lost[b] = m.unobserved[b] / n
            end if
        next
        return { from: from_date, to: to_date, buckets: buckets(),
                 rates: rates, unobserved: lost, starting: m.starting }
    end function

    ' A bucket with no loans in it reports `unknown`, NEVER zero: a zero roll
    ' rate out of an empty bucket reads as "nothing went bad" when the truth is
    ' "there was nothing there".

    function _snapshot(table, on_date)
        out = { }
        for each row in table
            if row["as_of"] = on_date then
                out[string(row["id"])] = row["status"]
            end if
        next
        return out
    end function

    ' --- vintage -------------------------------------------------------------

    ' A cohort's cumulative bad rate against AGE, so loans made two years apart
    ' can be compared at the same point in their lives. Indexed by calendar
    ' month instead, every cohort's curve starts at a different age, the
    ' averages mix cohorts, and the result is a smooth, meaningless line that
    ' does not look like a mistake.
    '
    ' CUMULATIVE MEANS EVER-REACHED: once a loan touches a bad status it counts
    ' at that age and every later one, even if it cures. That is the convention
    ' a loss curve is drawn under, and it is declared here rather than assumed
    ' because "currently in" is a different and equally reasonable curve.
    '
    ' spec: { bad: [...], basis: "original"|"outstanding", cohort_by: "month"|"quarter"|"year" }
    function vintage(table, spec)
        checked = check(table)
        if type(spec) != "record" then
            error "credit.vintage expects a spec record"
        end if
        bad = spec["bad"]
        if is_unknown(bad) then
            bad = ["dpd_90", "dpd_120_plus", "charged_off"]
        end if
        for each b in bad
            if not contains(buckets(), b) then
                error "credit.vintage: " + string(b) + " is not a bucket"
            end if
        next
        basis = spec["basis"]
        if is_unknown(basis) then
            error ("credit.vintage needs a basis: \"original\" divides by the"
                   + " cohort as it was written, \"outstanding\" by what is still"
                   + " on the book, and they are different curves")
        end if
        if basis != "original" and basis != "outstanding" then
            error "credit.vintage: basis must be \"original\" or \"outstanding\""
        end if
        grain = spec["cohort_by"]
        if is_unknown(grain) then
            grain = "month"
        end if

        ' First pass: the age at which each loan first went bad, its cohort,
        ' the ages it was observed at, and the age it left the book.
        first_bad = { }
        cohort_of = { }
        top_age = { }
        alive = { }
        for each row in table
            id = string(row["id"])
            age = dates.between(row["opened"], row["as_of"], "months")
            if age < 0 then
                error ("credit.vintage: loan " + id + " is observed on "
                       + string(row["as_of"]) + ", before it was opened")
            end if
            c = _cohort_label(row["opened"], grain)
            cohort_of[id] = c
            ' A COHORT'S CURVE STOPS AT THE AGE IT HAS REACHED. Carrying every
            ' cohort out to the oldest one's age reports a 0% bad rate at ages
            ' the young cohort has not lived through -- "no losses" where the
            ' truth is "no data", which is the same plausible wrong answer this
            ' library exists to refuse. Real vintage tables are triangles for
            ' exactly this reason.
            if is_unknown(top_age[c]) or age > top_age[c] then
                top_age[c] = age
            end if
            if contains(bad, row["status"]) then
                prior = first_bad[id]
                if is_unknown(prior) or age < prior then
                    first_bad[id] = age
                end if
            end if
            if not _is_absorbing(row["status"]) then
                was = alive[id]
                if is_unknown(was) or age > was then
                    alive[id] = age
                end if
            end if
        next

        ' Second pass: cohort sizes, then the curve.
        size = { }
        members = { }
        for each id in keys(cohort_of)
            c = cohort_of[id]
            if is_unknown(size[c]) then
                size[c] = 0
                members[c] = []
            end if
            size[c] = size[c] + 1
            lst = members[c]
            append(lst, id)
            members[c] = lst
        next

        labels = sort(keys(size))
        series = { }
        for each c in labels
            points = []
            for age = 0 to top_age[c]
                bad_n = 0
                on_book = 0
                for each id in members[c]
                    fb = first_bad[id]
                    is_bad = not is_unknown(fb) and fb <= age
                    if is_bad then
                        bad_n = bad_n + 1
                    end if
                    ' A loan counts as outstanding at this age if it was still
                    ' being serviced then OR has already gone bad. The `or` is
                    ' load-bearing: a delinquent loan is BOTH still on the book
                    ' and in the numerator, so adding the two counts instead
                    ' would put it in the denominator twice and understate the
                    ' rate -- by exactly the amount the curve is about.
                    a = alive[id]
                    if is_bad or (not is_unknown(a) and a >= age) then
                        on_book = on_book + 1
                    end if
                next
                denominator = size[c]
                if basis = "outstanding" then
                    denominator = on_book
                end if
                r = unknown
                if denominator > 0 then
                    r = bad_n / denominator
                end if
                append(points, { age: age, cohort_size: size[c],
                                 at_risk: denominator, bad: bad_n, rate: r })
            end for
            series[c] = points
        next
        return { cohorts: labels, basis: basis, bad: bad, series: series }
    end function

    function _cohort_label(d, grain)
        if grain = "year" then
            return string(d.year)
        end if
        if grain = "quarter" then
            return string(d.year) + "-Q" + string(floor((d.month - 1) / 3) + 1)
        end if
        if grain = "month" then
            return string(d.year) + "-" + _pad2(d.month)
        end if
        error "credit.vintage: cohort_by must be month, quarter or year"
    end function

    function _pad2(n)
        if n < 10 then
            return "0" + string(n)
        end if
        return string(n)
    end function

    ' --- charge-off and recovery --------------------------------------------

    ' A CHARGE-OFF IS A DECISION, not a threshold crossing: the servicer wrote
    ' the balance off and the date is a fact in the record, so this READS
    ' `charged_off` rather than inferring it from days past due. Inferring it
    ' would produce a loss figure the servicer's own books disagree with.
    '
    ' GROSS AND NET ARE REPORTED SEPARATELY and never netted silently, because
    ' they are different numbers used for different purposes and quietly
    ' reporting one as the other halves a loss rate.
    function losses(table, spec)
        checked = check(table)
        grain = "month"
        if type(spec) = "record" and not is_unknown(spec["cohort_by"]) then
            grain = spec["cohort_by"]
        end if
        wrote_off = { }
        cohort_of = { }
        gross = { }
        recovered = { }
        for each row in table
            id = string(row["id"])
            cohort_of[id] = _cohort_label(row["opened"], grain)
            if row["status"] = "charged_off" and is_unknown(wrote_off[id]) then
                b = row["balance"]
                if is_unknown(b) or type(b) != "money" then
                    error ("credit.losses: loan " + id + " is charged off with no"
                           + " balance -- the amount written off is the number")
                end if
                wrote_off[id] = row["as_of"]
                gross[id] = b
            end if
        next
        for each row in table
            id = string(row["id"])
            r = row["recovery"]
            if not is_unknown(r) then
                if type(r) != "money" then
                    error "credit.losses: loan " + id + " has a recovery that is not money"
                end if
                off = wrote_off[id]
                if is_unknown(off) or row["as_of"] <= off then
                    error ("credit.losses: loan " + id + " reports a recovery on "
                           + string(row["as_of"]) + " that is not after a charge-off"
                           + " -- money before the write-off is a payment")
                end if
                prior = recovered[id]
                if is_unknown(prior) then
                    prior = r * 0
                end if
                recovered[id] = prior + r
            end if
        next
        by_cohort = { }
        total_n = 0
        for each id in keys(wrote_off)
            c = cohort_of[id]
            slot = by_cohort[c]
            if is_unknown(slot) then
                slot = { cohort: c, count: 0, gross: gross[id] * 0,
                         recoveries: gross[id] * 0, net: gross[id] * 0 }
            end if
            slot.count = slot.count + 1
            slot.gross = slot.gross + gross[id]
            rec = recovered[id]
            if not is_unknown(rec) then
                slot.recoveries = slot.recoveries + rec
            end if
            slot.net = slot.gross - slot.recoveries
            by_cohort[c] = slot
            total_n = total_n + 1
        next
        return { cohorts: sort(keys(by_cohort)), by_cohort: by_cohort,
                 count: total_n }
    end function

    ' --- the bridge ----------------------------------------------------------

    ' Drive `lending` over a list of loans and produce the status table, so our
    ' own machinery is a PRODUCER of the table rather than a special case the
    ' analytics know about. `entries` is
    '
    '     { id:, loan: <lending.loan>, events: [...] }
    '
    ' A due instalment is unpaid when the cumulative payments received by the
    ' observation date fall short of the cumulative schedule through that due
    ' date -- the contractual due-date method, which is what makes `bucket`'s
    ' two conventions answerable at all.
    function observe(book, dates_list, method)
        if type(book) != "array" or count(book) = 0 then
            error "credit.observe expects a non-empty array of loans"
        end if
        if not contains(methods(), method) then
            error "credit.observe: method must be \"mba\" or \"ots\""
        end if
        table = []
        for each item in book
            id = item["id"]
            l = item["loan"]
            if is_unknown(id) or is_unknown(l) then
                error "credit.observe: each entry needs an id and a loan"
            end if
            events = item["events"]
            if is_unknown(events) then
                events = []
            end if
            months_per = 12 / l.periods_per_year
            if months_per != floor(months_per) then
                error ("credit.observe: " + string(l.periods_per_year) + " periods a"
                       + " year does not fall on month boundaries -- due dates"
                       + " cannot be dated")
            end if
            sched = lending.schedule(l)
            zero = l.principal * 0
            for each on_date in dates_list
                ' A LOAN THAT DOES NOT EXIST YET HAS NO STATUS. A portfolio
                ' observed on a fixed calendar naturally has loans entering
                ' part way through, and emitting a row for a date before
                ' origination would put an unwritten loan in the `current`
                ' bucket and inflate every denominator behind it.
                if on_date < l.opened then
                    continue
                end if
                so_far = []
                for each e in events
                    if e["on"] <= on_date then
                        append(so_far, e)
                    end if
                next
                st = lending.apply(l, so_far, false)
                paid = st.paid_principal + st.paid_interest + st.paid_fees
                unpaid = []
                cumulative = zero
                for k = 0 to count(sched) - 1
                    cumulative = cumulative + sched[k].payment
                    due_on = l.opened + (1 month) * ((k + 1) * months_per)
                    if due_on <= on_date and cumulative > paid then
                        append(unpaid, due_on)
                    end if
                end for
                status = bucket(unpaid, on_date, method)
                if st.balance <= zero then
                    status = "paid_off"
                end if
                append(table, { id: id, opened: l.opened, as_of: on_date,
                                status: status, balance: st.balance })
            next
        next
        return table
    end function

end library
