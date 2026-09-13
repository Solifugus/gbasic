' Generated documentation: the catalog plus what a person wrote down.
load discovery

cat = { source: "erp",
        tables: { "erp.sales.invoice": { name: "invoice", type: "TABLE" } },
        columns: { "erp.sales.invoice.total":    { name: "total",    type_name: "decimal" },
                   "erp.sales.invoice.raised_on": { name: "raised_on", type_name: "date" },
                   "erp.sales.invoice.ref":      { name: "ref",      type_name: "varchar" } },
        primary_keys: [], edges: [] }
cat = discovery.annotate(cat, {
        "erp.sales.invoice":          { means: "one invoice as issued", owner: "finance" },
        "erp.sales.invoice.total":    { means: "invoice total, tax included", unit: "USD" },
        "erp.sales.invoice.raised_on": { means: "the date the invoice was issued" } })

means = discovery.notes_of(cat, "means")
units = discovery.notes_of(cat, "unit")
for each id in sort(keys(cat.columns))
    line = "  " + cat.columns[id].name + " (" + cat.columns[id].type_name + ")"
    if has(means, id) then
        line = line + " -- " + means[id]
    else
        ' A column nobody has described is worth SEEING, not hiding.
        line = line + " -- (undocumented)"
    end if
    if has(units, id) then
        line = line + " [" + units[id] + "]"
    end if
    print line
end for
