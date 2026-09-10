' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' discovery -- READING SQL. See docs/discovery_design.md.
'
' SEPARATE FROM discovery_test.bas BECAUSE IT NEEDS NOTHING. Everything here
' is a pure function of a string: no driver, no connection, no database. Those
' checks used to live beside the catalog ones and were therefore SKIPPED
' wherever no ODBC driver was installed -- a whole parser guarded by a tier
' that can go quiet, which is the failure mode this project keeps finding in
' its own gate. Moved rather than duplicated: two copies of an assertion drift.
'
' SELF-CHECKING AND FORCED. Every defect in a SQL reader is a PLAUSIBLE
' ANSWER -- a truncated expression still reads like an expression, a column
' paired with the wrong name still reads like a lineage, a numeric literal
' reported as a source column still reads like a column. A golden would record
' any of them as expected and defend it.

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
print ""
print "-- reading SQL for the objects it touches"
' PORTABLE AND DIALECT-FREE: these are pure string checks, and they are the
' bulk of the value. Each case exists because a plausible implementation gets
' it wrong -- a `from` inside a comment or a string literal, a CTE name, a
' subquery, a MERGE whose `update set` looks like a table called `set`.
function refs_of(sql, side)
    r = discovery.references(sql)
    return join(r[side], ",")
end function

check("a plain select reads its table", refs_of("select a from t1", "reads"), "t1")
check("insert..select reads and writes", refs_of("insert into tgt select * from src", "writes"), "tgt")
check("and the source is a read", refs_of("insert into tgt select * from src", "reads"), "src")
' DELETE FROM is the one place `from` does not mean a read.
check("delete from is a WRITE, not a read", refs_of("delete from gone where 1=1", "writes"), "gone")
check("and delete reads nothing", refs_of("delete from gone where 1=1", "reads"), "")
check("a join is a read", refs_of("select 1 from a join b on 1=1", "reads"), "a,b")
check("merge writes its target", refs_of("merge into d using s on 1=1 when matched then update set d.x = s.x", "writes"), "d")
' A MERGE READS BOTH SIDES, and the target side is the half that is easy to
' miss: `when matched then update set d.x = s.x` takes rows from `d` to decide
' what matched. Reported as a write alone, altering a column of `d` would show
' no impact on the module that reads it.
check("and USING is the source read", refs_of("merge into d using s on 1=1 when matched then update set d.x = s.x", "reads"), "d,s")
check("an update reads the table it writes", refs_of("update t set x = y * 2 where z = 1", "reads"), "t")
' THE CONTROL: a DELETE reads the table to choose rows and puts no value into
' anything, so it is a write and only a write. Without this the rule above is
' indistinguishable from "every target is also a read".
check("CONTROL: a delete is a write and not a read", refs_of("delete from gone where x = 1", "reads"), "")
' A KEYWORD IS NOT A NAME. Found by testing: MERGE's `update set d.x` emitted a
' table called `set`, and a phantom object in a lineage graph is reported with
' exactly the same confidence as a real one.
check("`set` is never mistaken for a table", contains(refs_of("merge into d using s on 1=1 when matched then update set d.x = s.x", "writes"), "set"), false)
check("a CTE name is not a table", refs_of("with cte as (select * from real_t) select * from cte", "reads"), "real_t")
check("a subquery is not a table", refs_of("select * from (select id from inner_t) x", "reads"), "inner_t")
check("a comment is not SQL", refs_of("-- insert into ghost select * from nowhere" + chr(10) + "select 1 from real_t", "reads"), "real_t")
check("a string literal is not SQL", refs_of("select 'from ghost' from real_t", "reads"), "real_t")
check("delimited names survive", refs_of("select * from [Odd Name]", "reads"), "Odd Name")
check("a qualified name keeps its parts", refs_of("select * from a.b.c", "reads"), "a.b.c")
' THE GAP IS THE LOAD-BEARING OUTPUT. Dynamic SQL is a hop that CANNOT be read,
' and a tracer that drops it silently produces a lineage graph that is
' confidently incomplete -- worse than one naming its own holes.
check("dynamic SQL is reported as a gap", count(discovery.references("exec ('select * from ' + @t)").gaps) > 0, true)
check("and ordinary SQL reports none", count(discovery.references("select * from t").gaps), 0)

print ""
print "-- the projection: which columns an output is built from"
pj = discovery.projection("select id, sum(amt) as total, a + b as combined from t where x = 1")
check("every output column is found", count(pj), 3)
check("a bare column names itself", pj[0].output, "id")
check("an aliased aggregate takes its alias", pj[1].output, "total")
check("and is marked as an aggregate", pj[1].aggregate, true)
check("naming the column it aggregates", join(pj[1].sources, ","), "amt")
check("an expression names every column it reads", join(pj[2].sources, ","), "a,b")
check("and a plain column is not an aggregate", pj[0].aggregate, false)
' `select *` IS NOT EXPANDED. Expanding it needs the shape of something this
' function was not given, and guessing is how a lineage graph becomes
' confident and wrong.
check("select * is reported, not expanded", discovery.projection("select * from t")[0].output, "*")

print ""
print "-- the predicates, which ARE the lineage"
' A `where` clause is not metadata about a derivation, it is the reason two
' numbers differ.
pr = discovery.predicates("select a from t join u on u.id = t.id where status = 'ACTIVE' group by a having count(*) > 2")
kinds = []
for each q in pr
    append(kinds, q.kind)
end for
check("join, where and having are all captured", join(kinds, ","), "join,where,having")
' Guarded before indexing: with the predicate scan disabled the list is empty,
' and a crash on pr[1] would hide the mismatch that already named the problem.
if count(pr) < 2 then
    check("(cannot check the literal: no predicates were found)", false, true)
else
' THE LITERAL IS THE EXPLANATION. "status = 'ACTIVE'" is the answer;
' "status = '...'" is not -- so string contents are kept, while the token kind
' still stops them ever being read as an object name.
check("the literal VALUE survives", contains(pr[1].text, "ACTIVE"), true)
end if
check("and a literal is still never a table", join(discovery.references("select 'from ghost' from real_t").reads, ","), "real_t")

print ""
print "-- explain: why two same-named columns disagree"
' THE QUESTION THE LIBRARY EXISTS FOR. Two reports show a number by the same
' name, they differ, and someone spends a day finding out why. Both are
' correct; what is wanted is where the derivations parted.
gross = "select d, sum(gross_amt) as total_volume from fact_t group by d"
net = "select d, sum(net_amt) as total_volume from fact_t where status = 'ACTIVE' group by d"
ex = discovery.explain({ name: "total_volume", body: gross }, { name: "total_volume", body: net })
check("the shared ancestor is named", join(ex.shared, ","), "fact_t")
check("they are not identical", ex.identical, false)
found_pred = false
found_expr = false
for each dd in ex.differences
    if contains(dd, "ACTIVE") then
        found_pred = true
    end if
    if contains(dd, "gross_amt") and contains(dd, "net_amt") then
        found_expr = true
    end if
end for
check("the narrowing predicate is reported as the difference", found_pred, true)
check("and so is the differing expression", found_expr, true)
' THE CONTROL, and it is what stops the library inventing a distinction so as
' to have something to say: two identical derivations must report NONE.
same = discovery.explain({ name: "total_volume", body: gross }, { name: "total_volume", body: gross })
check("CONTROL: a column compared with itself differs in nothing", same.identical, true)
check("and reports no differences at all", count(same.differences), 0)

print ""
print "-- statements: a procedure body is not one statement"
' A VIEW IS ONE SELECT AND A PROCEDURE IS NOT, which is where the ETL lives.
' Everything above this line reads the first statement it finds; a body with
' three would have had two of them silently dropped.
st = discovery.statements("truncate table w.stg; insert into w.stg (a) select b from src; update w.stg set a = a + 1")
check("every statement is found", count(st), 3)
kinds = []
targets = []
for each s in st
    append(kinds, s.kind)
    append(targets, s.target)
end for
check("each is classified by its VERB", join(kinds, ","), "truncate,insert,update")
check("and each names what it writes", join(targets, ","), "w.stg,w.stg,w.stg")
' THE VERB, NOT THE FIRST WORD. Both of these begin with something that is not
' the verb, and the verb is what says which side carries the output names.
check("`create view ... as select` is a select", discovery.statements("create view v as select a from t")[0].kind, "select")
check("`with cte as (...) insert` is an insert", discovery.statements("with c as (select 1 x) insert into t (a) select x from c")[0].kind, "insert")
' T-SQL MAKES THE SEMICOLON OPTIONAL. A body written without them is not a
' malformed body, it is the ordinary case in a great deal of real code.
check("statements split with no separator at all", count(discovery.statements("truncate table a insert into a (x) select y from b update a set z = 1")), 3)
' THE THREE EXCEPTIONS, each of which was a wrong answer before it was an
' exception. A select that FEEDS a write belongs to that write ...
check("the select feeding an insert is not a second statement", count(discovery.statements("insert into t (a) select b from s")), 1)
' ... a set operation is one statement with several selects in it ...
check("a union is one statement", count(discovery.statements("select a from x union all select a from y")), 1)
' ... and a MERGE owns every arm it declares.
check("a merge owns its arms", count(discovery.statements("merge into d using s on d.id = s.id when matched then update set d.x = s.x when not matched then insert (id) values (s.id)")), 1)

print ""
print "-- derivations: the output column PAIRED with what fills it"
' `projection` reads a select list, which names its own outputs. An INSERT
' does not: the names are on one side of the statement and the expressions on
' the other, and pairing them is the whole difficulty.
dv = discovery.derivations("insert into f (deal_id, avail) select s.id, s.gross * (1 - s.pvr) from stg s join ctp k on k.code = s.code where k.active = 1")[0]
check("the target is the table written", dv.target, "f")
check("the outputs are the INSERT's names, not the select's", dv.columns[0].output + "," + dv.columns[1].output, "deal_id,avail")
check("paired POSITIONALLY with the expressions", dv.columns[1].expression, "s.gross * (1 - s.pvr)")
' A NUMERIC LITERAL IS NOT A COLUMN. Digits are legal inside an identifier, so
' `1` and `0.97` read exactly like names -- and `_sources_in` had been
' reporting `1` as a source column of `x * (1 - y)` since it was written.
' Harmless until something tried to resolve it, and then an entry in the
' UNRESOLVED list, which is the one field a caller has to be able to trust.
check("a literal is not reported as a source column", join(dv.columns[1].sources, ","), "gross,pvr")
check("the qualifier is kept for lineage", dv.columns[1].source_refs[0].qualifier, "s")
check("and the predicate travels with the derivation", count(dv.predicates) > 0, true)
' AN UPDATE NAMES BOTH SIDES ITSELF -- the one shape with no pairing decision.
up = discovery.derivations("update f set avail = gross * 0.97, status = 'R' where status = 'A'")[0]
check("an update pairs by assignment", up.columns[0].output + " <- " + up.columns[0].expression, "avail <- gross * 0.97")
check("a literal assignment has no sources", count(up.columns[1].sources), 0)
' A MERGE ASSIGNS FROM SEVERAL ARMS, and a column assigned differently in the
' matched and not-matched arms genuinely has two derivations.
mg = discovery.derivations("merge into d using s on d.id = s.id when matched then update set d.name = upper(s.name) when not matched then insert (id, name) values (s.id, s.name)")[0]
outs = []
for each c in mg.columns
    append(outs, c.output)
end for
check("both merge arms are reported", join(outs, ","), "name,id,name")
' AND THE JOIN PREDICATE STOPS AT THE FIRST ARM. Without `when` among its
' terminators the `on` clause swallowed every arm and reported the whole
' statement as one condition.
check("the join predicate is the join predicate", mg.predicates[0].text, "d.id = s.id")

print ""
print "-- what CANNOT be paired is said so, not guessed"
' BOTH OF THESE PRODUCE A PERFECTLY ORDINARY SET OF EXPRESSIONS if paired
' anyway, with the WRONG NAMES attached -- and a column lineage built on that
' is confident and wrong in a way nothing downstream can detect.
mism = discovery.derivations("insert into t (a, b, c) select x, y from s")[0]
check("a count mismatch is refused, not truncated", count(mism.columns), 0)
check("and it says which counts disagreed", contains(mism.notes[0], "3") and contains(mism.notes[0], "2"), true)
nolist = discovery.derivations("insert into t select x, y from s")[0]
check("no column list is a note, not a guess", count(nolist.notes), 1)
check("and it names the catalog as what would settle it", contains(nolist.notes[0], "ordinal"), true)
' THE CONTROL: a statement that CAN be paired must carry no note at all, or
' "notes" would just mean "this library reads insert statements".
check("CONTROL: a pairable insert notes nothing", count(discovery.derivations("insert into t (a) select x from s")[0].notes), 0)

print ""
print "-- only a depth-zero `as` is an alias"
' `cast(x as int)` has an `as` that is part of the expression. Taking it cut
' the derivation off at the cast and named the output column `int` -- a
' plausible name, on a truncated expression, with nothing raised.
cst = discovery.projection("select 'D-' + cast(f.id as varchar(20)) as reference from f")[0]
check("the output keeps its real alias", cst.output, "reference")
' AND THE CASE WITH NO ALIAS, which is the one that actually bites: with an
' alias present the LAST `as` wins and the answer comes out right by accident,
' so a fixture that only tested the aliased form passed on the broken reader.
' Inside an `insert ... select` the expressions are unaliased by construction,
' which is exactly where this was found.
bare = discovery.derivations("insert into g (reference) select 'D-' + cast(f.id as varchar(20)) from f")[0]
check("an unaliased cast is not truncated", contains(bare.columns[0].expression, "varchar(20)"), true)
check("and the output name comes from the insert, not from the cast", bare.columns[0].output, "reference")
check("nor is the cast's type read as a source column", join(bare.columns[0].sources, ","), "id")

print ""
print "-- a temp table is a hop, not a dead end"
' `select ... into #tmp` then `insert ... select ... from #tmp` is how a great
' deal of real ETL is written, and a temp table is NEVER in a catalog. Without
' this the chain dead-ends at the first one -- which in T-SQL is most
' procedures. Nothing is guessed: the statement that FILLS the temp table is
' what says which columns it has.
function add_table(cat, sch, tb, cols)
    tid = "s." + sch + "." + tb
    cat.tables[tid] = { source: "s", catalog: "", schema: sch, table: tb, column: "", type: "TABLE" }
    p = 1
    for each c in cols
        cat.columns[tid + "." + c] = { source: "s", catalog: "", schema: sch,
                                       table: tb, column: c, position: p }
        p = p + 1
    end for
    return cat
end function

tcat = { source: "s", tables: {}, columns: {}, primary_keys: [], edges: [] }
tcat = add_table(tcat, "trading", "deal", ["id", "gross"])
tcat = add_table(tcat, "w", "fact", ["id", "amt"])
tmods = [{ source: "s", schema: "w", name: "p_etl", kind: "procedure",
           body: "select d.id, d.gross * 2 as doubled into #tmp from trading.deal d; insert into w.fact (id, amt) select id, doubled from #tmp" }]
tl = discovery.lineage(tcat, tmods, "SQL Server", "s.w.fact.amt", {})
check("the chain passes through the temp table", count(tl.steps), 2)
check("and reaches the operational column behind it", join(tl.origins, ","), "s.trading.deal.gross")
check("nothing was lost on the way", count(tl.unresolved), 0)
' AND IT IS SAID SO. A local object is not a catalog object, and a caller must
' be able to tell: the id carries `::`, which no catalog id ever does, and
' `intermediates` names every one a walk went through.
check("the local object is named as one", join(tl.intermediates, ","), "s.w.p_etl::#tmp")
check("and its id cannot be mistaken for a catalog id", contains(tl.steps[0].sources[0], "::"), true)
' THE CONTROL: a name that DOES bind to the catalog is not treated as local,
' or every hop would become an intermediate and the distinction would say
' nothing.
tmods2 = [{ source: "s", schema: "w", name: "p_direct", kind: "procedure",
            body: "insert into w.fact (id, amt) select d.id, d.gross from trading.deal d" }]
tl2 = discovery.lineage(tcat, tmods2, "SQL Server", "s.w.fact.amt", {})
check("CONTROL: a real table is not an intermediate", count(tl2.intermediates), 0)
check("and the direct chain still reaches the same origin", join(tl2.origins, ","), "s.trading.deal.gross")

print ""
print "-- a view reading another view"
' In a real schema the view is often the interface, and views on views are
' ordinary. The unqualified `orders` also has to bind through the dbo search
' path, which is the shape SQL Server produces and the reason parsing and
' resolution are separate phases.
tcat = add_table(tcat, "dbo", "v1", ["amt"])
tcat = add_table(tcat, "rpt", "v2", ["total"])
tcat = add_table(tcat, "dbo", "orders", ["amt"])
vmods = [{ source: "s", schema: "dbo", name: "v1", kind: "VIEW",
           body: "select amt from orders where amt > 0" },
         { source: "s", schema: "rpt", name: "v2", kind: "VIEW",
           body: "select sum(amt) as total from dbo.v1" }]
vl = discovery.lineage(tcat, vmods, "SQL Server", "s.rpt.v2.total", {})
check("the chain runs through both views", count(vl.steps), 2)
check("and reaches the base table behind them", join(vl.origins, ","), "s.dbo.orders.amt")
check("the unqualified name bound through the search path", count(vl.unresolved), 0)

print ""
print "-- an insert with no column list is settled by the CATALOG, not guessed"
' `derivations` is a pure function of the SQL and cannot know the target's
' ordinal order, so it reports a note. `lineage` HAS a catalog and discharges
' it by the same rule the database itself applies -- which is the difference
' between "this cannot be answered here" and "this cannot be answered".
tmods3 = [{ source: "s", schema: "w", name: "p_bare", kind: "procedure",
            body: "insert into w.fact select d.id, d.gross from trading.deal d" }]
tl3 = discovery.lineage(tcat, tmods3, "SQL Server", "s.w.fact.amt", {})
' Guarded before indexing. With the catalog fill removed the walk finds no
' step at all, and a crash on steps[0] would hide the mismatch that has
' already named the problem -- the same trap this fixture's neighbour fell
' into once.
if count(tl3.steps) = 0 then
    check("the second output column pairs with the second expression", "no step at all", "s.trading.deal.gross")
else
check("the second output column pairs with the second expression",
      join(tl3.steps[0].sources, ","), "s.trading.deal.gross")
end if
check("and nothing is left unresolved once the catalog has spoken", count(tl3.unresolved), 0)
' THE CONTROL: a count that CANNOT be right is still refused. w.fact has two
' columns, so three expressions cannot fill it, and inventing a pairing would
' produce an ordinary-looking lineage with everything shifted by one.
tmods4 = [{ source: "s", schema: "w", name: "p_wrong", kind: "procedure",
            body: "insert into w.fact select d.id, d.gross, d.id from trading.deal d" }]
tl4 = discovery.lineage(tcat, tmods4, "SQL Server", "s.w.fact.amt", {})
check("CONTROL: a count the catalog contradicts is still refused", count(tl4.steps), 0)
check("and it is reported rather than dropped", count(tl4.unresolved) > 0, true)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
