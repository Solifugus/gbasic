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
print "-- R9: nothing in the null region relates to anything"
for each r in tr.relationships
    check("no relationship touches " + tr.null_region,
          starts_with(r.from, tr.null_region + ".") or starts_with(r.to, tr.null_region + "."), false)
end for

print ""
print "-- refusals"
on error goto next
estate.ddl(sp, "mariadb")
check("a dialect that cannot carry the estate is refused", contains(error.message, "no schemas"), true)
error.clear()
check("CONTROL: postgres is supported", count(estate.ddl(sp, "postgres")) > 0, true)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
