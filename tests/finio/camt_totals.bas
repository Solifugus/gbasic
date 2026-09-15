' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' What finio makes of a camt statement's arithmetic, in the shape
' tests/run_finio_camt.sh compares against Python's ElementTree and against
' the document's own declared balances.
'
' RENDERED AT TWO PLACES ON PURPOSE. `money.text(m)` defaults to the STORAGE
' scale, which carries four guard digits below the minor unit, so an exactly
' correct total prints as 5550.100000 and reads as a disagreement with a
' Decimal that prints 5550.10. Two is right for the fixtures' USD and EUR and
' would be wrong for JPY or KWD -- this is a comparison harness for two known
' currencies, not a general renderer, and the day a fixture in a third currency
' arrives this line has to ask `money.currency` instead.
load finio
load finio_camt

program main( args )
    reg = finio.registry([ finio_camt.adapter() ])
    doc = finio.read_file(reg, args[0], {})
    i = 0
    for each st in doc.entities.statements
        t = finio_camt.entry_totals(doc.records, st)
        op = finio_camt.balance_of(doc.records, st, "OPBD")
        print ("S" + string(i) + " " + st.currency + " " + string(count(st.entries))
               + " " + money.text(t.credits, 2) + " " + money.text(t.debits, 2)
               + " " + money.text(op.amount + t.credits - t.debits, 2))
        i = i + 1
    end for
end program
