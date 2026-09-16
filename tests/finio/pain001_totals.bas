' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' What finio makes of a pain.001's two levels of control total, in the shape
' tests/run_finio_pain001.sh compares against an independent Python reading and
' against the document's own declared sums.
load finio
load finio_pain001

program main( args )
    reg = finio.registry([ finio_pain001.adapter() ])
    doc = finio.read_file(reg, args[0], {})
    total = unknown
    ntx = 0
    i = 0
    for each b in doc.entities.blocks
        t = finio_pain001.block_total(doc.records, b)
        print "B" + string(i) + " " + money.text(t.total, 2) + " " + string(count(b.transactions))
        ntx = ntx + count(b.transactions)
        if is_unknown(total) then
            total = t.total
        else
            total = total + t.total
        end if
        i = i + 1
    end for
    print "M" + money.text(total, 2) + " " + string(ntx)
end program
