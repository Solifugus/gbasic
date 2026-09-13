' A question the catalog cannot settle, and a near neighbour it can.
load nlq

' Three staging schemas differing only by a NUMBER. Nothing here says which one
' is live -- that fact exists in somebody's head, not in the catalog.
staged = { tables: [ { schema: "staging",   table: "deal_load" },
                     { schema: "staging_2", table: "deal_load" },
                     { schema: "staging_3", table: "deal_load" } ],
           columns: [ { schema: "staging",   table: "deal_load", column: "deal_id" },
                      { schema: "staging_2", table: "deal_load", column: "deal_id" },
                      { schema: "staging_3", table: "deal_load", column: "deal_id" } ] }

p = nlq.plan(staged, {}, "how many rows are in the deal load?", {})
print "ok: " + string(p.ok)
for each r in p.refused_because
    print "  " + r.kind + ": " + join(r.candidates, ", ")
    print "  why: " + r.why
end for

' Now three REGIONAL schemas. Same shape, same column, same repeated table
' name -- but `emea` and `apac` are words a question can use, so the catalog
' does offer a distinction and the refusal must not fire.
regional = { tables: [ { schema: "trading",      table: "deal" },
                       { schema: "trading_emea", table: "deal" },
                       { schema: "trading_apac", table: "deal" } ],
             columns: [ { schema: "trading",      table: "deal", column: "deal_id" },
                        { schema: "trading_emea", table: "deal", column: "deal_id" },
                        { schema: "trading_apac", table: "deal", column: "deal_id" } ] }

p2 = nlq.plan(regional, {}, "how many deals are there?", {})
print ""
print "regional partitions -- ok: " + string(p2.ok)
print "tables: " + join(p2.tables, ", ")
