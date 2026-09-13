' What a column actually contains -- and the literal the question got wrong.
load nlq

cat = { tables: [ { schema: "trading", table: "deal" } ],
        columns: [ { schema: "trading", table: "deal", column: "deal_status" },
                   { schema: "trading", table: "deal", column: "deal_ref" } ] }

' Sampled from the database, however you like to sample it.
samples = [ { schema: "trading", table: "deal", column: "deal_status",
              values: [ "ACTIVE", "SETTLED", "CANCELLED" ] },
            { schema: "trading", table: "deal", column: "deal_ref",
              values: [ "D-0001", "D-0002", "D-0000000917" ] } ]

vocab = nlq.vocabulary(samples, { max_values: 5 })
for each k in keys(vocab)
    print k + ": " + join(vocab[k], ", ")
end for

' A column with too many distinct values has NO vocabulary -- a truncated list
' would report a real value as unknown. It gets an exemplar instead: not which
' values exist, but what one LOOKS like. The longest is chosen, because a
' format is best shown by its fullest instance.
wide = nlq.vocabulary(samples, { max_values: 2 })
print ""
print "capped at 2 -- columns with a vocabulary: " + string(count(keys(wide)))

ex = nlq.exemplars(samples, { min_distinct: 2 })
print "deal_ref looks like: " + ex["trading.deal.deal_ref"]

' The measured failure: the question says `active`, the column stores `ACTIVE`.
g = nlq.ground(cat, "how many deals are active?", {})
for each c in nlq.check_literals(g, vocab, "how many deals are active?")
    print ""
    print "said '" + c.said + "' but " + c.column + " stores '" + c.means + "'"
    print "why: " + c.why
end for
