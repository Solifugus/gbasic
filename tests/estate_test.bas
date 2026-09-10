' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' estate -- the fabricated business estate, and the properties that make it
' usable as GROUND TRUTH. See docs/estate_design.md.
'
' THIS TIER NEEDS NO DATABASE. `spec`, `ddl` and `truth` are pure, and the
' scanner is a pure function of a string -- so the load-bearing property (R1:
' one declaration produces both the database and the truth) is checkable
' without materialising anything.

load estate
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

sp = estate.spec()
tr = estate.truth(sp)

print "-- R1: ONE declaration produces both the database and the truth"
' THE LOAD-BEARING PROPERTY. A relationship declared `enforced` must become a
' constraint in the DDL; one declared otherwise must NOT -- while appearing in
' the truth either way. A hand-written answer key drifts the first time either
' side changes, and a fixture whose answer key is wrong teaches the tool to be
' wrong and then certifies it.
d = estate.ddl(sp, "sqlserver")
ddl_text = join(d, " ; ")
enforced = 0
unenforced = 0
for each r in tr.relationships
    if r.enforced then
        enforced = enforced + 1
        parts = split(r.from, ".")
        check("enforced " + r.from + " is a constraint",
              contains(ddl_text, "foreign key (" + parts[2] + ")"), true)
    else
        unenforced = unenforced + 1
    end if
end for
check("some relationships are enforced", enforced > 0, true)
' THE CONTROL, and without it R1 is satisfied by emitting every relationship:
' an undeclared one must leave NO trace in the DDL while still being true.
check("and most are NOT, which is the common real case", unenforced > enforced, true)
check("every relationship appears in the truth", count(tr.relationships), enforced + unenforced)
' Checked on the SOURCE side, inside that table's own create statement.
' Matching the target text is not enough and the first draft got this wrong:
' `ar_invoice.cust_id` is unenforced while the ENFORCED
' `contract.counterparty_id` emits the identical `references
' trading.counterparty(id)`, so a substring search over the whole DDL reports
' a constraint that is not there.
function create_stmt_for(d, schema_name, table_name)
    for each s in d
        if starts_with(s, "create table " + schema_name + "." + table_name + " (") then
            return s
        end if
    end for
    return ""
end function

un = 0
for each r in tr.relationships
    if not r.enforced then
        parts = split(r.from, ".")
        stmt = create_stmt_for(d, parts[0], parts[1])
        if contains(stmt, "foreign key (" + parts[2] + ")") then
            un = un + 1
        end if
    end if
end for
check("an unenforced relationship leaves NO constraint behind", un, 0)

print ""
print "-- R2/R3: the truth is separate, and it NAMES the decoys"
check("no column is called is_fk or similar", contains(lower(ddl_text), "is_fk"), false)
check("decoys are named, not merely omitted", count(tr.decoys) > 0, true)
for each dc in tr.decoys
    check("the decoy " + dc.from + " says why", len(dc.why) > 0, true)
end for
check("a null region is declared", len(tr.null_region) > 0, true)
' R3b: facts that are NOT in the database at all, where the right answer is
' "unanswerable from the catalog" rather than a guess.
check("undiscoverable facts are recorded", count(tr.undiscoverable) > 0, true)

print ""
print "-- the ETL: what each module DECLARES it touches is what the scanner FINDS"
' AN INDEPENDENT CROSS-CHECK, and the reason this tier needs no database. The
' estate declares each module's reads and writes; `discovery.references`
' derives them from the SQL text without seeing the declaration. Agreement
' between two independently-written statements is evidence; a golden of either
' alone would be a transcript.
for each m in tr.flows
    r = discovery.references(m.body)
    if has(m, "gap") then
        check(m.name + " is reported as a GAP", count(r.gaps) > 0, true)
        check("and yields no phantom edges", count(r.reads) + count(r.writes), 0)
    else
        check(m.name + " reads what it declares", join(r.reads, ","), join(m.reads, ","))
        check(m.name + " writes what it declares", join(r.writes, ","), join(m.writes, ","))
    end if
end for

print ""
print "-- R33: the divergence the estate exists to pose"
' Two columns, THE SAME NAME, both correct, DIFFERENT NUMBERS. The question is
' not "are these related" but "why do they disagree", and the answer is the
' predicate. Recorded so a later increment can be measured against it.
check("a divergence is declared", count(tr.divergences) > 0, true)
dv = tr.divergences[0]
check("both sides share an ancestor", len(dv.share) > 0, true)
check("and the reason is recorded", contains(dv.because, "filters"), true)
ga = ""
gb = ""
for each m in tr.flows
    if ends_with(dv.a, m.name + ".total_volume") then
        ga = m.body
    end if
    if ends_with(dv.b, m.name + ".total_volume") then
        gb = m.body
    end if
end for
check("the two bodies differ", ga = gb, false)
check("and both read the shared ancestor",
      contains(ga, split(dv.share, ".")[1]) and contains(gb, split(dv.share, ".")[1]), true)

print ""
print "-- R33 ANSWERED: the tool's explanation against the estate's own"
' THE ORACLE. The estate declares `because` as prose, written when the estate
' was designed; `discovery.explain` derives its answer from the SQL alone,
' having never seen that prose. Agreement between two independently written
' statements is evidence -- a golden of either alone would be a transcript.
ex = discovery.explain({ name: "total_volume", body: ga }, { name: "total_volume", body: gb })
check("the shared ancestor is the one declared", contains(join(ex.shared, ","), split(dv.share, ".")[1]), true)
check("and the two are not identical", ex.identical, false)
said_filter = false
said_columns = false
for each dd in ex.differences
    if contains(dd, "ACTIVE") then
        said_filter = true
    end if
    if contains(dd, "gross_vol_mmbtu") and contains(dd, "avail_after_pvr") then
        said_columns = true
    end if
end for
' The estate's prose says BOTH things; the derived answer must say both too.
check("the derived answer names the filter the estate recorded",
      said_filter and contains(dv.because, "ACTIVE"), true)
check("and the columns the estate recorded",
      said_columns and contains(dv.because, "avail_after_pvr"), true)

print ""
print "-- R9: nothing in the null region relates to anything"
for each r in tr.relationships
    check("no relationship touches " + tr.null_region,
          starts_with(r.from, tr.null_region + ".") or starts_with(r.to, tr.null_region + "."), false)
end for

print ""
print "-- COLUMN LINEAGE, against the estate's own declared answer"
' THE ORACLE, and the same one R33 uses: the estate declares what each column
' is derived from, written by hand when the estate was designed;
' `discovery.lineage` derives its answer from the module BODIES alone, having
' never seen that list. Agreement between two independently written statements
' is evidence -- a golden of either alone is a transcript. Deriving the answer
' key from the bodies with the same parser would make it a copy of the answer.
lcat = estate.catalog(sp, "wh")
lmods = []
for each m in sp.modules
    ' The dynamic-SQL module is T-SQL only; it declares itself a gap and has
    ' nothing to derive.
    if not has(m, "dialects") then
        append(lmods, { source: "wh", schema: m.schema, name: m.name,
                        kind: m.kind, body: m.body })
    end if
end for

function step_for(ln, column, via)
    for each s in ln.steps
        if s.column = "wh." + column and s.via = "wh." + via then
            return s
        end if
    end for
    return { column: "", via: "", sources: [], expression: "" }
end function

function qualify(names)
    out = []
    for each n in names
        append(out, "wh." + n)
    end for
    return join(out, ",")
end function

' EVERY DECLARED EDGE, DERIVED. Each entry names a column, the module that
' fills it, and what it is filled FROM -- and all three must come back.
lin_checked = 0
for each want in tr.column_lineage
    ln = discovery.lineage(lcat, lmods, "PostgreSQL", "wh." + want.column, {})
    got = step_for(ln, want.column, want.via)
    check("lineage: " + want.column + " via " + want.via,
          join(got.sources, ","), qualify(want.from))
    lin_checked = lin_checked + 1
end for
check("every declared edge was checked", lin_checked, count(tr.column_lineage))

' THE WHOLE CHAIN, not one hop. This is the question that costs a day: a
' number on a report, four hops from the operational table it came out of.
full = discovery.lineage(lcat, lmods, "PostgreSQL", "wh.warehouse.rpt_volume_net.total_volume", {})
check("the trail ends where the estate says it ends",
      join(full.origins, ","), qualify(tr.lineage_origins))
check("and it took more than one hop to get there", count(full.steps) > 3, true)
' NOTHING WAS LOST ON THE WAY. `unresolved` is the field a caller has to be
' able to trust, so a clean chain must report an EMPTY one -- otherwise the
' list becomes noise and stops being read.
check("nothing on the chain was unresolved", count(full.unresolved), 0)

' A COLUMN FILLED FROM A CONSTANT has a writer and no sources, which is a
' DIFFERENT FACT from having no writer. Conflating them is how a hard-coded
' value becomes invisible in a lineage report -- and a hard-coded value is one
' of the commonest reasons a number is wrong and nobody can see why.
konst = discovery.lineage(lcat, lmods, "PostgreSQL", "wh.finance.gl_entry.acct_no", {})
check("a constant column has a writer", count(konst.steps), 1)
check("and no sources", count(konst.steps[0].sources), 0)
check("and is NOT reported as an origin", count(konst.origins), 0)
check("while the literal itself is shown", contains(konst.steps[0].expression, "4000"), true)

' TWO WRITERS OF ONE COLUMN, and the second reads the very table it writes.
' Both are ordinary and both break a walk that assumes one writer and no
' cycles; reporting only the first would silently hide a restatement.
two = discovery.lineage(lcat, lmods, "PostgreSQL", "wh.warehouse.fact_volume.avail_after_pvr", {})
vias = []
for each st in two.steps
    if st.hops = 0 then
        append(vias, st.via)
    end if
end for
check("both writers of one column are reported", count(vias), 2)
check("including the one that reads the table it writes",
      contains(join(vias, ","), "p_restate_fact"), true)

' THE CONTROL. A base-table column nothing writes is an ORIGIN with no steps
' -- without this, "the trail ends at trading.deal" is equally satisfied by a
' walker that stops everywhere.
base = discovery.lineage(lcat, lmods, "PostgreSQL", "wh.trading.deal.gross_vol_mmbtu", {})
check("CONTROL: a column nothing writes is an origin", join(base.origins, ","), "wh.trading.deal.gross_vol_mmbtu")
check("and has no derivation at all", count(base.steps), 0)

' AND THE NULL REGION. Nothing in `staging` is written by anything readable,
' so a lineage that reported a derivation there would have INVENTED it -- the
' same assertion R9 makes for relationships, one level down.
nul = discovery.lineage(lcat, lmods, "PostgreSQL", "wh.staging.tmp_rebate_2019.col_a", {})
check("nothing is derived in the null region", count(nul.steps), 0)
check("and it is reported as an origin, not as an answer", join(nul.origins, ","), "wh.staging.tmp_rebate_2019.col_a")

print ""
print "-- refusals"
on error goto next
estate.ddl(sp, "mariadb")
check("a dialect that cannot carry the estate is refused", contains(error.message, "no schemas"), true)
error.clear()
check("CONTROL: postgres is supported", count(estate.ddl(sp, "postgres")) > 0, true)
on error goto next
discovery.lineage(lcat, lmods, "PostgreSQL", "wh.warehouse.fact_volume.no_such_column", {})
check("lineage from a column that does not exist is refused", contains(error.message, "no column"), true)
error.clear()
on error stop

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
