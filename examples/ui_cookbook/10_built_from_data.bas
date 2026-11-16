' A WINDOW BUILT FROM DATA -- the one thing a `window` block cannot write,
' because a declaration has no loop in it.
'
' THERE IS NO SECOND API. The block binds a RECORD, and `ui.open` asks only
' that a record carry `kind: "window"` -- so building that record is the other
' door, and everything else (binding, handlers, properties, `ui.at`) works
' exactly as it does for a declared window.
'
' `ui.check` GIVES A BUILT WINDOW THE DECLARATION'S OWN LOAD-TIME RULES, which
' is what makes building one safe: a part with no name, two parts sharing one,
' a misspelled word or a handler naming nothing are all refused with the same
' sentences, and `ui.open` runs the check itself so a validator nobody called
' cannot be skipped.

function row_for(line)
    ' ONE ROW OF A GRID, as a record. `cells` aligns columns across rows, which
    ' is what makes a generated table of fields line up.
    return { kind: "container", word: "row", options: {},
             items: [ { kind: "part", word: "text", name: "d" + line.id,
                        options: { label: line.what } },
                      { kind: "part", word: "field", name: "q" + line.id,
                        options: { text: string(line.qty) } } ] }
end function

function invoice_for(lines)
    rows = []
    for each l in lines
        rows = append(rows, row_for(l))
    end for
    return { kind: "window", name: "invoice",
             options: { title: "Invoice", width: 420, height: 320 },
             items: [ { kind: "container", word: "cells", options: {},
                        items: rows } ] }
end function

program main(args)
    lines = [ { id: 1, what: "Consulting", qty: 3 },
              { id: 2, what: "Travel",     qty: 1 },
              { id: 3, what: "Licence",    qty: 12 } ]

    invoice = invoice_for(lines)

    ' THE RULES APPLY BEFORE ANYTHING IS BUILT, and need no display and no Qt.
    print "the built record passes ui.check: " + string(ui.check(invoice))

    if not ui.can_display() then
        print "no display"
        return
    end if
    ui.open(invoice)

    print "it generated " + string(len(lines)) + " rows"
    print "row 0 shows:  " + invoice.read("d1")
    print "row 2 holds:  " + invoice.read("q3")
    ' AND THE GEOMETRY IS REAL, asked of the layout rather than of any record
    ' of where we put things.
    at = invoice.at("q3")
    print "q3 sits at row " + string(at.row) + ", column " + string(at.column)
    ui.stop()

    ' A WINDOW THE RULES REJECT IS REFUSED, in the declaration's own words.
    on error goto refused
    bad = { kind: "window", name: "bad", options: {},
            items: [ { kind: "container", word: "column", options: {},
                       items: [ { kind: "part", word: "field", options: {} } ] } ] }
    x = ui.check(bad)
    print "MISMATCH: a part with no name was accepted"
    return
refused:
    print "a part with no name is refused: " + error.message
    error.clear()
end program
