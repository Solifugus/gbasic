' A WINDOW BUILT FROM DATA -- one row per invoice line, which is the one thing
' a `window` block cannot write, a declaration having no loop in it (§4 of
' docs/qt_gui_design.md).
'
' Run it:
'     ./gbasic examples/qt/invoice.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/invoice.bas   (no display)
'
' THERE IS NO SECOND API. The `window` block binds a RECORD, and `qt.open` asks
' only that a record carry `kind: "window"` -- so building that record is the
' other door, exactly as `server` and `web.routes` are two doors on one thing.
' `print invoice_window(lines)` shows you what a declaration would have bound.
'
' AND IT MEETS THE SAME RULES, through `qt.check`: the closed vocabulary with
' its near-miss hint, the placement rules, names unique within the window, a
' handler whose target exists, and the record's own field names. Which matters
' most where it is least visible -- a part built with no `name` would open
' perfectly and be unaddressable forever.
' THE DECLARED PART STAYS DECLARED. A heading and a total are fixed furniture,
' so they are written out; only the LINES are generated. That is the division
' §4 drew, and this is what it looks like in one screen.
function invoice_window(lines, total)
    rows = []
    for each line in lines
        rows = append(rows, { kind: "container", word: "row", options: {},
            items: [
                { kind: "part", word: "field", name: "d_" + line.code,
                  options: { label: line.what, text: line.what,
                             readOnly: true } },
                { kind: "part", word: "field", name: "q_" + line.code,
                  options: { text: string(line.qty), readOnly: true } },
                { kind: "part", word: "field", name: "a_" + line.code,
                  options: { text: money.text(line.amount, 2),
                             readOnly: true } }
            ] })
    end for
    return { kind: "window", name: "invoice",
        options: { title: "Invoice 4471", width: 480, height: 340 },
        items: [
            { kind: "container", word: "column",
              options: { margin: 10, spacing: 8 },
              items: [
                ' `cells` so the three columns LINE UP down the page, which is
                ' the whole reason a grid exists and the half a box layout
                ' cannot do.
                { kind: "container", word: "cells", options: {}, items: rows },
                { kind: "container", word: "row", options: {}, items: [
                    { kind: "part", word: "stretch", options: {} },
                    { kind: "part", word: "field", name: "sum",
                      options: { label: "Total", text: money.text(total, 2),
                                 readOnly: true } }
                ] },
                { kind: "container", word: "row", options: {}, items: [
                    { kind: "part", word: "stretch", options: {} },
                    { kind: "part", word: "button", name: "close",
                      options: { label: "Close", text: "Close" } }
                ] }
              ] },
            { kind: "handler", target: "close", signal: "clicked",
              handler: on_close }
        ] }
end function
function on_close()
    qt.stop()
end function
program main(args)
    lines = [ { code: "a1", what: "Consulting, March", qty: 12,
                amount: {USD}"1800.00" },
              { code: "b2", what: "Travel",            qty: 1,
                amount: {USD}"412.55" },
              { code: "c3", what: "Licences",          qty: 3,
                amount: {USD}"297.00" } ]
    total = {USD}"0.00"
    for each line in lines
        total = total + line.amount
    end for
    win = invoice_window(lines, total)
    ' CHECKED BEFORE ANYTHING IS OPENED, which is what a build-time mistake
    ' wants: `qt.check` needs no display and no Qt at all, so this runs on a
    ' text-only machine and in a test. `qt.open` runs it too -- a validator
    ' nobody is obliged to call is necessary and not sufficient.
    if not qt.can_display() then
        print "no display, so the window is not opened -- but it still checks:"
        print "  qt.check: " + string(qt.check(win))
        print("  " + string(len(lines)) + " generated lines, total " +
              money.text(total, 2))
        return
    end if
    print "qt.check: " + string(qt.check(win))
    qt.open(win)
    ' READ BACK BY NAME, generated or not: nothing distinguishes a built part
    ' from a declared one.
    print "the second line reads: " + qt.read(win, "a_b2", "text")
    ' AND PLACED BY THE LAYOUT, which is the half a property read cannot see.
    print "it sits at " + string(qt.at(win, "a_b2"))
    if contains(args, "check") then
        print "generated lines: " + string(len(lines))
        print "the total shows: " + qt.read(win, "sum")
        qt.stop()
        return
    end if
    qt.run()
    print "closed"
end program
