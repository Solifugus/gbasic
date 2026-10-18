' `text` AND `memo` -- a sentence on a window, and many lines of one. Under
' QT_QPA_PLATFORM=offscreen: real Qt, no display.
'
' WHY THEY EXIST, measured 2026-10-18 while writing one example per aspect of
' this backend: the leaf vocabulary reached QLineEdit, QPushButton and
' QCheckBox AND NO MORE, so a heading, an instruction or a room description
' had nowhere to go at all. A GUI toolkit that cannot display a sentence is
' not fully functional, and nothing in the tree said so -- every fixture and
' every example happened to need only an entry, a box and a button.
'
' THE NEAREST SUBSTITUTE WAS MEASURED AND REFUSED: a read-only frameless
' QLineEdit looks like a label (`readOnly: true, frame: false` both take), and
' it CANNOT WRAP and is still a text entry -- focusable, single-line,
' selectable as an input. A paragraph was not expressible, so this is a gap
' rather than a convenience.
'
' SELF-CHECKING AND FORCED: every defect here is a window that still looks
' like a window. A label that does not wrap shows the first line of a
' description and silently drops the rest; a memo whose text went to the wrong
' property is an empty pane. Neither raises.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window page( title: "Reading", width: 420, height: 320 )
    column( margin: 8, spacing: 6 )
        text heading( label: "The Cellar" )
        text blurb( label: "A dank room. Water drips somewhere behind you, and the only light comes from a grate overhead." )
        memo log( label: "You are in the cellar." )
        ' THE CONTROL FOR THE WHOLE FILE: a `field` is still one line and still
        ' editable, or "text and memo work" is satisfied by a change that made
        ' every leaf the same widget.
        field entry( label: "Command" )
    end column
end window

' A refusal reported as its MESSAGE, so what is asserted is the sentence.
function refused_read(part, prop)
    on error goto handle
    v = qt.read(page, part, prop)
    return "ANSWERED " + string(v)
handle:
    m = error.message
    error.clear()
    return m
end function

' A SECOND WINDOW for the placement half: `qt.at` asks a `cells` grid where a
' widget is, and a heading spanning the columns above a pane is what a `text`
' is most often for.
window grid( title: "Placed", width: 360, height: 240 )
    cells
        row
            text title( label: "Inventory", across: 2 )
        end row
        row
            memo body( label: "a brass key" )
        end row
    end cells
end window

program main()
    if not qt.can_display() then
        print "SKIP no display"
        return
    end if
    qt.open(page)
    qt.open(grid)

    print "-- `text`: a sentence, and it WRAPS"
    check("a text reads back what it was given",
          qt.read(page, "heading"), "The Cellar")
    ' WRAPPING IS THE WHOLE REASON THIS IS NOT A read-only FIELD. Asserted as
    ' a property rather than by looking at pixels, and paired with the field
    ' below so it is a DIFFERENCE and not just a true.
    check("it wraps by default", qt.read(page, "blurb", "wordWrap"), true)
    check("and a long description is held whole, not truncated",
          len(qt.read(page, "blurb")) > 80, true)
    ' A LABEL IS NOT AN ENTRY, which is the half a value check cannot see: a
    ' read-only `field` passes every check above. The discriminator is that a
    ' QLabel has no `readOnly` AT ALL, and an absent property is REFUSED BY
    ' NAME rather than answered `unknown` -- which is this backend's contract
    ' and the reason the refusal is the assertion here.
    check("a text has no `readOnly`, being no kind of entry",
          refused_read("heading", "readOnly"),
          "qt.read: a QLabel has no `readOnly` property")
    check("CONTROL: a field DOES have one", qt.read(page, "entry", "readOnly"),
          false)

    print "-- `text` is written like anything else"
    qt.write(page, "heading", "The Garden")
    check("qt.write reaches it", qt.read(page, "heading"), "The Garden")
    qt.set(page, "heading", "styleSheet", "font-weight: bold")
    check("and so does the by-name property route",
          qt.read(page, "heading", "styleSheet"), "font-weight: bold")

    print "-- `memo`: many lines, through `plainText` and not `text`"
    ' A QPlainTextEdit HAS NO `text` PROPERTY. The reader's fallthrough asked
    ' for one, which answers an INVALID QVariant, which this backend reports
    ' as an ABSENT PART -- so `qt.read` on a perfectly real memo said the
    ' window had no such thing. Both halves are named rather than left to the
    ' fallthrough, and this is the pair that says so.
    check("a memo reads back", qt.read(page, "log"), "You are in the cellar.")
    three = "You are in the cellar." + chr(10) + "> go north" + chr(10) + "You are in the garden."
    qt.write(page, "log", three)
    check("a memo holds MANY lines", len(split(qt.read(page, "log"), chr(10))), 3)
    check("byte for byte", qt.read(page, "log"), three)
    ' AND THE CONTROL THAT THE TWO ARE DIFFERENT KINDS OF WIDGET, which cost
    ' me a wrong assumption: I expected a QLineEdit given three lines to keep
    ' ONE, and MEASURED it keeps all three -- Qt strips newlines from TYPED
    ' input, not from `setText`, so the difference between a field and a memo
    ' is what a person SEES rather than what is stored. A text comparison
    ' therefore cannot tell them apart at all.
    qt.write(page, "entry", three)
    check("a field given three lines stores three (Qt strips only typed input)",
          contains(qt.read(page, "entry"), chr(10)), true)
    ' SO THE DISCRIMINATOR IS THE CLASS, asked through the same by-name door:
    ' `plainText` is a QPlainTextEdit property and a QLineEdit has none.
    check("only a memo carries `plainText`",
          type(qt.read(page, "log", "plainText")), "string")
    check("CONTROL: a field does not, and says so by name",
          refused_read("entry", "plainText"),
          "qt.read: a QLineEdit has no `plainText` property")
    check("a memo is editable by default, being an entry",
          qt.read(page, "log", "readOnly"), false)
    qt.set(page, "log", "readOnly", true)
    check("and can be made a transcript", qt.read(page, "log", "readOnly"), true)

    print "-- both are ordinary parts, with nothing special-cased"
    ' THEY ARE NAMED LEAVES, so they place in a grid and answer `qt.at` like
    ' anything else -- which is the half a property read cannot see, and the
    ' commonest real use of a `text` is a heading SPANNING a grid.
    check("a text spans its grid row", qt.at(grid, "title").across, 2)
    check("and the memo under it sits in the row below",
          qt.at(grid, "body").row, 1)
    qt.stop()
end program
