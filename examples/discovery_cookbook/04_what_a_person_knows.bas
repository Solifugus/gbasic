' The third tier of fact: what a person knows and the database does not.
load discovery

' A catalog `scan` would have produced -- written out here so the recipe needs
' no database.
cat = { source: "erp",
        tables: { "erp.sales.customer":    { name: "customer",    type: "TABLE" },
                  "erp.sales.customer2":   { name: "customer2",   type: "TABLE" },
                  "erp.sales.customer_new": { name: "customer_new", type: "TABLE" } },
        columns: { "erp.sales.customer2.balance": { name: "balance" } },
        primary_keys: [], edges: [] }

' Nothing in any schema says which of the three the business actually uses.
' That is not a gap in the reader -- it is not in the database at all.
cat = discovery.annotate(cat, {
        "erp.sales.customer":     { authority: "superseded", means: "the 2019 customer master" },
        "erp.sales.customer2":    { authority: "live", means: "the customer master", owner: "sales ops" },
        "erp.sales.customer_new": { authority: "archive", means: "an abandoned migration" },
        "erp.sales.customer2.balance": { means: "outstanding balance", unit: "USD" } })

for each id in sort(keys(discovery.notes_of(cat, "authority")))
    print discovery.notes_of(cat, "authority")[id] + "  " + id
end for
print ""
print "and every note says who said so: " + cat.notes["erp.sales.customer2"].known_by
