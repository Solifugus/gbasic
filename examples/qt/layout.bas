' LAYOUT, AND NOTHING ELSE -- every container §2.5 defines, with no data, no
' binding and no table, so what the screen shows is the layout and only the
' layout.
'
' Run it:
'     ./gbasic examples/qt/layout.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/layout.bas check
'
' The second form prints what it can observe and exits, which is also how you
' check a Qt install without looking at a screen.
'
' THERE ARE TWO KINDS OF CONTAINER HERE AND THE DIFFERENCE IS ALIGNMENT.
' `row` and `column` are BOXES: they pack things one after another and each row
' is independent, so two rows' columns do not line up. `cells` is a GRID: a
' `row` inside it means a row whose columns ALIGN with the rows above, which is
' the half a box cannot do, and `across:`/`down:` span them.
'
' AND ONLY THE GRID'S POSITIONS ARE READABLE. `qt.at` asks a QGridLayout where
' a widget is; a box layout has no coordinates to answer with, so the checks
' below cover the grid and the box half is shown rather than asserted. Said
' here because an example that quietly checks less than it looks like it does
' is worse than one that says so.

window shapes( title: "Layout", width: 520, height: 460 )
    column( margin: 10, spacing: 10 )

        text heading( label: "Four containers, and what each is for" )

        ' A BOX, HORIZONTAL. `stretch()` is pure space: it takes whatever is
        ' left over, so putting one FIRST pushes everything after it to the
        ' right -- which is how a dialog's buttons end up where people expect.
        group "row: packed left to right"
            row
                button left( text: "Left" )
                stretch()
                button middle( text: "Middle" )
                stretch()
                button right( text: "Right" )
            end row
        end group

        ' A FORM is a column of label-and-widget pairs, and the labels line up
        ' because Qt owns the layout rather than the author counting spaces.
        group "form: a label beside each part"
            form to show nothing_in_particular
                field one( label: "First" )
                field two( label: "Second and longer" )
                check three( label: "A box" )
            end form
        end group

        ' A GRID. Two rows whose columns ALIGN, and a heading SPANNING both --
        ' which is the thing a `row` cannot express at all.
        group "cells: columns that line up, and spans"
            cells
                row
                    text title( label: "Spanning two columns", across: 2 )
                end row
                row
                    field a( label: "A" )
                    field b( label: "B" )
                end row
                row
                    field c( label: "C" )
                    field d( label: "D" )
                end row
            end cells
        end group

        row
            stretch()
            button close( text: "Close" )
        end row
    end column

    on close.clicked
        qt.stop()
    end on
end window

program main(args)
    nothing_in_particular = {}
    if not qt.can_display() then
        print "No display is available, so there is no window to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
        return
    end if
    qt.open(shapes)

    if contains(args, "check") then
        ' THE GRID IS WHAT CAN BE ASKED. Every one of these comes from the
        ' LAYOUT rather than from any record of where things were put, which is
        ' the only way to tell a widget that was placed from one that was
        ' merely made.
        print "heading:           " + qt.read(shapes, "heading")
        print "title spans:       " + string(qt.at(shapes, "title").across)
        print("a is at:           " + string(qt.at(shapes, "a").row) + "," +
              string(qt.at(shapes, "a").column))
        print("d is at:           " + string(qt.at(shapes, "d").row) + "," +
              string(qt.at(shapes, "d").column))
        ' AND THE FACT THOSE NUMBERS TEACH: a labelled `field` takes TWO
        ' cells -- its label and itself -- so `a` sits in column 1 with its
        ' "A" in column 0. That is why a grid of labelled fields lines up at
        ' all, and the column is the only way to see the label was placed.
        print "a's label is at:   0," + string(qt.at(shapes, "a").column - 1)
        print("b and d share a column: " +
              string(qt.at(shapes, "b").column = qt.at(shapes, "d").column))
        ' AND THE HONEST LIMIT, printed rather than hidden: a box has no
        ' coordinates, so this is a refusal and not an absence.
        on error goto no_coords
        at = qt.at(shapes, "left")
        print "a button in a row answered " + string(at)
        qt.stop()
        return
no_coords:
        print "a part in a `row` has no grid position: " + error.message
        error.clear()
        qt.stop()
        return
    end if
    qt.run()
end program
