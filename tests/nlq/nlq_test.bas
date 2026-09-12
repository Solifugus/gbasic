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

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
