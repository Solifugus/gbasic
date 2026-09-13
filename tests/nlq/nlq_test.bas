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
on error stop

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
