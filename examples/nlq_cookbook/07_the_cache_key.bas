' A question costs seconds of model time and a dashboard asks it every refresh.
' nlq facilitates a cache; it does not own one.
load nlq

cat = { tables: [ { schema: "trading", table: "deal" } ],
        columns: [ { schema: "trading", table: "deal", column: "deal_status" },
                   { schema: "trading", table: "deal", column: "gross_vol_mmbtu" } ] }

question = "total gross volume of active deals"
base = nlq.plan(cat, {}, question, {}).key
print "key: " + base

' Asking again changes nothing, which is the point.
print "same question again:        " + string(nlq.plan(cat, {}, question, {}).key = base)

' An unrelated table arrives in the next import. The cached SQL is still right,
' so the key must not move -- keying on the whole catalog would invalidate
' every question on every import.
more = cat
append(more.tables, { schema: "finance", table: "gl_entry" })
append(more.columns, { schema: "finance", table: "gl_entry", column: "amount" })
print "an unrelated table arrives: " + string(nlq.plan(more, {}, question, {}).key = base)

' A column the question's own table lost. The cached SQL STILL RUNS on some
' databases and answers about a shape that is gone -- so the key must move.
' Keying on table NAMES alone would survive this, which is the trap.
thin = { tables: cat.tables,
         columns: [ { schema: "trading", table: "deal", column: "deal_status" } ] }
print "its own column is dropped:  " + string(nlq.plan(thin, {}, question, {}).key = base)

' And a synonym in force is part of the question's meaning.
syn = nlq.plan(cat, {}, question, { synonyms: { volume: [ "mmbtu" ] } }).key
print "a synonym is declared:      " + string(syn = base)
