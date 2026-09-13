' plan -> interpret -> settle. The model is a string here; nothing calls out.
load nlq

cat = { tables: [ { schema: "trading", table: "deal" },
                  { schema: "archive", table: "deal_2015" } ],
        columns: [ { schema: "trading", table: "deal",      column: "deal_status" },
                   { schema: "trading", table: "deal",      column: "gross_vol_mmbtu" },
                   { schema: "archive", table: "deal_2015", column: "gross_vol_mmbtu" } ] }

question = "total gross volume of active deals"
p = nlq.plan(cat, {}, question, { limit: 1, dialect: "SQLite SQL" })
print "planned: " + join(p.tables, ", ")

' What the model said. A fence is stripped rather than failed on -- that is
' punctuation, not a refusal to follow the instruction.
said = ("```sql" + chr(10) +
        "select sum(gross_vol_mmbtu) from trading.deal where deal_status = 'ACTIVE'" + chr(10) +
        "```")
r = nlq.interpret(p, said, {})
print "ok:  " + string(r.ok)
print "sql: " + r.sql

' R3: a table the grounding never surfaced. It is spelled correctly and it
' EXISTS, so it runs -- and answers about 2015.
bad = nlq.interpret(p, "select sum(gross_vol_mmbtu) from archive.deal_2015", {})
print ""
print "off-plan -- ok: " + string(bad.ok)
for each pr in bad.problems
    print "  " + pr.kind + " (" + pr.detail + "): " + pr.why
end for

' R4: a statement that writes is refused by inspection, before anything runs
' it. The system prompt asks for a SELECT, and an instruction is not a control.
writes = nlq.interpret(p, "delete from trading.deal where deal_status = 'ACTIVE'", {})
print ""
print "a write -- ok: " + string(writes.ok)
for each pr in writes.problems
    print "  " + pr.kind + " (" + pr.detail + "): " + pr.why
end for

' Rows came back from wherever you run SQL. R5: an answer never travels
' without the query that produced it.
a = nlq.settle(r, [ { total: 41250.5 } ])
print ""
print "answer: " + string(a.value) + " (" + a.column + "), from " + string(a.row_count) + " row"
print "asked:  " + a.question
print "by:     " + a.sql
