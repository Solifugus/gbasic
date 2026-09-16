' Recipe 6 — One call, five formats, four structural shapes.
'
' This is the recipe the framework exists for. A fixed-width ACH file, a
' delimited BAI2 statement, two XML messages and a tagged OFX download have
' nothing in common at the byte level: different framings, different record
' models, different ways of saying an amount. Above `finio` they are the same
' shape — a document with records, entities and a loss report.
'
' Nothing below names a format. The registry recognises each file from its own
' evidence.
'
' The one place the difference survives, deliberately, is `byte_fidelity`.
' NACHA and BAI2 can be reproduced byte for byte from what was read; camt.053
' cannot, because an XML document carries whitespace and attribute order that a
' reader does not model. Saying so beats claiming a guarantee that is false.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()

    files = [ "tests/finio/nacha/payroll.ach",
              "tests/finio/bai2/statement.bai",
              "tests/finio/camt/statement.xml",
              "tests/finio/ofx/statement_v1.ofx",
              "tests/finio/pain/payments.xml" ]

    for each path in files
        doc = finio.read_file(reg, path, {})
        kinds = []
        for each r in doc.records
            if not contains(kinds, r.kind) then
                append(kinds, r.kind)
            end if
        end for
        print (file_name(path))
        print ("  adapter        " + doc.adapter + " (" + doc.classification + ")")
        print ("  revision       " + string(doc.revision))
        print ("  records        " + string(count(doc.records)))
        print ("  record kinds   " + join(kinds, ", "))
        print ("  entities       " + join(keys(doc.entities), ", "))
        print ("  byte fidelity  " + string(doc.byte_fidelity))
        print ""
    end for
end program
