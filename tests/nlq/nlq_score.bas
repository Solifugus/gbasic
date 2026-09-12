' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Scores `nlq.ground` against estateforge's answer key.
'
' THE ORACLE IS NOT OURS: `estateforge.questions_for(demo_plan())` records, for
' every question, the `touches[]` it actually needs -- computed from the plan,
' not by running any SQL and not by asking this library anything. So retrieval
' is scorable with NO MODEL AND NO DATABASE, which is the whole argument for
' doing this part first.
'
' BOTH HALVES ARE REPORTED AND BOTH ARE NEEDED. Recall alone is maximised by
' selecting every table, which is exactly the failure retrieval exists to
' prevent; precision alone is maximised by selecting none. A benchmark that
' reports one of them is measuring the wrong thing.

load nlq

program main( args )
    f {file}= args[0]
    cat = decode(read(f))
    catalog = { tables: cat.tables, columns: cat.columns }

    ' DECLARED BY THE ESTATE'S OWNER, not derived. Every entry here was named
    ' by a previous run's `unresolved` field, which is how the loop closes
    ' without anything being invented.
    syns = {}
    if count(args) > 1 and args[1] = "synonyms" then
        syns = { report: [ "rpt" ], general: [ "gl" ], ledger: [ "gl" ],
                 staged: [ "stg" ], net: [ "net" ], counterparty: [ "ctp" ] }
    end if

    total = 0
    recalled = 0
    selected_total = 0
    needed_total = 0
    per_cap = {}
    for each q in cat.questions
        g = nlq.ground(catalog, q.text, { limit: 8, synonyms: syns })
        total = total + 1
        hit = true
        for each t in q.touches
            if not contains(g.tables, t) then
                hit = false
            end if
        end for
        if hit then
            recalled = recalled + 1
        end if
        selected_total = selected_total + count(g.tables)
        needed_total = needed_total + count(q.touches)
        cap = q.exercises
        if is_unknown(per_cap[cap]) then
            per_cap[cap] = { n: 0, ok: 0 }
        end if
        c = per_cap[cap]
        c.n = c.n + 1
        if hit then
            c.ok = c.ok + 1
        end if
        per_cap[cap] = c
        mark = "MISS"
        if hit then
            mark = "hit "
        end if
        print (mark + " " + q.id + " [" + q.exercises + "] needs " + join(q.touches, "+") + " | got " + join(g.tables, ", "))
        if not hit then
            print ("      near: " + join(g.near_misses, ", ") + " | unresolved: " + join(g.unresolved, ", "))
        end if
    end for

    print ""
    print ("RECALL " + string(recalled) + "/" + string(total))
    print ("SELECTED " + string(selected_total) + " tables for " + string(needed_total) + " needed")
    for each cap in sort(keys(per_cap))
        print ("CAP " + cap + " " + string(per_cap[cap].ok) + "/" + string(per_cap[cap].n))
    end for
end program
