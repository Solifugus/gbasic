' A question word that matched nothing, and the synonym that closes it.
load nlq

cat = { tables: [ { schema: "warehouse", table: "rpt_volume_gross" },
                  { schema: "trading",   table: "ctp" } ],
        columns: [ { schema: "warehouse", table: "rpt_volume_gross", column: "total_volume" },
                   { schema: "warehouse", table: "rpt_volume_gross", column: "ctp_code" },
                   { schema: "trading",   table: "ctp",              column: "code" },
                   { schema: "trading",   table: "ctp",              column: "name" } ] }

question = "gross volume by counterparty"

g = nlq.ground(cat, question, {})
print "unresolved: " + join(g.unresolved, ", ")
print "selected:   " + join(g.tables, ", ")

' Nobody outside this business knows that `ctp` means counterparty. Say so once.
syn = { counterparty: [ "ctp" ] }

g2 = nlq.ground(cat, question, { synonyms: syn })
print ""
print "with the synonym declared --"
print "unresolved: " + string(count(g2.unresolved))
print "selected:   " + join(g2.tables, ", ")
