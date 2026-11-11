' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Every profile rendered from tests/acctexport/axfixture.bas into its own
' folder under args[0], for tests/acctexport/check_layouts.py to read. The
' mappings below are the kind an accountant hands over; nothing here is a
' default the library supplies.

function out(root, name, r)
    d = join_path(root, name)
    f {file}= d
    make_dir(f, { parents: true })
    paths = acctexport.save(r, d, { overwrite: true })
    print name + " " + r.batch_id + " " + string(count(paths))
    return nothing
end function

program main(args)
    load acctexport
    load axfixture
    root = args[0]
    out(root, "general", acctexport.render(axfixture.full(), "general"))
    out(root, "qbo_us", acctexport.render(axfixture.usd_plain(), "qbo.invoices",
        { region: "us", home_currency: "USD", allow_lossy: ["discount_folded", "service_period"], skip: ["credit_notes", "payments", "refunds", "fees", "payouts"] }))
    out(root, "qbo_uk", acctexport.render(axfixture.usd_all(), "qbo.invoices",
        { region: "uk", home_currency: "USD", skip: ["credit_notes", "payments", "refunds", "fees", "payouts"],
          mappings: { tax_codes: { none: "No VAT", us_sales: "20.0% S" } },
          allow_lossy: ["discount_folded", "service_period", "tax_recomputed"] }))
    out(root, "qbo_big", acctexport.render(axfixture.big(150, 7), "qbo.invoices", { region: "us", home_currency: "USD" }))
    out(root, "qbo_tall", acctexport.render(axfixture.big(2, 600), "qbo.invoices", { region: "us", home_currency: "USD" }))
    xm = { accounts: { subscriptions: "200", setup: "201", usage: "202" },
           tax_types: { none: "Tax Exempt", us_sales: "Sales Tax", vat19: "19% VAT on Income" } }
    out(root, "xero_inv", acctexport.render(axfixture.full(), "xero.invoices",
        { mappings: xm, allow_lossy: ["service_period"], skip: ["credit_notes", "payments", "refunds", "fees", "payouts"] }))
    out(root, "xero_cn", acctexport.render(axfixture.full(), "xero.credit_notes",
        { mappings: xm, skip: ["invoices", "payments", "refunds", "fees", "payouts"] }))
    out(root, "xero_big", acctexport.render(axfixture.big(100, 7), "xero.invoices", { mappings: { accounts: { usage: "202" }, tax_types: { none: "Tax Exempt" } } }))
    for each acct in ["clearing", "bank"]
        out(root, "qbo_bank_" + acct, acctexport.render(axfixture.usd_all(), "qbo.bank", { account: acct, currency: "USD", skip: ["invoices", "credit_notes"] }))
        out(root, "xero_bank_" + acct, acctexport.render(axfixture.usd_all(), "xero.bank", { account: acct, currency: "USD", skip: ["invoices", "credit_notes"] }))
    next
    out(root, "iif", acctexport.render(axfixture.usd_plain(), "qbd.iif",
        { home_currency: "USD", allow_lossy: ["discount_folded", "payment_link"],
          mappings: { accounts: { receivable: "Accounts Receivable", undeposited_funds: "Undeposited Funds",
                                  bank: "Checking", processor_fees: "Merchant Fees" },
                      income_accounts: { subscriptions: "Subscription Income", setup: "Setup Income" } } }))
end program
