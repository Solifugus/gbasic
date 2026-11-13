' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' acctexport by hand: validation, the plan's classification, refusals (each
' beside a legal neighbour), rounding, allocations, payouts, mappings, skips,
' losses, determinism and the manifest. Expected values are worked out in
' tests/acctexport/axfixture.bas's header, never read back from the library.
' The layouts themselves are checked from outside by check_layouts.py.

function check(label, got, want)
    G.checks = G.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        G.mismatches = G.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' The fields of every problem, as "type id field".
function where(v)
    out = []
    for each p in v.problems
        append(out, p.type + " " + p.id + " " + p.field)
    next
    return out
end function

function said(v, text)
    for each p in v.problems
        if contains(p.why, text) then
            return true
        end if
    next
    return false
end function

' A bundle of one invoice with one line of 1 x price, amount and total as given.
function one_line(price, amount, mode)
    return acctexport.bundle({ organization: { name: "t", rounding: mode },
        customers: [ { id: "c", name: "C" } ],
        invoices: [ { id: "i", number: "1", customer: "c", issue_date: axfixture.d("2027-01-01"), due_date: axfixture.d("2027-01-01"),
                      currency: "USD", total: axfixture.m("USD", amount),
                      lines: [ { description: "x", quantity: 1, unit_price: axfixture.m("USD", price), amount: axfixture.m("USD", amount) } ] } ] })
end function

program main(args)
    load acctexport
    load axfixture
    G = { checks: 0, mismatches: 0 }
    dir = args[0]

    print "-- validation"
    b = axfixture.full()
    check("the fixture is valid", acctexport.validate(b).ok, true)
    x = b
    x.organization.rounding = "half_even"
    check("0.999 rounds to 1.00 either way, so half_even passes too", acctexport.validate(x).ok, true)
    check("0.335 -> 0.34 is half_up's", acctexport.validate(one_line("0.335", "0.34", "half_up")).ok, true)
    check("and half_even's (the kept digit 3 is odd, so it rounds up)", acctexport.validate(one_line("0.335", "0.34", "half_even")).ok, true)
    check("0.345 -> 0.35 is half_up's", acctexport.validate(one_line("0.345", "0.35", "half_up")).ok, true)
    v = acctexport.validate(one_line("0.345", "0.35", "half_even"))
    check("but half_even makes 0.345 0.34, so a line saying 0.35 is refused", where(v), ["invoices i lines[1].amount"])
    check("naming the arithmetic", said(v, "rounds half_even to 0.34, and the line says 0.35"), true)
    neg = one_line("1.00", "1.00", "half_up")
    neg.invoices[0].lines = [ neg.invoices[0].lines[0],
                              { description: "goodwill", quantity: 1, unit_price: axfixture.m("USD", "-0.345"), amount: axfixture.m("USD", "-0.35") } ]
    neg.invoices[0].total = axfixture.m("USD", "0.65")
    check("a negative line: -0.345 half_up is -0.35, away from zero", acctexport.validate(neg).ok, true)
    neg.organization.rounding = "half_even"
    check("and half_even's -0.34 is not -0.35", said(acctexport.validate(neg), "rounds half_even to -0.34"), true)
    check("a negative total is a credit note's", said(acctexport.validate(one_line("-1.00", "-1.00", "half_up")), "money owed back is a credit note"), true)
    x = b
    x.invoices[0].total = axfixture.m("USD", "236.99")
    v = acctexport.validate(x)
    check("a total that is not its lines", said(v, "come to 237.00 and its total says 236.99"), true)
    x = b
    x.invoices[0].lines[1].amount = axfixture.m("USD", "100.00")
    x.invoices[0].total = axfixture.m("USD", "247.00")
    check("a discount ignored in the amount", said(acctexport.validate(x), "quantity x unit_price - discount is 90"), true)
    x = b
    x.invoices[2].lines[0].tax = axfixture.m("USD", "9.50")
    check("money in the wrong currency", said(acctexport.validate(x), "tax is in USD and the document is in EUR"), true)
    x = b
    x.payments[0].allocations[0].amount = axfixture.m("USD", "120.00")
    v = acctexport.validate(x)
    check("an allocation larger than its payment", said(v, "it allocates 120.00 of a 100.00 payment"), true)
    check("and the invoice over-applied", said(v, "exceed its total by 20.00"), true)
    x = b
    x.payments[1].amount = axfixture.m("USD", "100.00")
    x.payments[1].allocations[0].amount = axfixture.m("USD", "100.00")
    x.payouts[0].amount = axfixture.m("USD", "184.66")
    check("a payment that overpays an invoice after its credit note", said(acctexport.validate(x), "exceed its total by 12.00"), true)
    x = b
    x.payouts[0].amount = axfixture.m("USD", "172.67")
    check("a payout that is not its parts", said(acctexport.validate(x), "come to 172.66 and it says 172.67"), true)
    x = b
    x.payouts[1].payments = ["pay4", "pay1"]
    check("a payment in two payouts", said(acctexport.validate(x), "'pay1' is already in payout 'po1'; paying it out twice counts it twice"), true)
    x = b
    x.refunds[0].amount = axfixture.m("USD", "88.01")
    check("a refund larger than its payment", said(acctexport.validate(x), "refunds of payment 'pay2' exceed the payment"), true)
    x = b
    x.refunds[0].credit_note = "cn1"
    check("a refund of two things", said(acctexport.validate(x), "give exactly one of `payment` and `credit_note`"), true)
    x = b
    x.fees[0].amount = -x.fees[0].amount
    check("a negative fee", said(acctexport.validate(x), "which way money moved is the record's type"), true)
    x = b
    x.invoices[1].number = "WS-0001"
    check("a number used twice", said(acctexport.validate(x), "the number 'WS-0001' is used twice"), true)
    x = b
    x.invoices[0].customer = "nobody"
    check("an unknown customer", said(acctexport.validate(x), "its customer 'nobody' is not in the bundle"), true)
    x = b
    x.organization.rounding = "bankers"
    check("rounding must be declared, not guessed", said(acctexport.validate(x), "rounding must be \"half_up\" or \"half_even\""), true)
    on error goto next
    y = acctexport.bundle({ organization: { name: "x", rounding: "half_up" }, invoices: [ { id: "a", amout: 1 } ] })
    check("a misspelt field is refused by name", contains(error.message, "invoice 1 has a field 'amout'"), true)
    error.clear()
    on error stop

    print ""
    print "-- the plan: unsupported, impossible, lossy, missing"
    pl = acctexport.plan(b, "xero.invoices")
    unsup = []
    for each u in pl.unsupported
        append(unsup, u.type)
    next
    check("xero.invoices carries invoices; the rest is unsupported", unsup, ["credit_notes", "payments", "refunds", "fees", "payouts"])
    check("and every line needs an account and a tax type", pl.classification, "impossible")
    check("named as mappings", count(pl.missing_mappings) > 0 and contains(pl.missing_mappings[0].mapping, "mappings.accounts"), true)
    xm = { accounts: { subscriptions: "200", setup: "201", usage: "202" },
           tax_types: { none: "Tax Exempt", us_sales: "Sales Tax", vat19: "19% VAT on Income" } }
    pl = acctexport.plan(b, "xero.invoices", { mappings: xm })
    check("with mappings, a service period is the one loss", [pl.classification, pl.losses[0].kind], ["lossy", "service_period"])
    on error goto next
    r = acctexport.render(b, "xero.invoices", { mappings: xm })
    check("an unsupported type is refused unless skipped", contains(error.message, "does not carry credit_notes (cn1)"), true)
    error.clear()
    r = acctexport.render(b, "xero.invoices", { mappings: xm, skip: ["credit_notes", "payments", "refunds", "fees", "payouts"] })
    check("a loss is refused unless named", contains(error.message, "Name \"service_period\" in `allow_lossy`"), true)
    error.clear()
    on error stop
    r = acctexport.render(b, "xero.invoices", { mappings: xm, allow_lossy: ["service_period"],
                                                skip: ["credit_notes", "payments", "refunds", "fees", "payouts"] })
    check("accepted, the loss is in the manifest", r.manifest.accepted_losses[0].kind, "service_period")
    check("and so is what was skipped", r.manifest.skipped[0].type, "credit_notes")
    check("10.00 off 100.00 is the exact percentage 10", contains(r.files[0].text, ",10,201,"), true)
    pq = acctexport.plan(b, "qbo.invoices", { region: "us", home_currency: "USD" })
    imp = []
    for each i in pq.impossible
        append(imp, i.id)
    next
    check("QBO US: sales tax and a second currency are impossible", imp, ["inv2", "inv4", "inv4"])
    pq = acctexport.plan(axfixture.usd_plain(), "qbo.invoices", { region: "us", home_currency: "USD" })
    kinds = []
    for each l in pq.losses
        append(kinds, l.kind)
    next
    check("QBO US on the plain part: a service period (line 1) and a folded discount (line 2)", kinds, ["service_period", "discount_folded"])
    pi = acctexport.plan(b, "qbd.iif", { home_currency: "USD" })
    check("IIF: refunds and a second currency, unsupported and impossible", pi.classification, "impossible")
    xi = axfixture.usd_plain()
    xi.customers[0].name = "Łódź"
    pi = acctexport.plan(xi, "qbd.iif", { home_currency: "USD" })
    found = false
    for each i in pi.impossible
        if contains(i.why, "IIF's ASCII text cannot carry") then
            found = true
        end if
    next
    check("IIF refuses text it cannot carry rather than transliterating", found, true)
    ps = acctexport.plan(b, "qbo.bank", { account: "clearing", currency: "USD" })
    check("a clearing statement in USD: EUR movements are impossible", ps.classification, "impossible")

    print ""
    print "-- determinism and the batch"
    g1 = acctexport.render(b, "general")
    g2 = acctexport.render(b, "general")
    same = true
    for each f, i in g1.files
        if f.text != g2.files[i].text then
            same = false
        end if
    next
    check("the same bundle gives the same bytes", same, true)
    check("and the same batch id, derived from them", g1.batch_id = g2.batch_id and starts_with(g1.batch_id, "ax-"), true)
    x = b
    x.payments[0].reference = "ch_1b"
    g3 = acctexport.render(x, "general")
    check("a different bundle, a different id", g3.batch_id != g1.batch_id, true)
    check("a given batch id is used as given", acctexport.render(b, "general", { batch_id: "2027-02" }).batch_id, "2027-02")
    m = g1.manifest
    check("counts by type", m.counts, { customers: 4, invoices: 3, credit_notes: 1, payments: 4, refunds: 1, fees: 4, payouts: 2 })
    t = []
    for each x2 in m.totals
        append(t, x2.type + " " + x2.currency + " " + x2.amount)
    next
    check("totals by type and currency, never added across currencies", t,
          ["invoices EUR 59.50", "invoices USD 238.07", "credit_notes USD 49.00", "payments EUR 59.50", "payments USD 189.07",
           "refunds USD 10.00", "fees EUR 1.20", "fees USD 6.41", "payouts EUR 58.30", "payouts USD 172.66"])
    check("says it generated, not imported", m.status, "generated")
    check("each file's checksum is of its bytes", m.files[0].sha256, hex_encode(sha256(g1.files[0].text)))
    check("the manifest is JSON", try_decode(g1.manifest_text).ok, true)

    print ""
    print "-- save"
    paths = acctexport.save(g1, dir)
    check("every file and the manifest", count(paths), count(g1.files) + 1)
    on error goto next
    again = acctexport.save(g1, dir)
    check("a batch is not overwritten by accident", contains(error.message, "already exists"), true)
    error.clear()
    on error stop
    again = acctexport.save(g1, dir, { overwrite: true })
    check("unless asked", count(again), count(paths))

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program
