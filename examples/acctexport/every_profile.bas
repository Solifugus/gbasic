' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' The same month, exported under every profile, with the mappings an
' accountant would hand over and the refusals that happen without them.
'
'   GBASIC_PATH=stdlib ./gbasic examples/acctexport/every_profile.bas

function month()
    feb1 {date}= "2027-02-01"
    feb15 {date}= "2027-02-15"
    feb3 {date}= "2027-02-03"
    feb5 {date}= "2027-02-05"
    return acctexport.bundle({
        organization: { name: "Worksplicer Inc.", rounding: "half_up" },
        customers: [ { id: "cus_1", name: "Plain Co", email: "ap@plain.example" } ],
        invoices: [ { id: "inv_1", number: "WS-1", customer: "cus_1", issue_date: feb1, due_date: feb15,
                      currency: "USD", total: money.of("USD", "120.00"),
                      lines: [ { description: "Team plan, 2 seats", quantity: 2, unit_price: money.of("USD", "60.00"),
                                 amount: money.of("USD", "120.00"), category: "subscriptions" } ] } ],
        payments: [ { id: "pay_1", customer: "cus_1", date: feb3, currency: "USD", amount: money.of("USD", "120.00"),
                      processor: "Stripe", reference: "ch_1",
                      allocations: [ { invoice: "inv_1", amount: money.of("USD", "120.00") } ] } ],
        fees: [ { id: "fee_1", date: feb3, currency: "USD", amount: money.of("USD", "3.78"), processor: "Stripe", payment: "pay_1" } ],
        payouts: [ { id: "po_1", date: feb5, currency: "USD", amount: money.of("USD", "116.22"), processor: "Stripe",
                     reference: "po_X", payments: ["pay_1"], fees: ["fee_1"] } ] })
end function

program main(args)
    load acctexport
    b = month()

    ' What a profile would do, before it does it.
    p = acctexport.plan(b, "qbo.invoices", { region: "us", home_currency: "USD" })
    print "qbo.invoices: " + p.classification
    for each u in p.unsupported
        print "  not carried: " + u.type + " " + join(u.ids, ", ")
    next

    ' QuickBooks Online takes the invoices; payments, fees and payouts are not
    ' file imports there, so they are skipped by name and the manifest says so.
    q = acctexport.render(b, "qbo.invoices", { region: "us", home_currency: "USD",
                                               skip: ["payments", "fees", "payouts"] })
    print q.files[0].text
    print "skipped: " + q.manifest.skipped[0].type

    ' The processor's balance as a clearing account: gross in, fee and payout out.
    c = acctexport.render(b, "qbo.bank", { account: "clearing", currency: "USD", skip: ["invoices"] })
    print c.files[0].text

    ' QuickBooks Desktop: every account named by the accountant; the
    ' payment-to-invoice link IIF cannot carry is accepted by name.
    i = acctexport.render(b, "qbd.iif", {
            home_currency: "USD",
            mappings: { accounts: { receivable: "Accounts Receivable", undeposited_funds: "Undeposited Funds",
                                    bank: "Checking", processor_fees: "Merchant Fees" },
                        income_accounts: { subscriptions: "Subscription Income" } },
            allow_lossy: ["payment_link"] })
    print "iif: " + string(i.manifest.files[0].rows) + " records, losses accepted: " + i.manifest.accepted_losses[0].kind

    ' Without the accounts, IIF refuses rather than inventing them.
    on error goto next
    x = acctexport.render(b, "qbd.iif", { home_currency: "USD" })
    print "refused: " + left(error.message, 80) + "..."
    error.clear()
end program
