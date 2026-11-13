' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' The bundles tests/run_acctexport.sh exports, built by hand so every amount
' below can be checked with a pencil. A month of a software company:
'
'   inv1  USD  3 seats @ 49.00 = 147.00, setup 100.00 less 10.00 = 90.00  total 237.00
'   inv2  USD  3 x 0.333 = 0.999 -> 1.00 (half_up), tax 0.07             total   1.07
'   inv4  EUR  50.00 + 19% VAT 9.50                                       total  59.50
'   cn1   USD  one seat credited, applies to inv1                         total  49.00
'   pay1  USD  100.00 -> inv1 (partial)     pay2 USD 88.00 -> inv1 (the rest: 237 - 49 - 100)
'   pay3  USD    1.07 -> inv2               pay4 EUR 59.50 -> inv4
'   ref1  USD   10.00 of pay2
'   fees  USD 3.20 (pay1), 2.88 (pay2), 0.33 (pay3); EUR 1.20 (pay4)
'   po1   USD  100.00 + 88.00 + 1.07 - 10.00 - 3.20 - 2.88 - 0.33 = 172.66
'   po2   EUR  59.50 - 1.20 = 58.30
library axfixture

    load acctexport

    function m(code, text)
        return money.of(code, text)
    end function

    function d(text)
        x {date}= text
        return x
    end function

    function customers()
        return [ { id: "c1", name: "Łódź Print House", email: "ap@lodz.example",
                   address: { line1: "ul. Piotrkowska 1", city: "Łódź", postal_code: "90-001", country: "PL" } },
                 { id: "c2", name: "Acme \"Quoted\", Inc.", email: "billing@acme.example" },
                 { id: "c3", name: "Café Ñandú GmbH", address: { city: "Berlin", country: "DE" } },
                 { id: "c4", name: "Plain Co" } ]
    end function

    function full()
        return acctexport.bundle({
            organization: { name: "Worksplicer Inc.", rounding: "half_up" },
            customers: customers(),
            invoices: [
                { id: "inv1", number: "WS-0001", customer: "c1", issue_date: d("2027-02-01"), due_date: d("2027-02-15"),
                  currency: "USD", total: m("USD", "237.00"), memo: "Team plan, \"annual\" terms",
                  lines: [ { id: "l1", description: "Team plan, per seat\nJanuary 2027", quantity: 3, unit_price: m("USD", "49.00"),
                             amount: m("USD", "147.00"), category: "subscriptions", item: "TEAM-SEAT",
                             service_from: d("2027-01-01"), service_to: d("2027-01-31") },
                           { id: "l2", description: "Onboarding, set-up", quantity: 1, unit_price: m("USD", "100.00"),
                             discount: m("USD", "10.00"), amount: m("USD", "90.00"), category: "setup" } ],
                  external: { crm: "deal-17", billing: "sub_77" } },
                { id: "inv2", number: "WS-0002", customer: "c2", issue_date: d("2027-02-02"), due_date: d("2027-03-04"),
                  currency: "USD", total: m("USD", "1.07"),
                  lines: [ { id: "l3", description: "API calls (metered)", quantity: 3, unit_price: m("USD", "0.333"),
                             amount: m("USD", "1.00"), tax: m("USD", "0.07"), tax_code: "us_sales", category: "usage" } ] },
                { id: "inv4", number: "WS-0004", customer: "c3", issue_date: d("2027-02-03"), due_date: d("2027-02-17"),
                  currency: "EUR", total: m("EUR", "59.50"),
                  lines: [ { id: "l4", description: "Team plan (EU)", quantity: 1, unit_price: m("EUR", "50.00"),
                             amount: m("EUR", "50.00"), tax: m("EUR", "9.50"), tax_code: "vat19", category: "subscriptions" } ] } ],
            credit_notes: [
                { id: "cn1", number: "WS-CN-0001", customer: "c1", issue_date: d("2027-02-04"), currency: "USD",
                  total: m("USD", "49.00"), applies_to: "inv1",
                  lines: [ { id: "l5", description: "One seat removed", quantity: 1, unit_price: m("USD", "49.00"),
                             amount: m("USD", "49.00"), category: "subscriptions" } ] } ],
            payments: [
                { id: "pay1", customer: "c1", date: d("2027-02-05"), currency: "USD", amount: m("USD", "100.00"),
                  method: "card", reference: "ch_1", processor: "Stripe", allocations: [ { invoice: "inv1", amount: m("USD", "100.00") } ] },
                { id: "pay2", customer: "c1", date: d("2027-02-06"), currency: "USD", amount: m("USD", "88.00"),
                  method: "card", reference: "ch_2", processor: "Stripe", allocations: [ { invoice: "inv1", amount: m("USD", "88.00") } ] },
                { id: "pay3", customer: "c2", date: d("2027-02-06"), currency: "USD", amount: m("USD", "1.07"),
                  method: "card", reference: "ch_3", processor: "Stripe", allocations: [ { invoice: "inv2", amount: m("USD", "1.07") } ] },
                { id: "pay4", customer: "c3", date: d("2027-02-07"), currency: "EUR", amount: m("EUR", "59.50"),
                  method: "sepa", reference: "ch_4", processor: "Stripe", allocations: [ { invoice: "inv4", amount: m("EUR", "59.50") } ] } ],
            refunds: [ { id: "ref1", customer: "c1", date: d("2027-02-08"), currency: "USD", amount: m("USD", "10.00"),
                         payment: "pay2", reference: "re_1", processor: "Stripe" } ],
            fees: [ { id: "fee1", date: d("2027-02-05"), currency: "USD", amount: m("USD", "3.20"), processor: "Stripe", payment: "pay1", description: "card fee" },
                    { id: "fee2", date: d("2027-02-06"), currency: "USD", amount: m("USD", "2.88"), processor: "Stripe", payment: "pay2", description: "card fee" },
                    { id: "fee3", date: d("2027-02-06"), currency: "USD", amount: m("USD", "0.33"), processor: "Stripe", payment: "pay3", description: "card fee" },
                    { id: "fee4", date: d("2027-02-07"), currency: "EUR", amount: m("EUR", "1.20"), processor: "Stripe", payment: "pay4", description: "sepa fee" } ],
            payouts: [ { id: "po1", date: d("2027-02-10"), currency: "USD", amount: m("USD", "172.66"), processor: "Stripe", reference: "po_A",
                         payments: ["pay1", "pay2", "pay3"], refunds: ["ref1"], fees: ["fee1", "fee2", "fee3"] },
                       { id: "po2", date: d("2027-02-11"), currency: "EUR", amount: m("EUR", "58.30"), processor: "Stripe", reference: "po_B",
                         payments: ["pay4"], fees: ["fee4"] } ] })
    end function

    ' The USD, untaxed part, with no refund: what QuickBooks Online (US) and an
    ' IIF file can carry. Names are ASCII, as IIF requires.
    function usd_plain()
        b = full()
        b.customers = [ { id: "c1", name: "Lodz Print House", email: "ap@lodz.example" }, { id: "c4", name: "Plain Co" } ]
        b.invoices = [ b.invoices[0] ]
        b.invoices[0].memo = "Team plan, annual terms"
        b.invoices[0].lines[0].description = "Team plan, per seat, January 2027"
        b.payments = [ b.payments[0], b.payments[1] ]
        b.refunds = []
        b.fees = [ b.fees[0], b.fees[1] ]
        b.payouts = [ { id: "po1", date: d("2027-02-10"), currency: "USD", amount: m("USD", "181.92"), processor: "Stripe",
                        reference: "po_A", payments: ["pay1", "pay2"], fees: ["fee1", "fee2"] } ]
        return b
    end function

    ' Everything in USD: the fixture without inv4, pay4, fee4 and po2.
    function usd_all()
        b = full()
        b.invoices = [ b.invoices[0], b.invoices[1] ]
        b.payments = [ b.payments[0], b.payments[1], b.payments[2] ]
        b.fees = [ b.fees[0], b.fees[1], b.fees[2] ]
        b.payouts = [ b.payouts[0] ]
        return b
    end function

    ' n invoices of k lines each, every line 1 x 10.00, for the split rules.
    function big(n, k)
        invs = []
        for i = 1 to n
            lines = []
            for j = 1 to k
                append(lines, { id: "l" + string(j), description: "line " + string(j), quantity: 1,
                                unit_price: m("USD", "10.00"), amount: m("USD", "10.00"), category: "usage" })
            next
            append(invs, { id: "b" + string(i), number: "B-" + string(i), customer: "c4", issue_date: d("2027-03-01"),
                           due_date: d("2027-03-31"), currency: "USD", total: m("USD", string(10 * k) + ".00"), lines: lines })
        next
        return acctexport.bundle({ organization: { name: "Worksplicer Inc.", rounding: "half_even" },
                                   customers: [ { id: "c4", name: "Plain Co" } ], invoices: invs })
    end function
end library
