' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' A software company's month, handed to its accountant: one subscription
' invoice, the customer's card payment through a processor, the processor's
' fee, and the payout of what was left to the bank. The application supplies
' the records (from its own database, outside this library); acctexport turns
' them into files and a manifest.
'
'   GBASIC_PATH=stdlib ./gbasic examples/acctexport/worksplicer_month.bas [out_dir]
'
' With an out_dir the general bundle is written there; without one, the files
' are only described.

program main(args)
    load acctexport

    jan1 {date}= "2027-01-01"
    jan31 {date}= "2027-01-31"
    feb1 {date}= "2027-02-01"
    feb3 {date}= "2027-02-03"
    feb5 {date}= "2027-02-05"
    price {USD}= "49.00"
    seats = 3
    amount {USD}= "147.00"
    fee_amt {USD}= "4.56"
    net {USD}= "142.44"

    b = acctexport.bundle({
        organization: { name: "Worksplicer Inc.", rounding: "half_up" },
        customers: [ { id: "cus_1001", name: "Łódź Print House", email: "ap@lodzprint.example",
                       address: { line1: "ul. Piotrkowska 1", city: "Łódź", postal_code: "90-001", country: "PL" } } ],
        invoices: [ { id: "inv_2027_0001", number: "WS-2027-0001", customer: "cus_1001",
                      issue_date: feb1, due_date: feb1, currency: "USD", total: amount,
                      memo: "Team plan, January 2027",
                      lines: [ { id: "li_1", description: "Team plan, per seat\nJanuary 2027", quantity: seats,
                                 unit_price: price, amount: amount, category: "subscriptions", item: "TEAM-SEAT",
                                 service_from: jan1, service_to: jan31 } ],
                      external: { billing_system: "sub_77" } } ],
        payments: [ { id: "pay_9001", customer: "cus_1001", date: feb3, currency: "USD", amount: amount,
                      method: "card", reference: "ch_3Q", processor: "Stripe",
                      allocations: [ { invoice: "inv_2027_0001", amount: amount } ] } ],
        fees: [ { id: "fee_9001", date: feb3, currency: "USD", amount: fee_amt, processor: "Stripe",
                  payment: "pay_9001", description: "card processing fee" } ],
        payouts: [ { id: "po_501", date: feb5, currency: "USD", amount: net, processor: "Stripe",
                     reference: "po_1Q", payments: ["pay_9001"], fees: ["fee_9001"] } ] })

    v = acctexport.validate(b)
    print "valid: " + string(v.ok)

    ' 1. The general bundle: every record, nothing lost.
    r = acctexport.render(b, "general", { batch_id: "worksplicer-2027-02" })
    print "general: batch " + r.batch_id + ", " + string(count(r.files)) + " files"
    for each f in r.manifest.files
        print "  " + f.name + "  rows " + string(f.rows) + "  sha256 " + left(f.sha256, 12)
    next
    for each t in r.manifest.totals
        print "  total " + t.type + " " + t.currency + " " + t.amount
    next

    ' 2. Xero needs an account code and a tax type for each line: the
    ' accountant's chart, not the application's guess. Without them it refuses.
    on error goto next
    x = acctexport.render(b, "xero.invoices", { skip: ["payments", "fees", "payouts"] })
    print "xero without mappings: " + left(error.message, 95) + "..."
    error.clear()
    on error stop
    x = acctexport.render(b, "xero.invoices", {
            skip: ["payments", "fees", "payouts"],
            mappings: { accounts: { subscriptions: "200" }, tax_types: { none: "Tax Exempt (0%)" } },
            allow_lossy: ["service_period"] })
    print "xero: " + x.files[0].name + ", accepted losses: " + string(count(x.manifest.accepted_losses))

    ' 3. The payout as Xero sees the bank: one deposit of what arrived.
    s = acctexport.render(b, "xero.bank", { account: "bank", currency: "USD", skip: ["invoices"] })
    print "bank statement:"
    print s.files[0].text

    if count(args) > 0 then
        paths = acctexport.save(r, args[0])
        print "wrote " + string(count(paths)) + " files to " + args[0]
    end if
end program
