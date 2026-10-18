' AN ORDER LIST AND THE ORDER UNDER IT -- a `table` feeding a `form`, which is
' the commonest shape a business screen takes and the one §7 designed `table`
' for.
'
' Run it:
'     ./gbasic examples/qt/orders.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/orders.bas   (no display)
'
' NOTHING HERE WIRES A WIDGET. The table takes its rows from `lines` and the
' form takes its fields from `chosen`, both by NAME -- a column called `total`
' reads `.total` from every element, and a field called `who` reads `chosen.who`.
' What the program does is answer "which row?" and refill the form.

window orders( title: "Orders", width: 560, height: 380 )
    column( margin: 10, spacing: 10 )

        table lines_view to show lines
            field placed( label: "Date" )
            field who(    label: "Customer" )
            field total(  label: "Total", type: {USD} )
        end table

        group "The order"
            form to show chosen
                field who(   label: "Customer" )
                field total( label: "Total", type: {USD} )
            end form
        end group

        row
            stretch()
            button done( text: "Close" )
        end row

    end column

    ' CHOOSING A ROW IS READING, so the table is `to show` and still
    ' selectable -- a display-only table is uneditable, not disabled.
    ' `qt.selected` answers an INDEX, which is what lets it index the very
    ' list the table was built from.
    on lines_view.itemSelectionChanged
        at = qt.selected(orders, "lines_view")
        if not is_unknown(at) then
            row = lines[at]
            x = qt.write(orders, "who", row.who)
            x = qt.write(orders, "total", row.total)
        end if
    end on

    on done.clicked
        qt.stop()
    end on
end window

program main(args)
    if not qt.can_display() then
        print "No display is available, so there is no window to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
    else
        lines = [ { placed: "2026-01-04", who: "Ada Lovelace",  total: {USD}"120.50" },
                  { placed: "2026-01-09", who: "Grace Hopper",  total: {USD}"88.00" },
                  { placed: "2026-02-02", who: "Alan Turing",   total: {USD}"1500.00" } ]
        ' THE FORM'S SUBJECT STARTS EMPTY, which is an ordinary state and not a
        ' special case: a field the record lacks shows nothing until something
        ' fills it in.
        chosen = {}

        qt.open(orders)
        ' A ROW CHOSEN FROM THE PROGRAM, so the window opens with something in
        ' it rather than waiting to be clicked.
        qt.select(orders, "lines_view", 0)

        if contains(args, "check") then
            print "rows:          " + string(len(lines))
            print "chosen row:    " + string(qt.selected(orders, "lines_view"))
            print "its customer:  " + qt.cell(orders, "lines_view", 0, "who")
            ' THE CAPTION COMES FROM `label:` WHERE THE DATA COMES FROM `name`,
            ' which is why the heading is an independent oracle: a column
            ' reading the wrong field still shows a plausible value under a
            ' heading that would have to be wrong too.
            print "its heading:   " + qt.heading(orders, "lines_view", "who")
            print "by position:   " + qt.heading(orders, "lines_view", 1)
            qt.select(orders, "lines_view", 2)
            print "after select:  " + string(qt.selected(orders, "lines_view"))
            print "that customer: " + qt.cell(orders, "lines_view", 2, "who")
            qt.stop()
            return
        end if
        qt.run()
    end if
end program
