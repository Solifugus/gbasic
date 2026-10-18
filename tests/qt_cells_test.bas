' `cells` -- tabular placement with spans (§2.5), under QT_QPA_PLATFORM=offscreen.
'
' IT NEEDED NO NEW WORD. A grid IS rows of cells, so `cells` holds `row`s --
' reusing the word, refined inside `cells` to mean a row whose columns ALIGN
' with the rows above it, where a `row` elsewhere is a box that does not. Both
' shapes already parsed; only the vocabulary refused them.
'
' SELF-CHECKING AND FORCED, AND THE ORACLE WAS BUILT BEFORE THIS FILE, the
' pattern having cost two increments already: every part reads back BY NAME, so
' a widget in the WRONG CELL or with the WRONG SPAN is invisible to every other
' check -- and placement is the entire point of `cells`. `qt.at` asks the
' LAYOUT where a widget is, walking up from the widget rather than from any
' record of where we put it, because a remembered position would agree with a
' placement that was wrong.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window sheet( title: "Cells", width: 480, height: 280 )
    column( margin: 8 )
        cells( spacing: 6 )
            row
                field first( label: "First" )
                field last(  label: "Last" )
            end row
            row
                field notes( label: "Notes", across: 3 )
            end row
            row
                check agree( label: "I agree", across: 4 )
                button help_me( text: "?" )
            end row
            row
                field tall( label: "Tall", down: 2 )
            end row
        end cells
        row
            stretch()
            button ok( text: "OK" )
        end row
    end column
end window

program main()
    check("a window with cells opens", qt.open(sheet), true)

    ' ------------------------------------------------------------ PLACEMENT
    ' EACH ROW IS THE NEXT ONE DOWN, and columns fill left to right -- which
    ' is the only thing that makes a grid a grid rather than a stack of boxes.
    check("the first row is row 0", qt.at(sheet, "first").row, 0)
    check("the second row is row 1", qt.at(sheet, "notes").row, 1)
    check("the third is row 2", qt.at(sheet, "agree").row, 2)
    check("and the fourth row 3", qt.at(sheet, "tall").row, 3)

    ' A `field`'s LABEL TAKES A CELL OF ITS OWN, which is how a labelled grid
    ' lines up -- and it is also the only way the FIELD half of the `label:`
    ' fix is assertable at all: a field's label becomes a separate QLabel with
    ' no name, so it cannot be read back. Its COLUMN says it is there.
    check("a field sits one column right of its own label",
          qt.at(sheet, "first").column, 1)
    check("so the next field starts after BOTH of them",
          qt.at(sheet, "last").column, 3)
    ' A CHECK CARRIES ITS OWN CAPTION, so it takes no label cell -- the
    ' difference from a field, and the control that says the label cell is
    ' added for a reason rather than always.
    check("a check needs no label cell, so it starts at column 0",
          qt.at(sheet, "agree").column, 0)

    ' ---------------------------------------------------------------- SPANS
    ' THE HALF A BOX LAYOUT CANNOT DO AT ALL, and the reason `cells` exists.
    check("`across:` covers that many columns", qt.at(sheet, "notes").across, 3)
    check("and leaves the row count alone", qt.at(sheet, "notes").down, 1)
    check("`down:` covers that many rows", qt.at(sheet, "tall").down, 2)
    check("and leaves the column count alone", qt.at(sheet, "tall").across, 1)
    check("a part with neither spans one cell",
          qt.at(sheet, "first").across, 1)
    ' AND A SPAN DOES NOT MOVE THE WIDGET, which the obvious implementation
    ' gets wrong: QGridLayout cannot change an item's span, so the widget is
    ' REMOVED and re-added -- at the position it already had, not at the next
    ' free one.
    check("a spanned part keeps its position", qt.at(sheet, "agree").row, 2)
    check("and its column", qt.at(sheet, "agree").column, 0)

    ' ------------------------------------------------- STILL ORDINARY PARTS
    ' A cell holds a REAL widget, so everything that works elsewhere works
    ' here -- without this, `cells` could place things correctly and render
    ' them unusable.
    check("a field in a cell takes a value", qt.write(sheet, "first", "Ada"),
          true)
    check("and reads it back", qt.read(sheet, "first"), "Ada")
    check("a check in a cell has its caption",
          qt.read(sheet, "agree", "text"), "I agree")
    check("a button in a cell has its text",
          qt.read(sheet, "help_me", "text"), "?")
    check("and what is OUTSIDE the cells is still laid out",
          qt.read(sheet, "ok", "text"), "OK")

    ' ------------------------------------------------------------- REFUSALS
    on error goto next
    x = qt.at(sheet, "ok")
    if error then
        print "NOTINGRID: " + error.message
        error.clear()
    else
        print "MISMATCH a part outside any cells reported a position"
    end if
end program
