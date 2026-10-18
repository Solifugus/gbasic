' THE BUILDER (§4) -- a window a DECLARATION COULD NOT WRITE, because a
' declaration cannot contain a loop. Under QT_QPA_PLATFORM=offscreen: real Qt,
' real widgets, no display.
'
' §4 said "a Qt library wants both doors -- the block for fixed layouts, and a
' builder for data-driven lists. The builder's surface is not designed here."
' MEASURED 2026-10-18, THE DOOR WAS ALREADY OPEN AND THE SURFACE IS THE RECORD:
' `qt.open` reads the record a declaration binds and asks only that `kind` is
' "window", so a record a program BUILDS opens, renders, reads back and fires
' handlers exactly as a declared one. No verb was needed. What was needed was
' the validation a declaration gets -- `qt.check`, and see tests/qt_check_test.bas.
'
' THE LOAD-BEARING TIER IS PARITY, and it has to be: "the builder works" is
' satisfied by a builder that renders something PLAUSIBLE, which is the whole
' failure class this backend's suite exists for -- a window with widgets in it
' looks like a window. So the same window is written BOTH WAYS, declared and
' built, and every observable must AGREE: the properties, the labels, and the
' grid geometry `qt.at` reads back from the layout itself.
'
' AND THE GENERATED HALF IS WHAT NO DECLARATION CAN STATE: one row per record
' of a list whose length is known only at run time, which is §4's own example
' (a row per invoice) and the case §2.4 rejected (a keypad of nineteen buttons).

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

' THE DECLARED HALF of the parity pair. Three fields in a grid, written out.
window declared( title: "Parity", width: 360, height: 200 )
    cells
        row
            field a( label: "A", text: "one" )
            field b( label: "B", text: "two" )
        end row
        row
            field c( label: "C", text: "three", across: 2 )
        end row
    end cells
end window

' THE BUILT HALF -- the same window, from data. Nothing here names a widget:
' the shape comes from the list.
function built_from(rows)
    body = []
    for each r in rows
        cells = []
        for each f in r
            part = { kind: "part", word: "field", name: f.name,
                     options: { label: f.label, text: f.text } }
            if has(f, "across") then
                part.options.across = f.across
            end if
            cells = append(cells, part)
        end for
        body = append(body, { kind: "container", word: "row", options: {},
                              items: cells })
    end for
    return { kind: "window", name: "built",
             options: { title: "Parity", width: 360, height: 200 },
             items: [ { kind: "container", word: "cells", options: {},
                        items: body } ] }
end function

program main()
    rows = [ [ { name: "a", label: "A", text: "one" },
               { name: "b", label: "B", text: "two" } ],
             [ { name: "c", label: "C", text: "three", across: 2 } ] ]
    b = built_from(rows)

    ' The builder's own answer first: it is a window, by the declaration's own
    ' rules, BEFORE anything is constructed.
    check("the built record passes qt.check", qt.check(b), true)

    print "-- PARITY: one window, two doors, every observable equal"
    qt.open(declared)
    qt.open(b)
    for each n in ["a", "b", "c"]
        check("text of " + n + " agrees",
              qt.read(b, n, "text"), qt.read(declared, n, "text"))
        ' GEOMETRY IS THE HALF A PROPERTY READ CANNOT SEE. Every part reads
        ' back by NAME, so a widget in the wrong cell or with the wrong span
        ' is invisible to a value check -- the hole that cost two increments
        ' before `qt.at` existed. Asking the LAYOUT is what separates a
        ' builder that places things from one that merely makes them.
        check("placement of " + n + " agrees",
              qt.at(b, n), qt.at(declared, n))
    end for
    check("the span really is 2, so parity is not two identical mistakes",
          qt.at(b, "c").across, 2)
    check("and `a` sits in the row above it",
          string(qt.at(b, "a").row) + "," + string(qt.at(b, "a").column), "0,1")
    qt.stop()

    print "-- what no declaration can write: a row per record"
    ' Nineteen, which is §2.4's own rejected keypad, and a length a block
    ' cannot name.
    lines = []
    i = 1
    while i <= 19
        lines = append(lines, { name: "r" + string(i), label: "Row " + string(i),
                                text: "v" + string(i) })
        i = i + 1
    end while
    many = { kind: "window", name: "many", options: { title: "Many" },
             items: [ { kind: "container", word: "column", options: {},
                        items: [] } ] }
    for each l in lines
        many.items[0].items = append(many.items[0].items,
            { kind: "part", word: "field", name: l.name,
              options: { label: l.label, text: l.text } })
    end for
    check("nineteen generated parts pass qt.check", qt.check(many), true)
    qt.open(many)
    check("the first reads back", qt.read(many, "r1", "text"), "v1")
    check("the nineteenth reads back", qt.read(many, "r19", "text"), "v19")
    check("and they are NOT all the same widget",
          qt.read(many, "r1", "text") = qt.read(many, "r19", "text"), false)
    qt.stop()

    print "-- a handler on a GENERATED part fires"
    ' A built handler carries a function VALUE, where a declaration carries a
    ' body. The connection is made the same way, which is what this asserts.
    gen = { kind: "window", name: "gen", options: { title: "Gen" },
            items: [ { kind: "container", word: "column", options: {},
                       items: [ { kind: "part", word: "button", name: "go",
                                  options: { label: "Go" } },
                                { kind: "part", word: "field", name: "out",
                                  options: {} } ] },
                     { kind: "handler", target: "go", signal: "clicked",
                       handler: on_go } ] }
    check("the built handler passes qt.check", qt.check(gen), true)
    qt.open(gen)
    qt.invoke(gen, "go", "click")
    check("the generated handler ran and wrote through the window",
          qt.read(gen, "out", "text"), "fired")
    qt.stop()

    print "-- qt.open REFUSES a built window the declaration's rules reject"
    ' A validator nobody is obliged to call is necessary and not sufficient --
    ' `rules.validate` refused a malformed stack all along while
    ' `rules.evaluate` still answered `ok: true` for a string. So the check
    ' runs inside `qt.open` too, and this is the half that says so.
    on error goto refused
    qt.open({ kind: "window", name: "bad", options: {},
              items: [ { kind: "container", word: "column", options: {},
                         items: [ { kind: "part", word: "field",
                                    options: {} } ] } ] })
    print "MISMATCH qt.open accepted a part with no name"
    return
refused:
    check("an invalid built window is refused AT OPEN, in the same words",
          contains(error.message, "needs a name"), true)
    error.clear()
end program

function on_go()
    ' THE WINDOW IS REACHED BY NAME, as every handler reaches one: the record
    ' is the address, so a generated window is addressed exactly as a declared
    ' one.
    qt.write(gen, "out", "fired")
end function
