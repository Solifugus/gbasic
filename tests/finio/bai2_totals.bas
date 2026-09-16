' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' What finio makes of a BAI2 file's three-level arithmetic, in the shape
' tests/run_finio_bai2.sh compares against an independent Python reading.
load finio
load finio_bai2

program main( args )
    reg = finio.registry([ finio_bai2.adapter() ])
    doc = finio.read_file(reg, args[0], {})
    file_total = 0
    for each g in doc.entities.groups
        gtot = 0
        for each a in g.accounts
            t = finio_bai2.account_total(doc.records, a).total
            gtot = gtot + t
            print "account " + string(t)
        end for
        print "group " + string(gtot) + " accounts " + string(count(g.accounts))
        file_total = file_total + gtot
    end for
    print "file " + string(file_total) + " groups " + string(count(doc.entities.groups))
end program
