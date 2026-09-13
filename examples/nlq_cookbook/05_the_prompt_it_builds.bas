' What the model is actually shown -- and the budget it must fit.
load nlq

cat = { tables: [ { schema: "trading", table: "deal" },
                  { schema: "trading", table: "counterparty" },
                  { schema: "finance", table: "gl_entry" } ],
        columns: [ { schema: "trading", table: "deal",         column: "deal_status" },
                   { schema: "trading", table: "deal",         column: "ctp_id" },
                   { schema: "trading", table: "deal",         column: "gross_vol_mmbtu" },
                   { schema: "trading", table: "counterparty", column: "ctp_id" },
                   { schema: "trading", table: "counterparty", column: "name" },
                   { schema: "finance", table: "gl_entry",     column: "amount" } ] }

samples = [ { schema: "trading", table: "deal", column: "deal_status",
              values: [ "ACTIVE", "SETTLED" ] } ]
vocab = nlq.vocabulary(samples, {})

question = "gross volume by counterparty for active deals"
g = nlq.ground(cat, question, { limit: 2 })
p = nlq.prompt(g, cat, vocab, question, { dialect: "SQLite SQL" })

print p.system
print ""
print p.user
print ""
print "about " + string(p.estimated_tokens) + " tokens, budget " + string(p.budget)

' A budget that cannot hold the schema is a REFUSAL, not a truncation. Truncate
' and it is the schema that gets dropped, never the question -- so the model
' answers confidently about the tables that survived the cut.
on error goto next
nlq.prompt(g, cat, vocab, question, { dialect: "SQLite SQL", budget_tokens: 20 })
if error then
    print ""
    print "refused: " + error.message
end if
