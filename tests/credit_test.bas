' credit analytics over a portfolio (docs/credit_analytics_design.md).
'
' SELF-CHECKING, not golden, and here it is forced: EVERY defect this library
' exists to prevent produces an ordinary-looking percentage. A roll rate that
' drops attrition, a vintage curve on the wrong index, a matrix that
' double-counts -- each reads as a book doing slightly better or worse than
' expected, and a golden would record the damaged figure as expected and then
' defend it.
'
' THE LOAD-BEARING TIER IS RECONCILIATION, the counterpart of the accounting
' equation: every loan observed at t is accounted for at t+1. It is never
' enforced anywhere in the library -- it falls out of correct bucketing -- so
' asserting it states what must be TRUE rather than what the code said.
'
' THE SECOND IS THE PLANTED PORTFOLIO, which is what
' docs/lending_design.md §8 asked for before credit analytics could be built:
' a book with known-bad accounts rather than six hand-written loans. The bad
' accounts are chosen by `fake.sample` and carry NO marker, so the analytics
' have to find them by computing.

load credit
load lending
load fake

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' --- the book ------------------------------------------------------------
'
' Three quarterly cohorts of 30 loans each, every loan on the same 36-month
' term so age reasoning is about the calendar and not about the product.

function terms_for(spec)
    return lending.loan({ principal: spec.principal, rate: spec.rate,
                          term: spec.term, opened: spec.opened,
                          basis: "amortized",
                          waterfall: "fees_interest_principal",
                          day_count: "30/360" })
end function

' Payments on the due date, every month, for `how_many` months.
function pay_stream(l, how_many)
    due = lending.payment(l)
    out = []
    for k = 1 to how_many
        append(out, { on: l.opened + (1 month) * k, kind: "payment", amount: due })
    next
    return out
end function

function rate_at(v, cohort_label, age)
    for each p in v.series[cohort_label]
        if p.age = age then
            return p.rate
        end if
    next
    return unknown
end function

function count_at_risk(v, cohort_label, age)
    for each p in v.series[cohort_label]
        if p.age = age then
            return p.at_risk
        end if
    next
    return unknown
end function

function cohort(base, n, from_d, to_d)
    return fake.portfolio(base, n, { from: from_d, to: to_d, currency: "USD",
                                     median: 20000, sigma: 0.4, terms: [36] })
end function

q1a {date}= "2026-01-05"
q1b {date}= "2026-03-25"
q2a {date}= "2026-04-05"
q2b {date}= "2026-06-25"
q3a {date}= "2026-07-05"
q3b {date}= "2026-09-25"

specs = []
for each s in cohort(101, 20, q1a, q1b)
    append(specs, s)
next
for each s in cohort(202, 20, q2a, q2b)
    append(specs, { id: "M" + s.id, opened: s.opened, principal: s.principal,
                    rate: s.rate, term: s.term })
next
for each s in cohort(303, 20, q3a, q3b)
    append(specs, { id: "Z" + s.id, opened: s.opened, principal: s.principal,
                    rate: s.rate, term: s.term })
next
check("the book is 60 loans", count(specs), 60)

' THE PLANTED BAD ACCOUNTS. Six of the thirty in the middle cohort, chosen by
' seed alone and named here and nowhere in the data. They pay twice and stop.
bad_at = fake.sample(4242, 20, 4)
bad_ids = { }
for each i in bad_at
    bad_ids[specs[20 + i].id] = true
next
check("four accounts are planted bad", count(keys(bad_ids)), 4)
check("and all six are in the middle cohort", left(specs[20 + bad_at[0]].id, 1), "M")

' AND SOME THAT RUN OFF. Five of the first cohort pay for a year and then
' clear the balance -- an ordinary prepayment, and the only way `paid_off`
' appears in the data at all. Without them the attrition tier below would be
' vacuous: a matrix that silently drops exits looks identical to a correct one
' when nothing ever exits.
' They come from the PLANTED cohort, and disjointly from the bad accounts:
' run-off and default in the same cohort is what makes the two vintage bases
' differ at all. A cohort with defaults and no prepayments has an identical
' curve either way, and the difference tier below would pass on a library that
' ignored `basis` entirely.
eligible = []
for i = 0 to 19
    if is_unknown(bad_ids[specs[20 + i].id]) then
        append(eligible, i)
    end if
end for
prepay_ids = { }
for each j in fake.sample(77, count(eligible), 4)
    prepay_ids[specs[20 + eligible[j]].id] = true
next

book = []
for each s in specs
    l = terms_for(s)
    n_paid = 24
    if not is_unknown(bad_ids[s.id]) then
        n_paid = 2
    end if
    evs = pay_stream(l, n_paid)
    if not is_unknown(prepay_ids[s.id]) then
        evs = pay_stream(l, 12)
        clear_on = l.opened + (1 month) * 13
        quote = lending.payoff(l, evs, clear_on)
        append(evs, { on: clear_on, kind: "payment", amount: quote.total })
    end if
    append(book, { id: s.id, loan: l, events: evs })
next
check("four accounts prepay", count(keys(prepay_ids)), 4)

' Monthly observations from the first quarter through the third year.
observed_on = []
anchor {date}= "2026-01-01"
for k = 0 to 23
    append(observed_on, anchor + (1 month) * k)
next

table = credit.observe(book, observed_on, "mba")
check("the table validates", credit.check(table) > 0, true)

' A loan contributes a row only from the month it was written. If it did not,
' unwritten loans would sit in `current` and inflate every denominator.
early = 0
for each r in table
    if r.as_of < r.opened then
        early = early + 1
    end if
next
check("no loan is observed before it was written", early, 0)

' --- TIER: the bridge ----------------------------------------------------
' `observe` must agree with `lending.apply` at the same date, or the producer
' and the single-loan library have drifted and every figure below inherits it.
probe = book[0]
agree = 0
tested = 0
for each r in table
    if r.id = probe.id then
        so_far = []
        for each e in probe.events
            if e["on"] <= r.as_of then
                append(so_far, e)
            end if
        next
        st = lending.apply(probe.loan, so_far, false)
        tested = tested + 1
        if st.balance = r.balance then
            agree = agree + 1
        end if
    end if
next
check("every observed balance equals lending.apply at that date", agree, tested)
check("and the bridge was actually exercised", tested > 20, true)

' --- TIER: reconciliation ------------------------------------------------
' §4's invariant, over every consecutive pair of observation dates. This is
' the arithmetic tier: it catches the dropped loan, the double count and the
' silently discarded exit together, and none of the three raises anything.
recon_ok = 0
recon_run = 0
entrants = 0
for k = 6 to 22
    m = credit.migration(table, observed_on[k], observed_on[k + 1])
    recon_run = recon_run + 1
    ok = true
    for each b in m.buckets
        rowsum = 0
        for each c in m.buckets
            rowsum = rowsum + m.counts[b][c]
        next
        if rowsum + m.unobserved[b] != m.starting[b] then
            ok = false
        end if
    next
    total = 0
    for each b in m.buckets
        total = total + m.starting[b]
    next
    if total != m.total then
        ok = false
    end if
    if ok then
        recon_ok = recon_ok + 1
    end if
    entrants = entrants + m.entered
end for
check("every migration reconciles, bucket by bucket", recon_ok, recon_run)
check("and there were enough of them to mean something", recon_run, 17)
check("new lending is reported as entrants, not as movement", entrants > 0, true)

' --- TIER: attrition is a state, not a hole ------------------------------
' The whole point of §4. Loans that pay off must appear in the matrix rather
' than vanishing from it, or a book that is running off looks like a book
' that is curing.
' From the second observation: nothing is written on the 1st of January, so
' the first date holds no rows at all and a migration out of it is refused --
' correctly, and by the tier below.
paid_off_seen = 0
for k = 1 to count(observed_on) - 2
    m = credit.migration(table, observed_on[k], observed_on[k + 1])
    paid_off_seen = paid_off_seen + m.counts["current"]["paid_off"]
end for
check("loans that paid off are in the matrix, not dropped", paid_off_seen > 0, true)

' A bucket nobody is in reports `unknown`, never zero: a zero roll rate out of
' an empty bucket reads as "nothing went bad" when the truth is "there was
' nothing there".
' --- TIER: a loan that leaves the data ----------------------------------
' The dropped-loan bug is INVISIBLE in the book above, because `observe`
' emits a row for every date once a loan is written and nothing ever
' disappears. Real extracts are not like that: a loan sold, transferred or
' simply missing from the file stops being observed, and if it is dropped
' from the migration rather than reported, the book looks like it is curing.
' So this tier builds a table where four accounts go silent -- without it the
' reconciliation tier above is satisfied by a library that never counts
' attrition at all, since there is none to count.
silent_ids = { }
for each i in fake.sample(555, 20, 4)
    silent_ids[specs[i].id] = true
next
cut {date}= "2027-01-01"
patchy = []
for each r in table
    if is_unknown(silent_ids[r.id]) or r.as_of < cut then
        append(patchy, r)
    end if
next
check("four accounts go silent", count(keys(silent_ids)), 4)
check("and the table is shorter for it", count(patchy) < count(table), true)

pm = credit.migration(patchy, cut - (1 month), cut)
lost = 0
for each b in pm.buckets
    lost = lost + pm.unobserved[b]
next
check("a loan that stops being observed is reported, not dropped", lost, 4)

pm_ok = true
for each b in pm.buckets
    rowsum = 0
    for each c in pm.buckets
        rowsum = rowsum + pm.counts[b][c]
    next
    if rowsum + pm.unobserved[b] != pm.starting[b] then
        pm_ok = false
    end if
next
check("and the matrix still reconciles with them in it", pm_ok, true)

' THE SAME INVARIANT IN RATES, and it needs its own check: the reconciliation
' tier above runs on `migration`, so a `roll_rates` that divided by the loans
' it still SAW rather than the whole starting bucket would pass every count
' assertion and report a book quietly curing. Every row plus its attrition
' must come to exactly 1.
rate_rows = 0
rate_ok = 0
for k = 1 to count(observed_on) - 2
    rr = credit.roll_rates(table, observed_on[k], observed_on[k + 1])
    for each b in rr.buckets
        if rr.starting[b] > 0 then
            total = rr.unobserved[b]
            for each c in rr.buckets
                total = total + rr.rates[b][c]
            next
            rate_rows = rate_rows + 1
            if round(total, 9) = 1 then
                rate_ok = rate_ok + 1
            end if
        end if
    next
end for
check("every roll-rate row plus its attrition comes to one", rate_ok, rate_rows)
check("over enough non-empty buckets to mean something", rate_rows > 40, true)

r0 = credit.roll_rates(table, observed_on[7], observed_on[8])
check("an empty bucket's roll rate is unknown, not zero",
      is_unknown(r0.rates["charged_off"]["current"]), true)

' --- TIER: the planted portfolio -----------------------------------------
' The analytics must find exactly the six accounts, by computing. Nothing in
' the table says which they are.
v = credit.vintage(table, { bad: ["dpd_90", "dpd_120_plus"],
                            basis: "original", cohort_by: "quarter" })
check("three cohorts", count(v.cohorts), 3)
check("named by quarter", string(v.cohorts), string(["2026-Q1", "2026-Q2", "2026-Q3"]))


' By month 9 an account that stopped after two payments is well past 90 days
' delinquent under either convention.
check("the planted cohort's bad rate at age 9 is exactly four in twenty",
      rate_at(v, "2026-Q2", 9), 4 / 20)
check("the cohort before it is clean", rate_at(v, "2026-Q1", 9), 0)
check("the cohort after it is clean", rate_at(v, "2026-Q3", 9), 0)
check("the denominator is the cohort as written",
      count_at_risk(v, "2026-Q2", 9), 20)


' CUMULATIVE MEANS EVER-REACHED: once bad, counted at every later age too.
climbs = true
last = 0 - 1
for each p in v.series["2026-Q2"]
    if p.bad < last then
        climbs = false
    end if
    last = p.bad
next
check("a cumulative curve never falls", climbs, true)

' A COHORT'S CURVE STOPS AT THE AGE IT HAS REACHED. Carrying the young cohort
' out to the old one's age would report 0% bad at ages it has not lived
' through -- "no losses" where the truth is "no data".
q1_len = count(v.series["2026-Q1"])
q3_len = count(v.series["2026-Q3"])
check("the older cohort's curve is longer", q1_len > q3_len, true)
check("and the younger one stops rather than reporting zeroes",
      q3_len < count(observed_on), true)

' --- TIER: the differences -----------------------------------------------
' Asserting the two conventions PART COMPANY where the convention says they
' should proves more than asserting a value: a library with one convention
' hardcoded produces perfectly reasonable numbers.
table_ots = credit.observe(book, observed_on, "ots")
differ = 0
mba_worse = 0
ots_worse = 0
ladder = credit.buckets()
for i = 0 to count(table) - 1
    a = table[i].status
    b = table_ots[i].status
    if a != b then
        differ = differ + 1
        if find(ladder, a) > find(ladder, b) then
            mba_worse = mba_worse + 1
        else
            ots_worse = ots_worse + 1
        end if
    end if
end for
check("MBA and OTS disagree on the same loans", differ > 0, true)
check("and MBA is never the gentler of the two", ots_worse, 0)
check("it is harsher on every row where they differ", mba_worse, differ)

' The bases are two different curves, and the outstanding one is the higher:
' its denominator sheds the loans that paid off and kept the numerator.
vo = credit.vintage(table, { bad: ["dpd_90", "dpd_120_plus"],
                             basis: "outstanding", cohort_by: "quarter" })
check("the outstanding basis gives a different rate",
      rate_at(vo, "2026-Q2", 18) != rate_at(v, "2026-Q2", 18), true)
check("and a higher one, since its denominator sheds the loans that ran off",
      rate_at(vo, "2026-Q2", 18) > rate_at(v, "2026-Q2", 18), true)
check("a delinquent loan is in that denominator ONCE, not twice",
      count_at_risk(vo, "2026-Q2", 9) <= 20, true)

' --- TIER: losses --------------------------------------------------------
' A charge-off is a DECISION, read from the record rather than inferred from
' days past due. Three of the planted accounts are written off; one recovers.
wo {date}= "2027-03-01"
rec {date}= "2027-05-01"
written = []
n = 0
for each i in bad_at
    if n < 3 then
        append(written, specs[20 + i].id)
    end if
    n = n + 1
next
loss_table = []
for each r in table
    if contains(written, r.id) and r.as_of >= wo then
        append(loss_table, { id: r.id, opened: r.opened, as_of: r.as_of,
                             status: "charged_off", balance: r.balance })
    else
        append(loss_table, r)
    end if
next
' A recovery after the write-off, on one account only.
for i = 0 to count(loss_table) - 1
    if loss_table[i].id = written[0] and loss_table[i].as_of = rec then
        row = loss_table[i]
        row["recovery"] = money.of("USD", "1500")
        loss_table[i] = row
    end if
end for

ls = credit.losses(loss_table, { cohort_by: "quarter" })
check("three accounts were written off", ls.count, 3)
check("all in the planted cohort", string(ls.cohorts), string(["2026-Q2"]))
slot = ls.by_cohort["2026-Q2"]
check("gross and net are reported separately, never netted silently",
      slot.gross != slot.net, true)
check("and net is gross less the recovery",
      money.text(slot.net, 2), money.text(slot.gross - money.of("USD", "1500"), 2))
check("the recovery is counted once", money.text(slot.recoveries, 2), "1500.00")

' The charged-off accounts are absorbing: they must not roll back out.
absorbed = credit.migration(loss_table, rec, rec + (1 month))
check("charged-off loans stay charged off", absorbed.counts["charged_off"]["charged_off"], 3)

' --- TIER: refusals ------------------------------------------------------
' Each beside its nearest legal neighbour, so the tier cannot be satisfied by
' a library that refuses everything.
on error goto next

x = credit.bucket([], observed_on[0], "fico")
check("an undeclared delinquency method is refused",
      error.message,
      "credit.bucket: method must be \"mba\" or \"ots\" -- they are a month apart and neither is the default (got fico)")
error.clear()

bogus = [{ id: "A", opened: q1a, as_of: observed_on[3], status: "late" }]
x = credit.check(bogus)
check("a status the ladder does not name is refused",
      error.message,
      "credit: row 1 has status late, which is not a bucket (current, dpd_30, dpd_60, dpd_90, dpd_120_plus, paid_off, charged_off)")
error.clear()

twice = [{ id: "A", opened: q1a, as_of: observed_on[3], status: "current" },
         { id: "A", opened: q1a, as_of: observed_on[3], status: "dpd_30" }]
x = credit.check(twice)
check("the same loan twice on one date is refused", error.message,
      "credit: loan A appears twice on 2026-04-01 -- the migration count would be right and the population double")
error.clear()

missing {date}= "2030-01-01"
x = credit.migration(table, observed_on[8], missing)
check("a migration against a date with no rows is refused", error.message,
      "credit.migration: no rows dated 2030-01-01 -- comparing against an empty date reports 100% attrition")
error.clear()

x = credit.vintage(table, { bad: ["dpd_90"] })
check("a vintage with no declared basis is refused",
      error.message,
      "credit.vintage needs a basis: \"original\" divides by the cohort as it was written, \"outstanding\" by what is still on the book, and they are different curves")
error.clear()

cured = [{ id: "A", opened: q1a, as_of: observed_on[3], status: "charged_off",
           balance: money.of("USD", "100") },
         { id: "A", opened: q1a, as_of: observed_on[4], status: "current",
           balance: money.of("USD", "100") }]
x = credit.migration(cured, observed_on[3], observed_on[4])
check("a charged-off loan that cures is refused as a data defect",
      error.message,
      "credit.migration: loan A left the charged_off bucket for current -- absorbing states do not roll")
error.clear()

' The controls: the same shapes, one step legal.
fine = credit.bucket([], observed_on[0], "ots")
check("a declared method is accepted", fine, "current")
error.clear()

fine2 = credit.migration(table, observed_on[8], observed_on[9])
check("a migration between two populated dates is accepted", fine2.total > 0, true)
error.clear()

' cohort_by defaults to month, which is why this is nine and not three.
fine3 = credit.vintage(table, { bad: ["dpd_90"], basis: "original" })
check("a vintage with a declared basis is accepted", count(fine3.cohorts), 9)
error.clear()

paid = [{ id: "A", opened: q1a, as_of: observed_on[3], status: "paid_off",
          balance: money.of("USD", "0") },
        { id: "A", opened: q1a, as_of: observed_on[4], status: "paid_off",
          balance: money.of("USD", "0") }]
fine4 = credit.migration(paid, observed_on[3], observed_on[4])
check("and an absorbing loan that STAYS put is fine",
      fine4.counts["paid_off"]["paid_off"], 1)
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
