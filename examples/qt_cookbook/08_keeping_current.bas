' KEEPING THE SCREEN CURRENT. A declaration is read ONCE, when the window
' opens -- so a table built from a list that later grows would show the old
' list for ever. `qt.refresh` re-reads a subject and redraws from it.
'
' IT ADDRESSES A TABLE BY ITS NAME AND A FORM BY ITS SUBJECT, because a form
' has no name; a word that meant both would be refused rather than resolved by
' a precedence nobody wrote down.
window board( title: "Keeping current", width: 560, height: 340 )
    column( margin: 10, spacing: 8 )
        table jobs to show tasks
            field what(  label: "Task" )
            field state( label: "State" )
        end table
        group "The chosen task"
            form to show current
                field what(  label: "Task" )
                field state( label: "State" )
            end form
        end group
        row
            button add(  text: "Add one" )
            button done( text: "Mark done" )
        end row
    end column

    on add.clicked
        ' APPEND TO THE SUBJECT ITSELF. `append` mutates IN PLACE and resolves
        ' through the frame chain, so it grows the very list `main` holds --
        ' which is what makes a handler able to change the data at all, given
        ' it cannot REBIND anything there. A snapshot (`rows = g.rows`) would
        ' leave the refresh re-reading an unchanged copy.
        append(tasks, { what: "Task " + string(len(tasks) + 1), state: "open" })
        drew = board.refresh("jobs")
        return nothing
    end on

    on done.clicked
        at = board.selected("jobs")
        if not is_unknown(at) then
            tasks[at].state = "done"
            drew = board.refresh("jobs")
            ' AND THE FORM, by its SUBJECT. Mutate the subject's FIELDS rather
            ' than rebinding it: a handler cannot rebind `current`.
            current.what = tasks[at].what
            current.state = tasks[at].state
            drew = board.refresh("current")
        end if
        return nothing
    end on
end window

program main(args)
    ' ONE LIST, and it IS the subject -- no second copy to drift.
    tasks = [ { what: "Write the page", state: "open" },
              { what: "Run the gate",   state: "open" } ]

    if not qt.can_display() then
        print "no display"
        return
    end if
    current = tasks[0]
    qt.open(board)

    print "the table starts with " + string(len(tasks)) + " rows"
    board.invoke("add", "click")
    board.invoke("add", "click")
    print "after adding twice:   " + string(len(tasks)) + " rows"
    print "the new last row is:  " + board.cell("jobs", 3, "what")

    board.select("jobs", 1)
    board.invoke("done", "click")
    print "row 1's state is now: " + board.cell("jobs", 1, "state")
    print "and the form says:    " + board.read("state")
    qt.stop()
end program
