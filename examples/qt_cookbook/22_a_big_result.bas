' A BIG RESULT -- a table that ASKS for its cells instead of being shown them,
' which is how you put a million rows on screen.
'
' `table x to show <list>` HOLDS EVERY ROW. That is right for a form's lines or
' a file list, and wrong for a query that came back with a hundred thousand
' rows: building a widget per cell costs memory and time proportional to the
' data rather than to the window.
'
' `table x to ask <function>` KEEPS NONE OF IT. The subject is a FUNCTION NAME,
' not a list -- it is called with a row and a column and answers that one cell,
' as it becomes visible. Tell the table how many rows there are with `qt.rows`,
' and tell it again whenever the data changes.
'
' THE COST IS THE VIEWPORT, NOT THE TABLE. Measured: Qt asks for about 27 cells
' whether the table has a hundred rows or a million, and scrolling to the end
' asks for a few dozen more rather than walking everything in between. This
' recipe prints its own callback's call count, so you can see it.
'
' WHATEVER THE CALLBACK RETURNS IS RENDERED BY THE SAME RENDERER `print` USES,
' so a number, a date or money arrives in the words the language itself uses --
' there is no formatting to do and nothing to convert.

window app( title: "Results", width: 560, height: 300 )
    column( margin: 8, spacing: 6 )
        text hint( label: "A table that asks for what it needs to draw." )
        table results to ask cell_of
            field num(   label: "#" )
            field who(   label: "Customer" )
            field total( label: "Total" )
            field when(  label: "Placed" )
        end table
    end column
end window

' THE CELL. Called with a row and a column, both counted from 0. In a real
' program this reads from wherever the rows actually live -- a database cursor,
' a file, a frame -- and here it computes them so the recipe needs nothing.
function cell_of(row, column)
    g.asked = g.asked + 1
    if column = 0 then
        return row + 1
    end if
    if column = 1 then
        return "customer " + string(row)
    end if
    if column = 2 then
        ' MONEY, which arrives formatted because the one renderer does it.
        return {USD}string(12 + row)
    end if
    return {date}"2026-01-01"
end function

program main(args)
    if not qt.can_display() then
        print "no display, so no window -- the declaration is still checked"
        return
    end if

    g = { asked: 0, ticks: 0 }
    qt.open(app)

    ' HOW MANY ROWS THERE ARE. This is a verb and not part of the declaration
    ' because it changes: a results grid is refilled by every query.
    qt.rows(app, "results", 1000000)
    print "rows:          " + string(qt.rows(app, "results"))

    ' READ ANY CELL, which asks the callback for exactly that one.
    print "row 0:         " + string(qt.cell(app, "results", 0, "who"))
    print "row 999999:    " + string(qt.cell(app, "results", 999999, "who"))
    print "money reads:   " + string(qt.cell(app, "results", 0, "total"))
    print "a date reads:  " + string(qt.cell(app, "results", 0, "when"))

    ' AND NOW LET QT DRAW IT, so the count below is what painting a
    ' million-row table actually costs. The timer hands the thread back; see
    ' the event-loop recipe for why that is how a `check` run ends.
    g.asked = 0
    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.ticks = g.ticks + 1
            if g.ticks >= 2 then
                qt.stop()
            end if
        end while
    end watch
    qt.run()
    cancelled = timer.cancel(beat)

    print "cells asked for to draw a MILLION rows: " + string(g.asked)
    print "which is fewer than a thousand:         " + string(g.asked < 1000)

    qt.close(app)
end program
