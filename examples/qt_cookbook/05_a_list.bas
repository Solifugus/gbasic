' A LIST AND THE RECORD UNDER IT -- the commonest shape a business screen
' takes, and the one `table` was designed for.
'
' A `table` is NAMED where a form is not, because a table shows MANY rows and
' the table itself is what a handler and `qt.selected` address. Its subject is
' a LIST, and each `field` inside is a COLUMN read from every element -- the
' column's `name` says which field, its `label` says what the heading reads.
window orders( title: "Orders", width: 620, height: 360 )
    column( margin: 10, spacing: 8 )
        table lines to show rows
            field placed( label: "Date" )
            field who(    label: "Customer" )
            field total(  label: "Total" )
        end table

        group "The chosen order"
            form to show chosen
                field who(   label: "Customer" )
                field total( label: "Total" )
            end form
        end group
    end column

    on lines.itemSelectionChanged
        pick()
    end on
end window

function pick()
    at = qt.selected(orders, "lines")
    if is_unknown(at) then
        return nothing
    end if
    row = g.rows[at]
    ' MUTATE THE SUBJECT'S FIELDS, NEVER REBIND IT. A handler cannot rebind
    ' `chosen` -- gBASIC has no closures, so the assignment would make a
    ' handler-local and warning 2104 would say so -- but writing its FIELDS
    ' reaches the record `main` holds. `qt.refresh` then redraws the form from
    ' it, addressed by the form's SUBJECT because a form has no name.
    chosen.who = row.who
    chosen.total = row.total
    drew = qt.refresh(orders, "chosen")
    return nothing
end function

program main(args)
    g = { rows: [] }
    g.rows = [ { placed: "2026-03-01", who: "Ada Lovelace",   total: {USD}420.00 },
               { placed: "2026-03-04", who: "Alan Turing",    total: {USD}98.50 },
               { placed: "2026-03-09", who: "Grace Hopper",   total: {USD}1210.00 } ]


    if not qt.can_display() then
        print "no display"
        return
    end if
    ' THE SUBJECTS ARE READ AT OPEN, so both are in place before the window is.
    rows = g.rows
    chosen = g.rows[0]
    qt.open(orders)

    print "the table drew " + string(len(g.rows)) + " rows"
    ' A HEADING COMES FROM `label:` AND A CELL FROM `name:` -- two different
    ' sources, which is what makes the heading an independent check that the
    ' columns are mapped the way the declaration says.
    print "heading 1 is:      " + qt.heading(orders, "lines", 1)
    print "cell (1, who) is:  " + qt.cell(orders, "lines", 1, "who")

    ' CHOOSING A ROW FROM THE PROGRAM fires the same handler a click would.
    qt.select(orders, "lines", 2)
    print "chosen row:        " + string(qt.selected(orders, "lines"))
    print "the form now says: " + qt.read(orders, "who")
    print "               and " + qt.read(orders, "total")
    qt.stop()
end program
