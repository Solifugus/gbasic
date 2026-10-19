' `split` AND `scroll` (§0.19) -- under QT_QPA_PLATFORM=offscreen: a real
' QSplitter and a real QScrollArea, no display.
'
' WHY THESE TWO AND WHY TOGETHER. Derived from gBASIC Studio's own source
' rather than guessed: its shell is `gtk.paned` three times nested and
' `gtk.scrolled` five times, and NOTHING in this vocabulary could express
' either -- `row` and `column` are boxes nobody can drag, and `source`, `memo`
' and `table` scroll themselves while an ordinary column of widgets does not.
' They are one increment because they are one layout.
'
' SELF-CHECKING NOT GOLDEN AND FORCED. Every defect here is a window that
' LOOKS BUILT: a splitter made the wrong way round still shows both panes, one
' nested off the window instead of its parent still renders all three regions,
' and a QScrollArea whose `widgetResizable` was left at Qt's default FALSE
' shows its content perfectly until somebody resizes the window -- then a
' stripe of widgets beside a grey expanse, with nothing raised.
'
' THE TIER TO READ FIRST IS NESTING. A `split` spends its one free slot on the
' DIRECTION (§0.8 measured that a container carries a name OR options, never
' both), so it CANNOT BE NAMED -- which means nothing can address it and
' nothing could say whether it was built, which way round it is, or how many
' panes it got. `qt.pane` is the oracle, built before this fixture, and it is
' asked of the LAYOUT by walking up from a part that CAN be named -- the rule
' `qt.at` already follows for a grid, and for the same reason: a position we
' remembered would agree with a placement that was wrong.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

' STUDIO'S OWN SHELL SHAPE: a file list beside an editor above an output table.
window app( title: "Shell", width: 760, height: 480 )
    split across
        scroll files
            column( margin: 4 )
                text f1( label: "one.bas" )
                text f2( label: "two.bas" )
                text f3( label: "three.bas" )
            end column
        end scroll
        split down
            source code( plainText: "x = 1" )
            table out to show rows
                field line( label: "Line" )
            end table
        end split
    end split
end window

' A LEAF AND A TABLE AS DIRECT PANES, which is the ordinary shape and the one
' that needed three separate layout guards relaxed -- `gb_qt_container`,
' `gb_qt_part` and `gb_qt_table` each ask the parent for a layout, and a
' QSplitter has none.
window bare( title: "Bare panes", width: 520, height: 200 )
    split across
        field who()
        table lines to show rows
            field line( label: "Line" )
        end table
    end split
end window

' PANES THAT ARE CONTAINERS, which is the ordinary shape -- Studio's panes hold
' a tree and an editor, not single widgets -- and the case that says `qt.pane`
' reports which PANE a part is in rather than which pane the part ITSELF is.
' A perturbation put this here: with the walk asking the splitter for the named
' widget's own index instead of the ancestor it came through, every check in
' this file still passed, because every other part asked about happens to BE a
' pane.
window nested( title: "Nested panes", width: 520, height: 200 )
    split across
        column( margin: 4 )
            field left( label: "Left" )
        end column
        column( margin: 4 )
            field right( label: "Right" )
        end column
    end split
end window

' NEITHER WORD ANYWHERE, so a part in no split and no scroll has something to
' answer with.
window plain( title: "Plain", width: 300, height: 140 )
    column( margin: 6 )
        field alone( label: "Alone" )
    end column
end window

program main(args)
    rows = [ { line: "first" }, { line: "second" } ]

    if not qt.can_display() then
        print "SKIP no display"
        return
    end if

    check("the shell opens", qt.open(app), true)

    print "-- the outer split"
    o = qt.pane(app, "files")
    check("the scroll is a pane of a split", o.kind, "split")
    check("`across` really is horizontal", o.direction, "across")
    check("and it is the first pane", o.index, 0)
    ' THE NESTING CHECK. The outer split holds the scroll AND the inner split;
    ' if the inner one had been hung off the window instead, every other check
    ' in this file would still pass and the outer would hold ONE pane. Same
    ' shape as the menu oracle, which a perturbation found the hard way.
    check("and the outer split holds two panes, so the inner one is in it",
          o.panes, 2)

    print "-- the inner split"
    c = qt.pane(app, "code")
    t = qt.pane(app, "out")
    check("the editor is in a split", c.kind, "split")
    check("`down` really is vertical", c.direction, "down")
    check("the editor is above", c.index, 0)
    check("the table is below", t.index, 1)
    check("and that split holds two", c.panes, 2)

    print "-- the nearest region wins"
    ' `f1` IS INSIDE THE SCROLL INSIDE A COLUMN INSIDE THE SPLIT. The walk stops
    ' at the first region it meets, which is the useful answer: "what am I
    ' scrolled in", not "what am I ultimately under".
    f = qt.pane(app, "f1")
    check("a part deep in the scroll reports the scroll", f.kind, "scroll")
    check("and a scroll answers `resizable` rather than panes",
          has(f, "resizable"), true)
    check("a scroll has no direction", has(f, "direction"), false)

    print "-- the trap: widgetResizable defaults to FALSE in Qt"
    ' LEFT ALONE, the inner widget keeps its original size for ever, so content
    ' never reflows to the viewport and a resized window shows a stripe of
    ' widgets beside a grey expanse -- with nothing raised. Asserted through
    ' BOTH doors: the oracle and the ordinary by-name property read, because a
    ' `scroll` can be named where a `split` cannot.
    check("the oracle says it was set", f.resizable, true)
    check("and so does the by-name read",
          qt.read(app, "files", "widgetResizable"), true)
    check("its scrollbar policy is readable too, by name",
          qt.read(app, "files", "verticalScrollBarPolicy"), "ScrollBarAsNeeded")

    print "-- the scroll really holds its children"
    x = qt.write(app, "f2", "renamed.bas")
    check("a part inside it is writable", qt.read(app, "f2"), "renamed.bas")

    print "-- a leaf and a table as direct panes"
    check("the bare window opens", qt.open(bare), true)
    b = qt.pane(bare, "who")
    check("a field can be a pane", b.kind, "split")
    check("and a table can be the other", qt.pane(bare, "lines").index, 1)
    e = qt.refresh(bare, "lines")
    check("the pane table populates", qt.cell(bare, "lines", 1, "line"),
          "second")

    print "-- a part nested inside a pane reports that pane"
    check("the nested window opens", qt.open(nested), true)
    l = qt.pane(nested, "left")
    r = qt.pane(nested, "right")
    check("a part in the first pane's column", l.index, 0)
    check("and one in the second pane's column", r.index, 1)
    check("both in the same two-pane split",
          string(l.panes) + " " + string(r.panes), "2 2")
    check("and the direction still reads", r.direction, "across")

    print "-- a part in neither answers unknown, not a record of zeroes"
    ' A RECORD SAYING `panes: 0` WOULD READ AS A SPLITTER THAT HOLDS NOTHING.
    ' The rule `qt.dialog` and `principal()` already follow.
    check("the plain window opens", qt.open(plain), true)
    check("a part in no region answers unknown",
          is_unknown(qt.pane(plain, "alone")), true)

    print "-- and the split is not addressable, which is the stated cost"
    on error goto no_name
    bad = qt.read(app, "across", "orientation")
    print "MISMATCH the direction word was registered as a name"
    return
no_name:
    check("its direction word is not a part name",
          contains(error.message, "across"), true)
    error.clear()

    qt.stop()
end program
