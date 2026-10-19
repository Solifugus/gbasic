' A TABLE THAT ASKS FOR ITS CELLS (§0.25) -- `table x to ask fn`, `qt.rows`,
' and a model behind a QTableView. Under QT_QPA_PLATFORM=offscreen.
'
' WHY IT EXISTS. `table x to show <list>` materialises every row into a
' QTableWidget, so §0.6 deferred the large-result path to a MODEL -- and gBASIC
' Studio is the consumer rather than a hypothesis: its results grid switches to
' `datagrid.create_virtual(count_fn, cell_fn)` above 500 rows, and `datagrid`
' is GTK-only, so this is the one thing its Qt conversion could not express.
'
' ONE MEASUREMENT DECIDED WHETHER IT IS POSSIBLE AT ALL. A model's `data()` has
' to call back into the interpreter, and per cell of the whole table that would
' be unusable. Measured in C++ first: 27 calls for 100 rows, 27 for a MILLION,
' 87 for a taller viewport, and `scrollToBottom` on a million cost 54 in total
' -- it jumps straight to the last row rather than touching anything on the
' way. THE COST IS THE VIEWPORT, NOT THE TABLE.
'
' AND THAT CLAIM IS ASSERTED HERE, IN THE LANGUAGE, which took one more idea:
' nothing paints offscreen without a real turn of Qt's loop, so `repaint`
' answered 0 -- the fixture hands the thread to `qt.run()` and takes it back
' with a timer, which is cookbook recipe 13's pattern. With that, a MILLION-row
' table asks for about three dozen cells.
'
' THE ORACLE IS FREE THIS TIME, which is unusual here: the callback is gBASIC
' code, so it COUNTS ITS OWN CALLS. Everything the headline claim needs is a
' number the fixture already has.
'
' SELF-CHECKING NOT GOLDEN AND FORCED: every defect produces a table full of
' plausible text. A column mapped wrongly shows the right values under the
' wrong headings; a row offset by one shows real data about the wrong record;
' and a model that asked for every cell would be correct and unusable.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window app( title: "Results", width: 520, height: 300 )
    column
        table results to ask cell_of
            field who(   label: "Customer" )
            field total( label: "Total" )
            field when(  label: "Placed" )
        end table
    end column
end window

' A HELD TABLE IN A SECOND WINDOW -- the control. Without it, "an asking table
' works" is satisfied by a change that broke the ordinary one, and the two
' build different widgets from the same word.
window held( title: "Held", width: 400, height: 240 )
    column
        table lines to show rows
            field name( label: "Name" )
        end table
    end column
end window

' THE CALLBACK. It encodes its own arguments into the answer, which is what
' makes a row or column offset visible rather than plausible -- and it counts
' its own calls, which is the whole oracle for the cost claim.
function cell_of(row, column)
    g.asked = g.asked + 1
    g.last_row = row
    g.last_col = column
    if column = 0 then
        return "customer " + string(row)
    end if
    if column = 1 then
        ' A NUMBER, not text: the kind must survive the crossing and be
        ' rendered by the ONE renderer rather than by this module's own
        ' opinion -- which is the rule `qt.write` already follows.
        return row * 2
    end if
    return {date}"2026-03-04"
end function


program main()
    rows = [{ name: "a" }, { name: "b" }]
    g = { asked: 0, last_row: -1, last_col: -1, ticks: 0, stopping: false }

    ' THE ONE TIMER AND THE ONE WATCHER, at the top of `main` where a `watch`
    ' is allowed. The watcher hands the thread back after two ticks whenever
    ' `spin` has asked it to, and the timer is cancelled at the end -- a
    ' repeating one left live keeps gBASIC's own post-main loop running and the
    ' program never exits, which is the trap the event-loop recipe records.
    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.ticks = g.ticks + 1
            if g.stopping and g.ticks >= 2 then
                qt.stop()
            end if
        end while
    end watch

    qt.open(app)

    ' ---- the row count is a VERB, because it changes --------------------
    check("a new asking table has no rows yet", qt.rows(app, "results"), 0)
    qt.rows(app, "results", 100)
    check("and it takes a count", qt.rows(app, "results"), 100)
    qt.rows(app, "results", 1000000)
    check("which can be set again", qt.rows(app, "results"), 1000000)

    ' ---- THE COST IS THE VIEWPORT, NOT THE TABLE -----------------------
    ' THE LOAD-BEARING TIER. Asserted as a DIFFERENCE between two row counts
    ' four orders of magnitude apart: a value would be a fact about this
    ' viewport, where what matters is that the number does NOT follow the
    ' table's size. A model that asked for every cell would be correct and
    ' unusable, and nothing else here would notice.
    ' TWO TURNS OF QT'S LOOP, AND BOTH ARE ENTERED FROM THE TOP LEVEL of the
    ' program block -- which is not a style choice. A `watch` body fires only
    ' while the GLOBAL frame is current (`watcher_trigger_change` returns at
    ' once otherwise), so `qt.run()` called from inside a FUNCTION leaves that
    ' function's frame current for the whole of Qt's loop and every tick is
    ' appended to `timer.ticks` and never delivered. The timer's own schedule
    ' advances, so it looks like it is working, and the program HANGS. Measured
    ' while writing this fixture, which first wrapped these turns in a `spin()`
    ' helper and hung with no output at all.
    ' THE COUNTER IS RESET BEFORE `qt.rows`, NOT AFTER, and that boundary is
    ' the assertion: what is being measured is the WHOLE cost of showing N
    ' rows -- telling the table how many there are AND painting it. Reset
    ' afterwards, a model that did expensive work while being told the count
    ' would be invisible, and a perturbation that prefetched five hundred rows
    ' on every `qt.rows` passed every check in this file.
    g.asked = 0
    g.ticks = 0
    g.stopping = true
    qt.rows(app, "results", 100)
    qt.run()
    small = g.asked

    g.asked = 0
    g.ticks = 0
    qt.rows(app, "results", 1000000)
    qt.run()
    big = g.asked

    check("a hundred rows asks for some cells", small > 0, true)
    check("and a million asks for some cells", big > 0, true)
    ' THE CLAIM: four orders of magnitude more rows, within a factor of two.
    check("a million rows costs no more than a hundred",
          big < small * 2 + 10, true)
    ' AND NEITHER IS ANYWHERE NEAR THE TABLE'S SIZE, which is the same claim
    ' from the other side and is what fails if the model ever walks the rows.
    check("and far fewer than the rows themselves", big < 1000, true)

    ' ---- the callback is asked for real coordinates ---------------------
    ' A CELL ENCODES ITS OWN ROW, so an offset shows real data about the wrong
    ' record rather than something obviously broken.
    check("a cell knows its row", qt.cell(app, "results", 7, "who"),
          "customer 7")
    check("and a different row differs", qt.cell(app, "results", 8, "who"),
          "customer 8")
    check("the callback saw that row", g.last_row, 8)

    ' ---- the value's KIND survives the crossing ------------------------
    ' Rendered through the ONE renderer, so a number and a date arrive in the
    ' words the language itself uses.
    check("a number cell", qt.cell(app, "results", 5, "total"), "10")
    check("a date cell", qt.cell(app, "results", 5, "when"), "2026-03-04")

    ' ---- THE COLUMN ORACLE ---------------------------------------------
    ' A cell's data comes from the CALLBACK and its heading from the
    ' DECLARATION, so a column mapped wrongly shows the right values under the
    ' wrong headings -- which only the captions can say. By POSITION, because
    ' by name shares the resolution being tested.
    check("column 0 is the customer", qt.heading(app, "results", 0), "Customer")
    check("column 1 is the total", qt.heading(app, "results", 1), "Total")
    check("column 2 is the date", qt.heading(app, "results", 2), "Placed")
    ' AND THE CALLBACK IS ASKED FOR THE MATCHING COLUMN, which is the half a
    ' caption check cannot see.
    x = qt.cell(app, "results", 1, "when")
    check("asking for the third column asks the callback for 2", g.last_col, 2)

    ' ---- a row can still be chosen -------------------------------------
    qt.select(app, "results", 3)
    check("a row can be chosen", qt.selected(app, "results"), 3)

    ' ---- refusals, each beside its nearest legal neighbour -------------
    on error goto next
    qt.rows(app, "results", 0 - 5)
    check("a negative row count is refused", error.message,
          "qt.rows: a table cannot have fewer than no rows")
    error.clear()

    qt.rows(app, "nope")
    check("an unknown part is refused", error.message,
          "qt.rows: `nope` is not a table that asks for its rows")
    error.clear()
    on error stop

    qt.close(app)

    ' ---- THE CONTROL: a held table still works -------------------------
    ' The same word builds a different widget, so without this "an asking
    ' table works" is satisfied by a change that broke the ordinary one.
    qt.open(held)
    check("a held table still shows its list", qt.cell(held, "lines", 1, "name"),
          "b")
    check("and still reports its heading", qt.heading(held, "lines", 0), "Name")
    on error goto next
    qt.rows(held, "lines")
    check("and a held table is not an asking one", error.message,
          "qt.rows: `lines` is not a table that asks for its rows")
    error.clear()
    on error stop
    qt.close(held)

    cancelled = timer.cancel(beat)
    check("the timer was cancelled, so the program can end", cancelled, true)
end program
