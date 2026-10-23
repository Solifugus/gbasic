' A COLUMN'S ALIGNMENT -- `align:` on a table's or tree's `field`, and
' `qt.alignment`. Under QT_QPA_PLATFORM=offscreen.
'
' ASKED FOR BY gBASIC STUDIO: Matthew wants numbers right-aligned in its grid,
' an asking table, and Studio could not do it, because the model answered only
' Qt's DisplayRole -- the value's kind was gone before Qt saw the text, and
' nothing set Qt::TextAlignmentRole. Studio already knows which columns are
' numeric (it sorts them numerically), so it says so per column.
'
' THE ORACLE ASKS QT, not the declaration: `qt.alignment` reads the asking
' model's TextAlignmentRole and a held table's or tree's item alignment, so a
' backend that stored `align:` and never applied it is caught. The control is
' a column declaring nothing, which must stay left, or "right" would be
' satisfied by a table that right-aligned every column.

window app( title: "Align", width: 420, height: 320 )
    column( margin: 8 )
        table asked to ask cell_of
            field label( label: "Name" )
            field amount( label: "Amount", align: "right" )
            field note( label: "Note", align: "center" )
        end table
        table held to show rows
            field who( label: "Who" )
            field total( label: "Total", align: "right" )
        end table
        tree files to show nodes
            field text( label: "File" )
            field size( label: "Size", align: "right" )
            field depth( depth: true )
        end tree
    end column
end window

function cell_of(row, column)
    if column = 0 then return "row " + string(row)
    if column = 1 then return string(row * 10)
    return "n"
end function

program main()
    g = { checks: 0, bad: 0 }
    if not qt.can_display() then
        print "NODISPLAY"
        return
    end if
    rows = [ { who: "Ada", total: "12.50" }, { who: "Lin", total: "7.00" } ]
    nodes = [ { text: "src", size: "", depth: 0 },
              { text: "a.bas", size: "120", depth: 1 } ]
    qt.open(app)
    qt.rows(app, "asked", 3)

    ' ---- THE ASKING TABLE, which is Studio's case ------------------------
    check("an asking table's undeclared column stays left",
          qt.alignment(app, "asked", 0, "label"), "left")
    check("its `align: \"right\"` column is right-aligned",
          qt.alignment(app, "asked", 2, "amount"), "right")
    check("and `align: \"center\"` centres",
          qt.alignment(app, "asked", 1, "note"), "center")
    check("while the cell's text is unchanged", qt.cell(app, "asked", 2, "amount"), "20")

    ' ---- A HELD TABLE, and a refresh keeps it ----------------------------
    check("a held table's undeclared column stays left",
          qt.alignment(app, "held", 0, "who"), "left")
    check("its right-aligned column", qt.alignment(app, "held", 1, "total"), "right")
    append(rows, { who: "Kim", total: "3.25" })
    x = qt.refresh(app, "held")
    check("a row added by qt.refresh is aligned too",
          qt.alignment(app, "held", 2, "total"), "right")

    ' ---- A TREE ----------------------------------------------------------
    check("a tree's undeclared column stays left",
          qt.alignment(app, "files", 1, "text"), "left")
    check("its right-aligned column", qt.alignment(app, "files", 1, "size"), "right")

    ' ---- REFUSALS ----------------------------------------------------------
    refused("a column that is not there",
            "qt.alignment: table `held` has no `nope` column", 1)
    refused("a row that is not there",
            "qt.alignment: table `held` has no row 99", 2)
    refused("a built window with a misspelled column option",
            "window: a table's column takes `label`, `type`, `depth` and `align`, and 'alignn' is none of them -- did you mean 'align'? (at column[0] > table t > field n)",
            3)
    refused("an `align:` that is not left, right or center",
            "window: `align:` on column 'n' is \"left\", \"right\" or \"center\" (at column[0] > table t > field n)",
            4)
    qt.close(app)
    print "ok   " + string(g.checks) + " checks, " + string(g.bad) + " mismatches"
end program

function built(opts)
    return { kind: "window", name: "built", options: { title: "b" },
             items: [ { kind: "container", word: "column", items: [
                 { kind: "container", word: "table", name: "t",
                   direction: "show", subject: "rows",
                   items: [ { kind: "part", word: "field", name: "n",
                              options: opts } ] } ] } ] }
end function

function refused(what, want, which)
    on error goto next
    if which = 1 then x = qt.alignment(app, "held", 0, "nope")
    if which = 2 then x = qt.alignment(app, "held", 99, "total")
    if which = 3 then x = qt.check(built({ alignn: "right" }))
    if which = 4 then x = qt.check(built({ align: "middle" }))
    if error then
        check(what, error.message, want)
    else if which >= 3 then
        check(what, string(x), want)
    else
        check(what, "no error", want)
    end if
    return nothing
end function

function check(what, got, want)
    g.checks = g.checks + 1
    if got = want then
        print "ok   " + what
    else
        g.bad = g.bad + 1
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function
