' `qt.refresh(win, table)` -- SHOW THE LIST AS IT IS NOW. Under
' QT_QPA_PLATFORM=offscreen: real Qt, no display.
'
' MEASURED 2026-10-18 WHILE BUILDING A WHOLE APPLICATION on this surface, and
' it was a GAP rather than a limit: a `table to show` captured its subject when
' the window opened and NEVER FOLLOWED IT, so appending to the list left
' `rowCount` where it was and a program had NO WAY to say otherwise. For a game
' that is awkward; for anything list-driven -- orders arriving, a search result,
' a filtered view, a file tree -- it is fatal.
'
' IT IS THE SYMMETRIC HALF OF `qt.subject`, which is what settled the design:
' §2.2's watcher pair could not do write-back, so a record is read from the
' widgets AT THE MOMENT ASKED. This is the same principle the other way round,
' and nothing is tracked in between, so no change can be missed.
'
' SELF-CHECKING AND FORCED: every defect here is a table that still looks like
' a table. A refresh that only ever grew would leave stale rows below the new
' ones; one that re-read the wrong variable would show a plausible list of
' something else; one that forgot the captured record would fold a refreshed
' table back into the OLD list. None of them raises.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

function refused(win, table)
    on error goto handle
    n = qt.refresh(win, table)
    return "ACCEPTED " + string(n)
handle:
    m = error.message
    error.clear()
    return m
end function

window board( title: "Board", width: 420, height: 320 )
    column( margin: 8 )
        table queue to show jobs
            field who(   label: "Who" )
            field state( label: "State" )
        end table
        ' AN `edit` TABLE TOO, so the capture half is exercised: a refreshed
        ' `edit` table must fold back into the NEW list, not the one captured
        ' when the window opened.
        table sheet to edit rows
            field a( label: "A" )
        end table
    end column
end window

' A SECOND WINDOW with no table at all, OPENED, so "declares no table" is
' what it answers. And a THIRD never opened, because "no such table" and "not
' open" are different problems with different fixes and a fixture that gets
' both from one window is testing one of them twice -- which is exactly what
' the first draft of this file did.
window plain( title: "Plain" )
    column
        field only( label: "Only" )
    end column
end window

window shut( title: "Never opened" )
    column
        field unused( label: "Unused" )
    end column
end window

program main()
    jobs = [ { who: "ada",   state: "waiting" },
             { who: "grace", state: "waiting" } ]
    rows = [ { a: "one" } ]

    if not qt.can_display() then
        print "SKIP no display"
        return
    end if
    qt.open(board)
    qt.open(plain)

    print "-- what the table showed at open"
    check("two rows at open", qt.read(board, "queue", "rowCount"), 2)
    check("and the first is ada", qt.cell(board, "queue", 0, "who"), "ada")

    print "-- THE DEFECT: the list changes and the table does not"
    jobs = append(jobs, { who: "alan", state: "waiting" })
    check("appending to the subject changes NOTHING on its own",
          qt.read(board, "queue", "rowCount"), 2)

    print "-- and what qt.refresh does about it"
    n = qt.refresh(board, "queue")
    check("it answers how many rows it showed", n, 3)
    check("the table grew", qt.read(board, "queue", "rowCount"), 3)
    check("the new row is there", qt.cell(board, "queue", 2, "who"), "alan")
    ' AND THE OLD ROWS ARE STILL RIGHT, which a refresh that appended rather
    ' than rewrote would also satisfy -- so the shrink below is what separates
    ' them.
    check("the old rows are unchanged", qt.cell(board, "queue", 0, "who"), "ada")

    print "-- IT SHRINKS, which is the half an append-only refresh fails"
    jobs = [ { who: "only", state: "done" } ]
    check("it answers the new count", qt.refresh(board, "queue"), 1)
    check("the table shrank", qt.read(board, "queue", "rowCount"), 1)
    check("and shows the new list", qt.cell(board, "queue", 0, "who"), "only")
    ' A STALE ROW BELOW IS THE DEFECT THIS CATCHES: if row 1 still existed it
    ' would hold "grace", a perfectly ordinary name under a correct heading.
    check("there is no row 1 left",
          contains(refused_cell("queue", 1), "has no row 1"), true)

    print "-- the CELLS follow too, not just the count"
    jobs = [ { who: "ada", state: "done" } ]
    shown = qt.refresh(board, "queue")
    check("a changed FIELD of the same row is redrawn",
          qt.cell(board, "queue", 0, "state"), "done")

    print "-- an empty list is a list"
    jobs = []
    check("refreshing to nothing answers 0", qt.refresh(board, "queue"), 0)
    check("and the table is empty", qt.read(board, "queue", "rowCount"), 0)
    ' THE CONTROL FOR THAT: it comes BACK. A refresh that broke on empty would
    ' pass the check above and fail here.
    jobs = [ { who: "back", state: "waiting" } ]
    check("and it fills again", qt.refresh(board, "queue"), 1)
    check("with the right row", qt.cell(board, "queue", 0, "who"), "back")

    print "-- THE CAPTURE MOVES WITH IT, or a fold reads two different moments"
    rows = [ { a: "one" }, { a: "two" } ]
    grew = qt.refresh(board, "sheet")
    check("the edit table grew", grew, 2)
    ' NO TYPING NEEDED, and a table's columns are deliberately NOT window-wide
    ' names (§0.6), so `qt.write(board, "a", ...)` is correctly refused -- the
    ' fold alone is what this tier is about.
    back = qt.subject(board, "rows")
    ' WITHOUT UPDATING THE CAPTURED RECORD this would fold the widgets over
    ' the ONE-element list captured at open, so the second row would vanish --
    ' a plausible result assembled from two different moments.
    check("and a fold sees the NEW list, not the captured one", len(back), 2)

    print "-- refusals, each beside its nearest legal neighbour"
    check("a window with no such table says so",
          contains(refused(board, "nope"), "declares no table named `nope`"),
          true)
    check("an OPEN window with no tables at all says the same",
          contains(refused(plain, "queue"), "declares no table"), true)
    ' A WINDOW THAT IS NOT OPEN gets its own sentence, because that is a
    ' different problem with a different fix.
    check("a window that is not open says THAT",
          contains(refused(shut, "queue"), "is not open"), true)
    qt.stop()
end program

function refused_cell(table, row)
    on error goto handle
    v = qt.cell(board, table, row, "who")
    return "ANSWERED " + string(v)
handle:
    m = error.message
    error.clear()
    return m
end function
