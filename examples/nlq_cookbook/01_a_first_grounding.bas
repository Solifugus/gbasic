' Grounding a question -- no model, no database.
load nlq

cat = { tables: [ { schema: "trading",   table: "deal" },
                  { schema: "trading",   table: "counterparty" },
                  { schema: "finance",   table: "gl_entry" },
                  { schema: "warehouse", table: "rpt_volume_gross" } ],
        columns: [ { schema: "trading",   table: "deal",             column: "deal_status" },
                   { schema: "trading",   table: "deal",             column: "gross_vol_mmbtu" },
                   { schema: "trading",   table: "counterparty",     column: "name" },
                   { schema: "finance",   table: "gl_entry",         column: "amount" },
                   { schema: "warehouse", table: "rpt_volume_gross", column: "total_volume" } ] }

g = nlq.ground(cat, "What is the gross volume by counterparty?", { limit: 2 })

print "terms:     " + join(g.terms, ", ")
print "selected:  " + join(g.tables, ", ")
print "near miss: " + join(g.near_misses, ", ")
print ""
print ("searched " + string(g.search.width) + " tables, " + string(g.search.matched) +
       " matched, kept " + string(g.search.limit) + ", cut at score " + string(g.search.cut))
print ""
for each row in g.detail
    print "  " + row.id + "  score " + string(row.score)
end for
