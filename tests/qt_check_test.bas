' `qt.check` -- the declaration's own rules, over a window a program BUILT.
' NEEDS NO QT AND NO DISPLAY, which is the point: this is a check on a
' DECLARATION, and the whole declarative surface was built to be provable with
' no toolkit (the `web.routes` property).
'
' MEASURED BEFORE A LINE WAS WRITTEN, which is what shaped the increment. §4 of
' docs/qt_gui_design.md says the block is SUGAR OVER A BUILDER because a
' declaration cannot contain a loop -- and that door turned out to be ALREADY
' OPEN: `qt.open` reads the record the declaration binds and asks only that
' `kind` is "window", so a built record opens, renders, reads back and fires
' handlers exactly as a declared one. Nobody knew, because nothing said so.
'
' WHAT IT LACKED WAS EVERY LOAD-TIME CHECK, and that is the defect
' `web.configure` was built to close rather than a missing nicety. Measured on
' the binary before this suite existed:
'
'   a part with NO NAME       opened quietly, unaddressable forever
'   TWO PARTS SHARING A NAME  opened quietly, the second shadowing the first
'   word: "feild"             refused by the BACKEND for having no widget
'                             wired, never "did you mean 'field'?"
'   `item` in a `column`      the right refusal with the wrong sentence
'   a handler naming nothing  refused for carrying no function, never "there
'                             is nothing called `nobody` in this window"
'
' The last is the refusal load-time validation exists for, so a builder without
' it is a builder whose mistakes surface as a window that looks fine.
'
' SELF-CHECKING AND FORCED: every defect here is an ORDINARY-LOOKING WINDOW. A
' name silently shadowed still shows a widget; a typo'd key is a part that
' simply has no label. None of them raises.
'
' THE CONTROLS ARE THE LOAD-BEARING HALF. A refusal suite with no control is
' satisfied by a checker that refuses everything -- and this one would be the
' worst kind, since it now runs inside `qt.open` and would refuse every window
' in the language.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

' A refusal, reported as its message so the SENTENCE is what is asserted and
' not merely that something was refused. The declaration's whole argument is
' that the words name the thing to edit.
function refused(win)
    on error goto handle
    ok = qt.check(win)
    return "ACCEPTED"
handle:
    message = error.message
    error.clear()
    return message
end function

function col(parts)
    return { kind: "container", word: "column", options: {}, items: parts }
end function

function win(items)
    return { kind: "window", name: "w", options: {}, items: items }
end function

function field(name)
    return { kind: "part", word: "field", name: name, options: {} }
end function

program main()
    print "-- CONTROL: a built window with nothing wrong is ACCEPTED"
    good = { kind: "window", name: "ok", options: { title: "T", width: 300 },
             items: [ col([ field("a"), field("b") ]) ] }
    check("a good built window passes", qt.check(good), true)

    print "-- the vocabulary, with the declaration's near-miss hint"
    m = refused(win([ col([ { kind: "part", word: "feild", name: "a",
                              options: {} } ]) ]))
    check("a typo is refused with the hint, not a backend message",
          contains(m, "did you mean 'field'?"), true)
    check("and it says WHERE, a record having no line",
          contains(m, "at column[0] > feild a"), true)

    print "-- a part with no name was ACCEPTED before this existed"
    m = refused(win([ col([ { kind: "part", word: "field", options: {} } ]) ]))
    check("a part with no name is refused", contains(m, "needs a name"), true)

    print "-- two parts sharing a name was ACCEPTED, the second shadowing"
    m = refused(win([ col([ field("a"), field("a") ]) ]))
    check("a name declared twice is refused, naming the consequence",
          contains(m, "declared twice"), true)

    print "-- the refusal load-time validation exists for"
    m = refused(win([ col([ field("a") ]),
                      { kind: "handler", target: "nobody", signal: "clicked",
                        handler: noop } ]))
    check("a handler naming nothing could never fire",
          contains(m, "names nothing in this window"), true)
    ' AND A BUILT HANDLER MAY CARRY NOTHING TO CALL, which a declared one
    ' cannot -- the grammar gave it a body. The two are different mistakes and
    ' get different sentences.
    m = refused(win([ col([ { kind: "part", word: "button", name: "go",
                              options: {} } ]),
                      { kind: "handler", target: "go", signal: "clicked" } ]))
    check("a handler with no function is refused separately",
          contains(m, "carries nothing to call"), true)

    print "-- placement, which depends on an item's surroundings"
    m = refused(win([ col([ { kind: "part", word: "item", name: "x",
                              options: {} } ]) ]))
    check("an `item` outside a menu is refused",
          contains(m, "belongs in a menu"), true)
    m = refused(win([ col([ { kind: "container", word: "menu", label: "File",
                              options: {}, items: [] } ]) ]))
    check("a menu inside a layout is refused",
          contains(m, "a menu bar is not laid out"), true)
    m = refused(win([ col([ { kind: "container", word: "tab", label: "One",
                              options: {}, items: [] } ]) ]))
    check("a `tab` outside a `tabs` is refused",
          contains(m, "belongs to a `tabs`"), true)

    print "-- the rules a named container depends on"
    m = refused(win([ { kind: "container", word: "table", subject: "rows",
                        direction: "show", options: {},
                        items: [ field("a") ] } ]))
    check("a table with no name is refused", contains(m, "a table needs a name"),
          true)
    m = refused(win([ { kind: "container", word: "form", options: {},
                        items: [ field("a") ] } ]))
    check("a form with no subject and no window subject is refused",
          contains(m, "names no subject"), true)

    print "-- THE REPRESENTATION, which is a different question from the rules"
    m = refused(win([ col([ { kind: "part", word: "field", nmae: "a",
                              options: {} } ]) ]))
    check("a typo'd FIELD NAME is refused by name",
          contains(m, "a part carries no 'nmae'"), true)
    check("and the message lists what a part may carry",
          contains(m, "kind word name label options"), true)
    m = refused({ kind: "window", name: "w", options: { titel: "T" },
                  items: [ col([ field("a") ]) ] })
    check("a typo'd head option is refused by name",
          contains(m, "unknown option 'titel'"), true)
    m = refused({ kind: "window", name: "w", titel: "T", options: {},
                  items: [ col([ field("a") ]) ] })
    check("a typo'd field on the WINDOW is refused by name",
          contains(m, "a window carries no 'titel'"), true)
    m = refused(win([ col([ { word: "field", name: "a" } ]) ]))
    check("an item with no `kind:` is refused, naming the three",
          contains(m, "needs `kind:`"), true)
    m = refused(win([ col([ "field a" ]) ]))
    check("a string among the items is refused as what it is",
          contains(m, "this is a string, not an item record"), true)

    print "-- the count travels, a raise carrying one sentence"
    m = refused(win([ col([ { kind: "part", word: "field", options: {} },
                            { kind: "part", word: "button", options: {} } ]) ]))
    check("two problems are reported as one plus a count",
          contains(m, "and 1 more"), true)

    print "-- CONTROLS: what must still be ACCEPTED"
    ' A refusal suite with no controls is satisfied by refusing everything, and
    ' here that would refuse every window in the language, `qt.open` now
    ' running this. One per shape the vocabulary admits.
    check("a stretch with no name is fine",
          qt.check(win([ col([ { kind: "part", word: "stretch",
                                 options: {} } ]) ])), true)
    check("a separator in a menu is fine",
          qt.check(win([ { kind: "container", word: "menu", label: "File",
                           options: {},
                           items: [ { kind: "part", word: "separator",
                                      options: {} } ] } ])), true)
    check("a menu at the window's top level is fine",
          qt.check(win([ { kind: "container", word: "menu", label: "File",
                           options: {},
                           items: [ { kind: "part", word: "item", name: "o",
                                      options: { label: "Open" } } ] } ])), true)
    check("a submenu inside a menu is fine",
          qt.check(win([ { kind: "container", word: "menu", label: "File",
                           options: {},
                           items: [ { kind: "container", word: "menu",
                                      label: "More", options: {},
                                      items: [ { kind: "part", word: "item",
                                                 name: "d", options: {} } ] } ] } ])),
          true)
    check("a tab inside a tabs is fine",
          qt.check(win([ { kind: "container", word: "tabs", name: "tt",
                           options: {},
                           items: [ { kind: "container", word: "tab",
                                      label: "One", options: {},
                                      items: [ field("a") ] } ] } ])), true)
    check("a row inside cells is fine",
          qt.check(win([ { kind: "container", word: "cells", options: {},
                           items: [ { kind: "container", word: "row",
                                      options: {},
                                      items: [ field("a") ] } ] } ])), true)
    check("a table's columns are ITS OWN names, not the window's",
          qt.check(win([ { kind: "container", word: "table", name: "t",
                           subject: "rows", direction: "show", options: {},
                           items: [ field("who") ] },
                         col([ field("who") ]) ])), true)
    check("a form inheriting the window's subject is fine",
          qt.check({ kind: "window", name: "w", subject: "cust",
                     direction: "edit", options: {},
                     items: [ { kind: "container", word: "form", options: {},
                                items: [ field("a") ] } ] }), true)
    check("a handler carrying a function and naming a real part is fine",
          qt.check(win([ col([ { kind: "part", word: "button", name: "go",
                                 options: {} } ]),
                         { kind: "handler", target: "go", signal: "clicked",
                           handler: noop } ])), true)
    check("a handler may name a named CONTAINER",
          qt.check(win([ { kind: "container", word: "table", name: "t",
                           subject: "rows", direction: "show", options: {},
                           items: [ field("who") ] },
                         { kind: "handler", target: "t", signal: "activated",
                           handler: noop } ])), true)

    print "-- DEPTH, which only a BUILT window can get wrong"
    ' A declaration's nesting is bounded by its source; a loop's is bounded by
    ' nothing, and the checker RECURSES -- so the cap stands in front of a C
    ' stack overflow, not a tidiness rule. MEASURED: with the cap lifted the
    ' walk survives 5,000 and segfaults at 10,000, and building the same
    ' 10,000-deep record WITHOUT checking it exits 0, which is what makes that
    ' a fact about this walk rather than about gBASIC.
    node = { kind: "part", word: "field", name: "leaf", options: {} }
    i = 1
    while i <= 60
        node = { kind: "container", word: "column", options: {},
                 items: [ node ] }
        i = i + 1
    end while
    check("CONTROL: sixty nested containers is accepted",
          qt.check(win([ node ])), true)
    while i <= 70
        node = { kind: "container", word: "column", options: {},
                 items: [ node ] }
        i = i + 1
    end while
    m = refused(win([ node ]))
    check("past the cap it is refused, naming the cap",
          contains(m, "nested more than 64 deep"), true)

    print "-- qt.check refuses what is not a window at all"
    m = refused({ kind: "part", word: "field", name: "a", options: {} })
    check("a part is not a window", contains(m, "expects a window record"), true)
end program

function noop()
end function
