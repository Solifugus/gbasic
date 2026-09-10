' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' The estate materialised, and `discovery` traced across it. Needs a real
' PostgreSQL or SQL Server -- SQLite has no stored procedures and MariaDB has
' no schemas, so neither can carry this estate.

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

c = odbc.connect(env("GBASIC_ODBC_CONNECTION"))
sp = estate.spec()
on error goto next
' tear down, deepest first
for each nm in ["warehouse.rpt_volume_net", "warehouse.rpt_volume_gross"]
    x = odbc.exec(c, "drop view " + nm)
    if error then
        error.clear()
    end if
end for
for each m in sp.modules
    if m.kind = "procedure" then
        if env("DIALECT") = "postgres" then
            x = odbc.exec(c, "drop function if exists " + m.schema + "." + m.name + "()")
        else
            x = odbc.exec(c, "drop procedure if exists " + m.schema + "." + m.name)
        end if
        if error then
            error.clear()
        end if
    end if
end for
' REVERSE declaration order: a referenced table cannot be dropped while its
' referrer still exists.
i = count(sp.tables) - 1
while i >= 0
    tb = sp.tables[i]
    x = odbc.exec(c, "drop table if exists " + tb.schema + "." + tb.name)
    if error then
        error.clear()
    end if
    i = i - 1
end while
built = 0
failed = 0
for each stmt in estate.ddl(sp, env("DIALECT"))
    x = odbc.exec(c, stmt)
    if error then
        failed = failed + 1
        print "  FAILED: " + left(stmt, 70)
        print "     " + left(error.message, 110)
        error.clear()
    else
        built = built + 1
    end if
end for
print "built " + string(built) + " statements, " + string(failed) + " failed"

sp = estate.spec()
tr_truth = estate.truth(sp)
info = odbc.info(c)
cat = discovery.scan(c, { source: "est" })
mods = discovery.modules(c, { source: "est" })
tr = discovery.trace(cat, mods, info.dbms_name)

print "scanned " + string(count(keys(cat.tables))) + " tables, " + string(count(mods)) + " modules, " + string(count(tr.edges)) + " edges"

' --- does every DECLARED flow appear in the trace?
print ""
print "-- declared flows recovered"
missing = 0
for each m in tr_truth.flows
    if not has(m, "dialects") or contains(m.dialects, env("DIALECT")) then
        for each side in ["reads", "writes"]
            for each obj in m[side]
                found = false
                for each e in tr.edges
                    if ends_with(e.module, "." + m.name) and e.kind = side and ends_with(e.object, "." + split(obj, ".")[1]) then
                        found = true
                    end if
                end for
                if not found then
                    print "  MISSING " + m.name + " " + side + " " + obj
                    missing = missing + 1
                end if
            end for
        end for
    end if
end for
check("every declared flow is recovered", missing, 0)

' --- R9: THE NULL REGION. Nothing may be reported as flowing here.
print ""
print "-- R9: the null region"
leaked = 0
for each e in tr.edges
    if contains(e.object, "." + tr_truth.null_region + ".") then
        print "  INVENTED " + e.module + " " + e.kind + " " + e.object
        leaked = leaked + 1
    end if
end for
' R9: THE LOAD-BEARING LIVE CHECK. Lineage reported into the null region
' is INVENTED, and no other tier can reveal that.
check("nothing is invented in the null region", leaked, 0)
' The dynamic-SQL module must be a GAP, not silently dropped: a tracer that
' loses a hop it could not read is confidently incomplete.
if env("DIALECT") = "sqlserver" then
    check("the unreadable hop is reported as a gap", count(tr.gaps) > 0, true)
end if
for each u in tr.unresolved
    print "  unresolved: " + u.module + " -> " + u.reference
end for

' --- impact: the headline use
print ""
print "-- impact of changing trading.deal"
' IMPACT IS THE HEADLINE USE: what does changing this affect downstream? The
' estate's chain is four hops across three schemas and fans out at the end, so
' asserting the FAR end is what proves the walk followed procedures AND views
' rather than stopping at the first.
reached = []
for each tid in keys(cat.tables)
    if ends_with(tid, ".trading.deal") then
        for each h in discovery.impact(tr, tid, "downstream")
            append(reached, h.object)
        end for
    end if
end for
function reaches(list, suffix)
    for each r in list
        if ends_with(r, suffix) then
            return true
        end if
    end for
    return false
end function
check("impact reaches the staging table one hop out", reaches(reached, ".warehouse.stg_deal"), true)
check("and the fact table two hops out", reaches(reached, ".warehouse.fact_volume"), true)
check("and the LEDGER three hops out, in another schema", reaches(reached, ".finance.gl_entry"), true)
check("and both divergent reports", reaches(reached, ".rpt_volume_gross") and reaches(reached, ".rpt_volume_net"), true)
' THE CONTROL: the null region is downstream of nothing.
check("CONTROL: the null region is not reachable", reaches(reached, ".staging.tmp_rebate_2019"), false)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
z = odbc.close(c)
