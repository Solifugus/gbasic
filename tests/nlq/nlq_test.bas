' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `nlq.ground` -- the value model and the refusals (docs/nlq_design.md).
'
' SELF-CHECKING RATHER THAN GOLDEN, and forced: a grounding's defects are all
' PLAUSIBLE ANSWERS. A ranked list with the right table second instead of first
' reads exactly like one with it first; a near miss that is silently dropped
' reads exactly like a search with no close call; a term that matched nothing
' reads exactly like a term that matched everything.

load nlq
load discovery

tally = { checks: 0, mismatches: 0 }
function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

cat = { tables: [ { schema: "trading", table: "deal" },
                  { schema: "trading", table: "counterparty" },
                  { schema: "archive", table: "deal_2015" },
                  { schema: "finance",  table: "gl_entry" },
                  { schema: "warehouse", table: "rpt_volume_gross" } ],
        columns: [ { schema: "trading", table: "deal", column: "deal_status" },
                   { schema: "trading", table: "deal", column: "gross_vol_mmbtu" },
                   { schema: "trading", table: "counterparty", column: "name" },
                   { schema: "archive", table: "deal_2015", column: "deal_status" },
                   { schema: "finance", table: "gl_entry", column: "amount" },
                   { schema: "warehouse", table: "rpt_volume_gross", column: "total_volume" } ] }

print "-- terms: a question is a bag of schema words"
check("stopwords go", contains(nlq.terms("How many deals are there?"), "deal"), true)
check("and 'how' is not a term", contains(nlq.terms("How many deals are there?"), "how"), false)
' `total`, `gross` and `net` are DELIBERATELY not stopwords -- they are half the
' measure names in a warehouse, and dropping them would make every volume
' question identical.
check("'gross' survives, being half a measure name", contains(nlq.terms("gross volume"), "gross"), true)
check("plural and singular are one word", nlq.terms("deals")[0], nlq.terms("deal")[0])

print ""
print "-- grounding ranks, and says what it nearly chose"
g = nlq.ground(cat, "How many deals have a status of ACTIVE?", { limit: 4 })
check("the table the question names is selected", contains(g.tables, "trading.deal"), true)
' AND SO IS THE ARCHIVE COPY, WHICH IS THE HONEST RESULT. `archive.deal_2015`
' scores IDENTICALLY -- same table word, same matching column -- so the order
' between them is decided by the tie-break and not by the question. NOTHING IN
' THE CATALOG SAYS WHICH IS LIVE (design R6), so ranking one above the other
' would be a guess wearing the clothes of a lookup. Asserted as a TIE, because
' "trading.deal came first" would pass on an alphabetical accident and hide it.
d_live = 0
d_arch = 0
for each row in g.detail
    if row.id = "trading.deal" then
        d_live = row.score
    end if
    if row.id = "archive.deal_2015" then
        d_arch = row.score
    end if
end for
check("and the archive copy ties with it, which the catalog cannot break", d_live, d_arch)
' THE NEAR MISS IS THE POINT. "The answer is trading.deal" and "the answer is
' trading.deal, and archive.deal_2015 was next" are different claims, and only
' the second lets a reader see the search had a close call.
narrow = nlq.ground(cat, "How many deals have a status of ACTIVE?", { limit: 1 })
check("and what it nearly chose is reported", count(narrow.near_misses) > 0, true)
check("the search reports its own width", g.search.width, 5)
check("and how many candidates matched at all", g.search.matched > 1, true)
check("and the score a near miss had to beat", g.search.cut > 0, true)

print ""
print "-- a term that reached NOTHING is named"
' The half a ranked list cannot show: a word nothing matched is the likeliest
' place a grounding is about to be confidently wrong.
g2 = nlq.ground(cat, "What is the general ledger balance?", { limit: 3 })
check("an unmatched word is reported", contains(g2.unresolved, "ledger"), true)
check("and a matched one is not", contains(g2.unresolved, "gl"), false)

print ""
print "-- a DECLARED synonym closes the loop the unresolved field opened"
' Declared by whoever owns the estate, never derived: guessing that `gl` means
' `general ledger` is a search over the whole schema with no null model, right
' often enough to be trusted and wrong silently.
g3 = nlq.ground(cat, "What is the general ledger balance?",
                { limit: 3, synonyms: { ledger: [ "gl" ], general: [ "gl" ] } })
check("the declared synonym reaches the table", contains(g3.tables, "finance.gl_entry"), true)
' AND IT MUST STOP REPORTING THE TERM AS UNRESOLVED, or an operator is told to
' declare a synonym they have already declared.
check("and the term is no longer called unresolved", contains(g3.unresolved, "ledger"), false)
' CONTROL: the same question without the synonym does NOT reach it -- otherwise
' "synonyms work" is satisfied by a grounding that found it anyway.
check("CONTROL: without the synonym it is not reached", contains(g2.tables, "finance.gl_entry"), false)

print ""
print "-- the schema is part of the name"
' Three tables called gl_entry are told apart by nothing else, and a question
' saying "in finance" is naming the schema out loud.
gs = nlq.ground(cat, "counterparty names in trading", { limit: 1 })
check("a schema word lifts its own tables", starts_with(gs.tables[0], "trading."), true)

print ""
print "-- ordering is TOTAL, so a driver's row order cannot decide the answer"
rev = { tables: [], columns: cat.columns }
i = count(cat.tables) - 1
while i >= 0
    append(rev.tables, cat.tables[i])
    i = i - 1
end while
ga = nlq.ground(cat, "deal status", { limit: 3 })
gb = nlq.ground(rev, "deal status", { limit: 3 })
check("the same question over a reordered catalog ranks identically",
      join(ga.tables, ","), join(gb.tables, ","))

print ""
print "-- R1/R6: what the catalog CANNOT settle is reported, not resolved"
' `trading.deal` and `archive.deal_2015` are not this case -- different names.
' The case is the same object name in different schemas, which is estateforge's
' planted NULL REGION: three `tmp_load_notes` where the truth says any lineage
' reported is INVENTED, and nothing in the catalog says which is live.
nullish = { tables: [ { schema: "staging",   table: "tmp_load_notes" },
                      { schema: "staging_2", table: "tmp_load_notes" },
                      { schema: "staging_3", table: "tmp_load_notes" },
                      { schema: "retail_banking", table: "account" } ],
            columns: [ { schema: "staging",   table: "tmp_load_notes", column: "note" },
                       { schema: "staging_2", table: "tmp_load_notes", column: "note" },
                       { schema: "staging_3", table: "tmp_load_notes", column: "note" },
                       { schema: "retail_banking", table: "account", column: "current_balance" } ] }
gn = nlq.ground(nullish, "load notes", { limit: 8 })
check("three identically named tables are reported ambiguous", count(gn.ambiguous) > 0, true)
check("and the kind names what the catalog cannot do", gn.ambiguous[0].kind, "same_name_different_schema")
check("and all three candidates are named", count(gn.ambiguous[0].candidates), 3)
' THE REFUSAL IS AT THE ANSWER, NOT THE GROUNDING. A caller may legitimately
' want the grounding to show a person the candidates and ask; what must not
' happen is a NUMBER produced from it, because that number is indistinguishable
' from a right one.
on error goto next
nlq.check_answerable(gn)
check("answering from it is refused", contains(error.message, "cannot be answered from the catalog alone"), true)
check("and the refusal names the candidates", contains(error.message, "staging_2.tmp_load_notes"), true)
error.clear()
on error stop
' CONTROL: an unambiguous grounding IS answerable. Without it the refusal is
' satisfied by a check that refuses everything.
gok = nlq.ground(nullish, "current balance of the account", { limit: 2 })
check("CONTROL: an unambiguous grounding is answerable", nlq.check_answerable(gok), true)
check("and it carries no ambiguity", count(gok.ambiguous), 0)

print ""
print "-- value vocabulary: the literal a catalog cannot supply"
' MEASURED, AND IT IS WHY THIS EXISTS. The first real model call produced
'   SELECT COUNT(*) FROM retail_banking.account WHERE account_status = 'open'
' against data that says 'OPEN'. The SQL is perfect and it returns 0 with no
' error. A catalog gives column NAMES and not column VALUES.
vrows = [ { schema: "retail_banking", table: "account", column: "account_status", value: "OPEN" },
          { schema: "retail_banking", table: "account", column: "account_status", value: "CLOSED" },
          { schema: "retail_banking", table: "account", column: "account_status", value: "OPEN" },
          { schema: "retail_banking", table: "account", column: "account_status", value: "FROZEN" } ]
v = nlq.vocabulary(vrows, {})
check("a repeated value is listed once", count(v["retail_banking.account.account_status"]), 3)
' A CEILING IS PART OF THE DEFINITION, and a column that overflows it gets NO
' vocabulary rather than a truncated one -- a partial list is worse than none,
' because a literal absent from it would be reported unknown when it is merely
' unlisted.
wide = []
i = 0
while i < 40
    append(wide, { schema: "s", table: "t", column: "c", value: "v" + string(i) })
    i = i + 1
end while
vw = nlq.vocabulary(wide, { max_values: 25 })
check("a high-cardinality column carries NO vocabulary", has(vw, "s.t.c"), false)
check("CONTROL: under the cap it does", has(nlq.vocabulary(wide, { max_values: 100 }), "s.t.c"), true)

vcat = { tables: [ { schema: "retail_banking", table: "account" } ],
         columns: [ { schema: "retail_banking", table: "account", column: "account_status" } ] }
vg = nlq.ground(vcat, "How many accounts are open?", { limit: 4 })
lits = nlq.check_literals(vg, v, "How many accounts are open?")
check("the wrong-case literal is caught before any model runs", count(lits), 1)
check("and it names what the column actually holds", lits[0].means, "OPEN")
check("and what the question said", lits[0].said, "open")
' CONTROL: a question whose words are not values of the column reports nothing,
' or "it catches literals" is satisfied by a check that flags every word.
check("CONTROL: an unrelated question flags nothing",
      count(nlq.check_literals(vg, v, "how many accounts are there")), 0)

print ""
print "-- R3/R4: what generated SQL may NAME, and may DO"
' R4 IS STRUCTURAL, NOT A REQUEST. The system prompt asks for a SELECT; that is
' an instruction, and instructions are not enforcement.
' R3 CATCHES A CONFIDENT LIE: a query naming a table the grounding never
' surfaced is a hallucination that happens to be spelled correctly -- and it
' RUNS, if the name exists, which in an estate holding deal, deal_2015,
' stg_deal and dim_deal it very well might.
sg = { tables: [ "trading.deal", "warehouse.stg_deal" ] }
check("a grounded read-only query is clean",
      count(nlq.check_sql("SELECT COUNT(*) FROM warehouse.stg_deal", sg, {})), 0)
un = nlq.check_sql("SELECT * FROM archive.deal_2015 JOIN trading.deal d ON d.id = 1", sg, {})
check("a table the grounding never surfaced is flagged", un[0].kind, "ungrounded_table")
check("and it is named", un[0].detail, "archive.deal_2015")
wr = nlq.check_sql("DELETE FROM trading.deal", sg, {})
check("a write is refused whatever the prompt asked for", wr[0].kind, "not_read_only")
' CONTROL: an explicitly allowed table is not flagged, or R3 would make a
' legitimate widening impossible.
check("CONTROL: an allowed table passes",
      count(nlq.check_sql("SELECT 1 FROM archive.deal_2015", sg, { allow: [ "archive.deal_2015" ] })), 0)

print ""
print "-- plan / interpret / settle: three pure steps an application drives"
' NONE PERFORMS I/O, and that is the design. A single answer() would have made
' the latency decision by HIDING it -- it blocks, and every consumer inherits
' blocking. The shape is `agent`'s, which solved the same problem here.
pcat = { tables: [ { schema: "retail_banking", table: "account" } ],
         columns: [ { schema: "retail_banking", table: "account", column: "status" },
                    { schema: "retail_banking", table: "account", column: "balance" } ] }
pvoc = { "retail_banking.account.status": [ "ACTIVE", "CLOSED" ] }
pl = nlq.plan(pcat, pvoc, "How many accounts are active?", { limit: 4 })
check("a plan is produced", pl.ok, true)
check("and it carries what it is about to cost", pl.estimated_tokens > 0, true)
check("and the objects it will be about", contains(pl.tables, "retail_banking.account"), true)
' A model asked for SQL only will still sometimes fence it; refusing on
' punctuation when the SQL is right there would be pedantry, not rigour.
rd = nlq.interpret(pl, "```sql" + chr(10) + "SELECT COUNT(*) FROM retail_banking.account" + chr(10) + "```", {})
check("a fenced answer is unwrapped", starts_with(rd.sql, "SELECT"), true)
check("and it passes R3/R4", rd.ok, true)
an = nlq.settle(rd, [ { n: 42 } ])
check("the value comes back", an.value, 42)
' R5: an answer never travels without its query.
check("with the SQL that produced it", contains(an.sql, "retail_banking.account"), true)
check("and the tables it touched", count(an.tables) > 0, true)

print ""
print "-- a plan SURVIVES ENCODE, because an application may answer later"
' The constraint `agent` found from the same direction: a run that cannot be
' stored is a run no deferring application can use. An application that emails
' the answer must keep the plan in between.
revived = decode(encode(pl))
check("a plan round-trips through encode", revived.ok, pl.ok)
' THE DIFFERENCE, not the round trip: "it encodes" alone is satisfied by a plan
' that encodes and then behaves differently.
r1 = nlq.interpret(pl, "SELECT COUNT(*) FROM retail_banking.account", {})
r2 = nlq.interpret(revived, "SELECT COUNT(*) FROM retail_banking.account", {})
check("and the revived plan produces the SAME reading", r2.sql + "|" + string(r2.ok),
      r1.sql + "|" + string(r1.ok))
check("and the same key", revived.key, pl.key)

print ""
print "-- the cache key covers what the SQL DEPENDS ON, not the question alone"
' Keying on question text is the trap: an import that drops a column leaves
' cached SQL that STILL RUNS and answers about the old shape.
same = nlq.plan(pcat, pvoc, "How many accounts are active?", { limit: 4 })
check("the same question over the same catalog keys the same", same.key, pl.key)
dropped = { tables: pcat.tables,
            columns: [ { schema: "retail_banking", table: "account", column: "status" } ] }
check("dropping a column changes the key", nlq.plan(dropped, pvoc, "How many accounts are active?", { limit: 4 }).key != pl.key, true)
voc2 = { "retail_banking.account.status": [ "ACTIVE", "CLOSED", "FROZEN" ] }
check("changing the vocabulary changes it", nlq.plan(pcat, voc2, "How many accounts are active?", { limit: 4 }).key != pl.key, true)
check("declaring a synonym changes it",
      nlq.plan(pcat, pvoc, "How many accounts are active?", { limit: 4, synonyms: { active: [ "open" ] } }).key != pl.key, true)
' CONTROL: a key that changed on everything would be useless as a cache key.
check("CONTROL: asking again does not change it", nlq.plan(pcat, pvoc, "How many accounts are active?", { limit: 4 }).key, pl.key)

print ""
print "-- exemplars: the columns a vocabulary cannot reach"
' MEASURED: the question said "contract 1", the column holds '0000000001', and
' the model wrote `contract_ref = 1` -- 0 rows where the answer is 4, silently.
' Not a VALUE problem (hundreds of distinct values) but a FORMAT one.
exrows = [ { schema: "trading", table: "deal", column: "contract_ref", value: "1" },
           { schema: "trading", table: "deal", column: "contract_ref", value: "0000000001" },
           { schema: "trading", table: "deal", column: "contract_ref", value: "0000000002" } ]
ex = nlq.exemplars(exrows, {})
' THE LONGEST, not the first: picking "1" from a column that also holds
' "0000000001" would teach exactly the wrong lesson.
check("the fullest instance is chosen", ex["trading.deal.contract_ref"], "0000000001")
excat = { tables: [ { schema: "trading", table: "deal" } ],
          columns: [ { schema: "trading", table: "deal", column: "contract_ref" } ] }
exp = nlq.plan(excat, {}, "deals under contract 1", { limit: 2, exemplars: ex })
check("and it reaches the prompt", contains(exp.user, "0000000001"), true)
check("CONTROL: without exemplars it does not", contains(nlq.plan(excat, {}, "deals under contract 1", { limit: 2 }).user, "0000000001"), false)

print ""
print "-- a refused grounding is a refused plan, and says why"
refcat = { tables: [ { schema: "staging", table: "tmp_notes" },
                     { schema: "staging_2", table: "tmp_notes" } ],
           columns: [ { schema: "staging", table: "tmp_notes", column: "note" },
                      { schema: "staging_2", table: "tmp_notes", column: "note" } ] }
rp = nlq.plan(refcat, {}, "the notes", { limit: 4 })
check("a plan over an unsettleable grounding is not ok", rp.ok, false)
check("and it carries the reason as a VALUE, not a raise", count(rp.refused_because) > 0, true)
on error goto next
nlq.interpret(rp, "SELECT 1", {})
check("and interpreting it is refused", contains(error.message, "was refused"), true)
error.clear()
on error stop

print ""
print "-- R2: two lineages are two answers, REPORTED not refused"
' MEASURED, WITH A NUMBER: asked for total volume, the model summed
' warehouse.fact_volume and returned 4,025,053 where the question is about
' trading.deal and the answer is 6,285,487 -- the fact table is built by an ETL
' that keeps only active counterparties, so the sum of a subset is a perfectly
' good number 36% short of the one asked for.
dcat = { tables: [ { schema: "trading", table: "deal" },
                   { schema: "warehouse", table: "fact_volume" },
                   { schema: "trading_emea", table: "deal" } ],
         columns: [ { schema: "trading", table: "deal", column: "gross_vol_mmbtu" },
                    { schema: "warehouse", table: "fact_volume", column: "gross_vol_mmbtu" },
                    { schema: "trading_emea", table: "deal", column: "gross_vol_mmbtu" } ] }
dg = nlq.ground(dcat, "total gross_vol_mmbtu", { limit: 4,
        derived_from: { "warehouse.fact_volume": "trading.deal" } })
check("a derived alternative is reported", count(dg.alternatives), 1)
check("naming both objects", count(dg.alternatives[0].candidates), 2)
check("and which is built from which", contains(dg.alternatives[0].why, "is built from"), true)
' THE CANDIDATES ARE THE TWO IN THE RELATIONSHIP, not everything holding the
' column. trading_emea.deal shares every column and is derived from NOTHING;
' naming it would make a regional partition look like a staging copy.
check("a peer sharing the column is NOT named",
      contains(dg.alternatives[0].candidates, "trading_emea.deal"), false)
' IT DISCLOSES, IT DOES NOT REFUSE -- and that is a correction. Built first as a
' blocker it refused 8 of 16 benchmark questions, the same failure R6 had at 15
' of 16: for t_total_volume the two derivations differ by 36% and the
' distinction IS the answer; for t_active_deals they agree exactly (315 either
' way) and it is noise. NLQ cannot tell those apart without running both, which
' is the application's decision to make.
check("it does not block answering", nlq.check_answerable(dg), true)
check("and carries no blocking ambiguity", count(dg.ambiguous), 0)
' CONTROL: with no derivation declared there is nothing to disclose, or this
' would fire on every estate that has two tables with a column in common.
check("CONTROL: undeclared derivation reports nothing",
      count(nlq.ground(dcat, "total gross_vol_mmbtu", { limit: 4 }).alternatives), 0)
' ONE ENTRY PER PAIR, NOT PER WORD: `gross_vol_mmbtu` matches gross, vol AND
' mmbtu, and three copies of one fact buries it.
check("and one entry per pair however many words matched",
      contains(dg.alternatives[0].measure, ","), true)

print ""
print "-- R6's other half: a fact the catalog does not hold at all"
' MEASURED FIRST, AND THE OBVIOUS SIGNAL DOES NOT WORK. "Which job reads X, and
' what breaks if it is dropped?" is unanswerable because a catalog has tables
' and columns and no notion of a JOB -- but `unresolved` does not separate it:
' that question leaves 4 of 9 words unmatched while an answerable one leaves 6
' of 9. Counting unmatched words measures verbosity, not answerability.
nmcat = { tables: [ { schema: "staging", table: "tmp_rebate_2019" } ],
          columns: [ { schema: "staging", table: "tmp_rebate_2019", column: "amount" } ] }
nmq = "Which job reads staging.tmp_rebate_2019, and what breaks if it is dropped?"
ng = nlq.ground(nmcat, nmq, { limit: 4, not_modelled: [ "job", "breaks", "dropped" ] })
found = false
for each a2 in ng.ambiguous
    if a2.kind = "not_in_the_catalog" then
        found = true
    end if
end for
check("a question about something the catalog does not model is refused", found, true)
on error goto next
nlq.check_answerable(ng)
check("and answering it is refused", contains(error.message, "not_in_the_catalog"), true)
error.clear()
on error stop
' CONTROL 1: the SAME question with nothing declared is answerable. Without it
' this is satisfied by a library that refuses any question mentioning a word.
check("CONTROL: undeclared, the same question is answerable",
      nlq.check_answerable(nlq.ground(nmcat, nmq, { limit: 4 })), true)
' CONTROL 2, AND IT IS THE GUARD THAT MATTERS: a declared term that DOES reach a
' column is not out-of-catalog. An estate with a real `job` column models jobs,
' whatever a list says -- the word resolved, so the catalog holds it.
jobcat = { tables: [ { schema: "ops", table: "run" } ],
           columns: [ { schema: "ops", table: "run", column: "job_name" } ] }
jg = nlq.ground(jobcat, "which job ran last", { limit: 4, not_modelled: [ "job" ] })
check("CONTROL: a declared term that reaches a column is not refused",
      nlq.check_answerable(jg), true)

print ""
print "-- A SCHEMA-LESS CATALOG, which is the first consumer's actual shape"
' gdash imports the tables a dashboard needs into ONE SQLite file, and SQLite
' has no schemas. Every estate measured against here has them, so this whole
' path was untested against the one consumer that exists -- the same blind spot
' as a fixture without views, and it would have surfaced as "NLQ does not work"
' rather than as "NLQ was never run this way".
flat = { tables: [ { schema: "", table: "account" },
                   { schema: "", table: "product" } ],
         columns: [ { schema: "", table: "account", column: "status" },
                    { schema: "", table: "account", column: "balance" },
                    { schema: "", table: "product", column: "product_code" } ] }
fv = nlq.vocabulary([ { schema: "", table: "account", column: "status", value: "ACTIVE" },
                      { schema: "", table: "account", column: "status", value: "CLOSED" } ], {})
check("a column keys without a leading dot", contains(keys(fv), "account.status"), true)
fp = nlq.plan(flat, fv, "How many accounts are active?", { limit: 2 })
check("a plan is produced without schemas", fp.ok, true)
check("and the object is named bare", contains(fp.tables, "account"), true)
check("and the prompt carries it", contains(fp.user, "account(status, balance)"), true)
check("and the vocabulary reaches it", contains(fp.user, "ACTIVE"), true)
fr = nlq.interpret(fp, "SELECT COUNT(*) FROM account WHERE status = 'ACTIVE'", {})
check("R3 accepts an unqualified grounded table", fr.ok, true)
' AND STILL REFUSES ONE IT NEVER SURFACED -- without this, "it works flat" is
' satisfied by a check that stopped checking once the dots went away.
fbad = nlq.check_sql("SELECT * FROM customers", { tables: [ "account" ] }, {})
check("and refuses one it never surfaced", fbad[0].kind, "ungrounded_table")
check("an alias and a join do not confuse it",
      count(nlq.check_sql("SELECT a.x FROM account a JOIN product p ON p.id = a.id",
                          { tables: [ "account", "product" ] }, {})), 0)
fa = nlq.settle(fr, [ { n: 79 } ])
check("and a value settles with its query", fa.value, 79)

print ""
print "-- the declared facts come FROM the catalog, not beside it"
' These are properties of the ESTATE, not of any one question, so a caller that
' uses `discovery` should not keep a second copy -- the same note that lets a
' question find rpt_volume_gross belongs in generated documentation and in a
' lineage walk. Five options had accumulated here one measurement at a time
' before that was noticed.
acat = { source: "s",
         tables: { "warehouse.rpt_volume_gross": { name: "rpt_volume_gross" },
                   "warehouse.fact_volume": { name: "fact_volume" } },
         columns: { "warehouse.rpt_volume_gross.total_volume": { name: "total_volume" },
                    "warehouse.fact_volume.total_volume": { name: "total_volume" } },
         primary_keys: [], edges: [] }
ann = discovery.annotate(acat, {
        "warehouse.rpt_volume_gross": { synonyms: [ "report" ] },
        "warehouse.fact_volume": { derived_from: [ "warehouse.rpt_volume_gross" ] },
        "": { not_modelled: [ "job" ] } })
ao = nlq.options_from(ann, { limit: 4 })
' A synonym note says "this OBJECT is also called that"; a question carries the
' WORD. So the map must run word -> the object's own words, which is the
' direction `ground` expands in -- the other way round would never match.
check("a synonym note becomes a question synonym", contains(keys(ao.synonyms), "report"), true)
check("pointing at the object's own words", contains(ao.synonyms["report"], "rpt"), true)
check("a derivation note becomes derived_from", contains(keys(ao.derived_from), "warehouse.fact_volume"), true)
check("and the estate's not_modelled comes through", contains(ao.not_modelled, "job"), true)
' READ, NOT MERGED BEHIND THE CALLER'S BACK: an explicit option is more specific
' than a standing note, and silently overriding it would be the surprise.
check("an explicit option wins over the note",
      join(nlq.options_from(ann, { not_modelled: [ "sla" ] }).not_modelled, ","), "sla")
' CONTROL: a catalog with no notes changes nothing, or this would invent
' options for every caller that never annotated anything.
check("CONTROL: an un-annotated catalog adds nothing",
      has(nlq.options_from(acat, { limit: 4 }), "synonyms"), false)

print ""
print "-- the two catalog shapes meet, and the ID survives the crossing"
' `discovery.scan` keys objects BY ID, this library takes rows, and the notes
' `options_from` reads are written against DISCOVERY IDS. So converting is not
' a reshape -- if the id changed, every derivation note would be looked up
' under a name no grounding ever produces, silently, and the disclosure would
' simply stop appearing. THE LOAD-BEARING ASSERTION IS THEREFORE THE ID, not
' the counts: a conversion that dropped the schema still yields a catalog that
' grounds perfectly well and answers about the wrong thing.
dcat = { source: "wh",
         tables: { "warehouse.rpt_volume_gross": { schema: "warehouse", table: "rpt_volume_gross", column: "" },
                   "warehouse.fact_volume": { schema: "warehouse", table: "fact_volume", column: "" } },
         columns: { "warehouse.rpt_volume_gross.total_volume": { schema: "warehouse", table: "rpt_volume_gross", column: "total_volume" },
                    "warehouse.fact_volume.total_volume": { schema: "warehouse", table: "fact_volume", column: "total_volume" } },
         primary_keys: [], edges: [] }
q = nlq.from_discovery(dcat)
check("every object crosses", count(q.tables), 2)
check("and every column", count(q.columns), 2)
gq = nlq.ground(q, "total volume", { limit: 2 })
check("the id a grounding produces IS the discovery id",
      contains(gq.tables, "warehouse.rpt_volume_gross"), true)
' AND THE PROOF THAT MATTERS: a note written against the discovery id reaches
' the grounding through the conversion. Without the id surviving, this is the
' check that goes quiet -- the alternative simply never fires and the output
' looks like an estate with no derivations in it.
dann = discovery.annotate(dcat,
        { "warehouse.rpt_volume_gross": { derived_from: [ "warehouse.fact_volume" ] } })
gd = nlq.ground(nlq.from_discovery(dann), "total volume",
                nlq.options_from(dann, { limit: 2 }))
check("a note written against a discovery id reaches the grounding",
      count(gd.alternatives) > 0, true)
' CONTROL: the same catalog with NO note reports no alternative, or "it fires"
' would be satisfied by a library that discloses an alternative for every pair.
check("CONTROL: with no note there is nothing to disclose", count(gq.alternatives), 0)
' The notes travel with the conversion, so annotating and asking need ONE
' catalog rather than two kept in step by hand.
check("the notes cross too", has(nlq.from_discovery(dann), "notes"), true)
' AND THE DEPTH VARIES BY DRIVER, which is why the split is at the LAST
' separator and not at the first. SQLite qualifies nothing, SQL Server
' qualifies with source, catalog and schema -- a conversion that assumed two
' segments would produce `src.db` as a schema and lose the rest, silently.
sqlite_cat = { tables: { "orders": { schema: "", table: "orders", column: "" } },
               columns: { "orders.status": { schema: "", table: "orders", column: "status" } },
               primary_keys: [], edges: [] }
check("an unqualified id survives", nlq.ground(nlq.from_discovery(sqlite_cat), "order status", {}).tables[0], "orders")
deep_cat = { tables: { "src.db.dbo.orders": { schema: "", table: "orders", column: "" } },
             columns: { "src.db.dbo.orders.status": { schema: "", table: "orders", column: "status" } },
             primary_keys: [], edges: [] }
check("and a four-segment one survives whole",
      nlq.ground(nlq.from_discovery(deep_cat), "order status", {}).tables[0], "src.db.dbo.orders")

print ""
print "-- gdash-12: a plan that cannot be answered is NOT ok"
' `plan` refused on AMBIGUITY and not on UNANSWERABILITY, so a question with
' nothing to ground against came back `ok: true` with a prompt naming no
' tables. Reported by gdash, who measured five such questions against a real
' dashboard and found all five planned fine. It fails SAFE -- `check_sql` then
' refuses whatever the model invents -- and EXPENSIVELY, because the model was
' paid for first, and it reports a SQL problem for something never about SQL.
empty_q = nlq.plan(cat, {}, "what is the share price?", {})
check("a question that grounds nothing is refused", empty_q.ok, false)
check("and says which words reached nothing",
      contains(empty_q.refused_because[0].candidates, "share"), true)
check("naming the kind", empty_q.refused_because[0].kind, "nothing_grounded")
' THE CONTROL, without which "refuses" is satisfied by refusing everything.
check("CONTROL: a question that grounds something still plans",
      nlq.plan(cat, {}, "how many deals are active?", {}).ok, true)
' A refused plan has the SAME SHAPE as an accepted one, so a caller can read a
' field without checking `ok` first -- true of the ambiguity refusal too now.
check("a refused plan still carries tables", count(empty_q.tables), 0)

print ""
print "-- gdash-12: what a column MEANS reaches the prompt"
' Reported with a measurement: money materialised as INTEGER minor units, so
' `amount` holds 125075 meaning 1250.75, and "over 1000 dollars" produced valid
' read-only SQL against the right table that was WRONG BY A FACTOR OF A HUNDRED.
' Vocabulary shows 125075 is a real value; an exemplar shows what an integer
' looks like; neither states a SCALE. `discovery.annotate` had `means` and
' `unit` all along and NEITHER REACHED `prompt`.
ncat = { source: "main",
         tables: { "main.orders": { schema: "main", table: "orders", column: "" } },
         columns: { "main.orders.amount": { schema: "main", table: "orders", column: "amount" } },
         primary_keys: [], edges: [] }
nann = discovery.annotate(ncat, { "main.orders.amount": { means: "stored in US cents", unit: "cents" } })
nopts = nlq.options_from(nann, {})
check("options_from carries the note", has(nopts, "notes"), true)
check("means and unit both arrive", contains(nopts.notes["main.orders.amount"], "cents"), true)
np = nlq.plan(nlq.from_discovery(nann), {}, "how many orders were over 1000 dollars?", nopts)
check("and the prompt states it", contains(np.user, "stored in US cents"), true)
check("under a heading a model can read", contains(np.user, "What the columns mean"), true)
' THE CONTROL: without the note the prompt says nothing about units, which is
' the state gdash measured -- so this pair is the difference, not a claim.
nplain = nlq.plan(nlq.from_discovery(ncat), {}, "how many orders were over 1000 dollars?", {})
check("CONTROL: with no note, nothing is stated", contains(nplain.user, "What the columns mean"), false)
' A note for a column of a table the grounding did NOT select must not be
' dragged in -- the prompt budget is the thing being protected.
check("only notes for grounded objects appear",
      contains(nlq.plan(nlq.from_discovery(nann), {}, "how many orders?", nopts).user, "cents"), true)

print ""
print "-- refusals, each beside its nearest legal neighbour"
on error goto next
nlq.ground({ tables: [] }, "anything", {})
check("a catalog with no columns is refused", contains(error.message, "no 'columns'"), true)
error.clear()
nlq.ground(cat, "how many are there", {})
check("a question with no schema word is refused", contains(error.message, "no term a schema could match"), true)
error.clear()
nlq.ground(cat, "deals", { limti: 3 })
check("an unknown option is refused by name", contains(error.message, "unknown option 'limti'"), true)
error.clear()
nlq.ground(cat, "deals", { synonyms: { deal: "trade" } })
check("a synonym that is not an array is refused", contains(error.message, "must be an array"), true)
error.clear()
on error stop
check("CONTROL: a well-formed call is accepted", count(nlq.ground(cat, "deals", {}).tables) > 0, true)
on error goto next
nlq.check_answerable({ tables: [ "x" ] })
check("check_answerable refuses a value that is not a grounding", contains(error.message, "expects a grounding"), true)
error.clear()
nlq.from_discovery({ tables: {} })
check("from_discovery refuses a value that is not a catalog", contains(error.message, "expects a catalog"), true)
error.clear()
nlq.ground(dcat, "total volume", {})
check("and ground names the conversion rather than just refusing",
      contains(error.message, "from_discovery"), true)
error.clear()
on error stop

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
