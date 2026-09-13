' The facts a database cannot state about itself, written down once.
load nlq
load discovery

' A catalog as `discovery.scan` produces one: objects keyed by id.
cat = { source: "warehouse",
        tables: { "warehouse.rpt_volume_gross": { schema: "warehouse", table: "rpt_volume_gross", column: "" },
                  "warehouse.fact_volume":      { schema: "warehouse", table: "fact_volume", column: "" },
                  "trading.ctp":                { schema: "trading", table: "ctp", column: "" } },
        columns: { "warehouse.rpt_volume_gross.total_volume": { schema: "warehouse", table: "rpt_volume_gross", column: "total_volume" },
                   "warehouse.fact_volume.total_volume":      { schema: "warehouse", table: "fact_volume", column: "total_volume" },
                   "trading.ctp.code":                        { schema: "trading", table: "ctp", column: "code" } },
        primary_keys: [], edges: [] }

' Three things nobody could derive from the SQL: what `ctp` is called in
' English, which of two identical column names is the derived one, and a
' subject the estate simply does not hold.
ann = discovery.annotate(cat, {
        "trading.ctp": { synonyms: [ "counterparty" ], means: "a trading counterparty" },
        "warehouse.rpt_volume_gross": { derived_from: [ "warehouse.fact_volume" ] },
        "": { not_modelled: [ "forecast" ] } })

' The same notes serve documentation, a lineage walk, and a question. nlq reads
' them rather than keeping a second copy.
opts = nlq.options_from(ann, { limit: 3 })
print "synonyms:     " + join(keys(opts.synonyms), ", ")
print "derived_from: " + join(keys(opts.derived_from), ", ")
print "not_modelled: " + join(opts.not_modelled, ", ")

' A discovery catalog keys by id; this library takes rows. Converting preserves
' the id exactly, which is what makes the notes above line up with what a
' grounding produces.
qcat = nlq.from_discovery(ann)

g = nlq.ground(qcat, "gross volume by counterparty", nlq.options_from(ann, { limit: 3 }))
print ""
print "selected:   " + join(g.tables, ", ")
print "unresolved: " + string(count(g.unresolved))

' R2 TRAVELS AS A FACT, NOT A REFUSAL. Two objects both offer a
' `total_volume` and one is derived from the other, so the two numbers may
' legitimately differ -- nlq cannot tell whether that difference is the whole
' answer or noise without running both, and that is the application's call.
for each alt in g.alternatives
    print "alternative: " + join(alt.candidates, " and ") + " -- " + alt.why
end for

' And a question about something the estate does not model is said so, rather
' than answered from whatever happened to score highest.
g2 = nlq.ground(qcat, "what is the volume forecast?", nlq.options_from(ann, { limit: 3 }))
for each a in g2.ambiguous
    print ""
    print a.kind + ": " + join(a.candidates, ", ")
    print "why: " + a.why
end for
