' LAYING A SCREEN OUT -- and an author never writes a coordinate.
'
' `row` and `column` pack left-to-right and top-to-bottom. `stretch()` absorbs
' the leftover space, which is why this vocabulary carries no alignment
' properties at all: to push something right, put a stretch to its left.
' `group` is a titled box. `cells` is the one container with coordinates, and
' it ALIGNS columns across its rows, which a row of rows cannot do.
window shapes( title: "Layout", width: 520, height: 320 )
    column( margin: 10, spacing: 10 )

        group "Packed in a row"
            row( spacing: 6 )
                button one(   text: "One" )
                button two(   text: "Two" )
                stretch()
                button right( text: "Pushed right" )
            end row
        end group

        group "Aligned in a grid"
            cells
                row
                    field code( label: "Code" )
                    field qty(  label: "Qty" )
                end row
                row
                    ' ACROSS SPANS COLUMNS, which is the thing a box cannot do.
                    field note( label: "Note", across: 3 )
                end row
            end cells
        end group
    end column
end window

program main(args)
    if not qt.can_display() then
        print "no display"
        return
    end if
    qt.open(shapes)

    ' ONLY A GRID HAS COORDINATES TO REPORT, and `qt.at` asks the LAYOUT rather
    ' than any record of where we put things -- so it answers where a widget
    ' REALLY is.
    at = qt.at(shapes, "code")
    print "code sits at row " + string(at.row) + ", column " + string(at.column)
    at = qt.at(shapes, "qty")
    print "qty  sits at row " + string(at.row) + ", column " + string(at.column)
    at = qt.at(shapes, "note")
    ' BRACKETED: a line break continues a statement only inside brackets.
    print ("note sits at row " + string(at.row) + ", column " +
           string(at.column) + " and spans " + string(at.across))

    ' A LABELLED `field` TAKES TWO CELLS: its label and itself. That is why
    ' `code` is at column 1 rather than 0, and why `note` spans 3.
    print "a labelled field takes two cells, which is why code is at column 1"
    qt.stop()
end program
