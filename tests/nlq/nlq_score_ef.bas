' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Score grounding against an estateforge fixture in ITS OWN EXPORT SHAPE
' (tools/export_fixture.sh), rather than a shape this repository hand-rolled.
' The exporter is gated on their side -- every section present, every view
' carrying columns, every `touches` resolving to a real object -- which is the
' defence against the failure that produced a 0/4 here once: a consumer reads
' the fixture instead of the estate, so what the fixture omits is invisible.

program main( args )
    load nlq
    f {file}= args[0]
    c = decode(read(f))
    cat = { tables: c.objects, columns: c.columns }

    ' Their `values` arrive GROUPED -- one record per column carrying its list,
    ' with the distinct and row counts that justified keeping it. Flattened
    ' here because `nlq.vocabulary` folds rows; the grouped form is strictly
    ' more informative and is the better input shape to take directly.
    vrows = []
    for each v in c.values
        for each one in v.values
            append(vrows, { schema: v.schema, table: v.table, column: v.column, value: one })
        end for
    end for
    vocab = nlq.vocabulary(vrows, {})
    ex = nlq.exemplars(vrows, {})

    syns = { report: [ "rpt" ], general: [ "gl" ], ledger: [ "gl" ],
             staged: [ "stg" ], counterparty: [ "ctp" ] }
    lim = 8
    if count(args) > 1 then
        lim = number(args[1])
    end if

    n = 0
    recalled = 0
    selected = 0
    needed = 0
    alts = 0
    per = {}
    for each q in c.questions
        g = nlq.ground(cat, q.text, { limit: lim, synonyms: syns,
                                      derived_from: c.derivation })
        n = n + 1
        hit = true
        for each t in q.touches
            if not contains(g.tables, t) then
                hit = false
            end if
        end for
        if hit then
            recalled = recalled + 1
        end if
        selected = selected + count(g.tables)
        needed = needed + count(q.touches)
        alts = alts + count(g.alternatives)
        cap = q.exercises
        if is_unknown(per[cap]) then
            per[cap] = { n: 0, ok: 0 }
        end if
        r = per[cap]
        r.n = r.n + 1
        if hit then
            r.ok = r.ok + 1
        end if
        per[cap] = r
        if not hit then
            print ("MISS " + q.id + " [" + q.exercises + "] needs " + join(q.touches, "+"))
            print ("     got " + join(g.tables, ", "))
        end if
    end for
    print ("RECALL " + string(recalled) + "/" + string(n) + " at limit " + string(lim))
    print ("SELECTED " + string(selected) + " for " + string(needed) + " needed, of " + string(count(c.objects)) + " objects")
    print ("ALTERNATIVES disclosed on " + string(alts) + " groundings")
    for each cap in sort(keys(per))
        print ("CAP " + cap + " " + string(per[cap].ok) + "/" + string(per[cap].n))
    end for
end program
