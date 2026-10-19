' THE QT BACKEND, RENDERING A `window` DECLARATION. Runs under
' QT_QPA_PLATFORM=offscreen, which is real Qt with no display -- the same
' arrangement docs/qt_gui_design.md §0.3 keeps available to a USER so a GUI
' program can render on a headless server.
'
' SELF-CHECKING AND FORCED. Every defect this backend can have produces an
' ordinary-looking RUN: a property silently stored as a dynamic one nothing
' reads, a handler connected to nothing, a value written to the wrong part --
' and none of them raises, so a golden would record the damaged run as expected
' and defend it. Each check states its own expected answer.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window customer( title: "Customer", width: 480, height: 320 )
    column( margin: 8, spacing: 8 )

        form to edit cust
            field id(   label: "Account" )
            field name( label: "Name" )
        end form

        group "Credit"
            form to edit cust
                field limit( label: "Limit", type: {USD} )
                check hold(  label: "On hold" )
            end form
        end group

        ' A RULE BETWEEN SECTIONS, which is a `separator` in a LAYOUT rather
        ' than in a menu -- one word, one meaning (a line that divides), two
        ' widgets. ITS ORIENTATION COMES FROM THE PARENT, as `stretch` takes
        ' its axis from one, and that is load-bearing rather than tidy:
        ' measured, the wrong way round renders as a stubby 119x3 stub taking
        ' a share of the width instead of a line. NAMED, because that is the
        ' only way to observe the orientation -- and because Studio STYLES its
        ' own rule, so one that could not be addressed could not be themed.
        separator rule()

        row
            stretch()
            button save(   text: "Save", default: true )
            ' THE SAME WORD IN A ROW, which must come out the other way round.
            separator bar()
            button cancel( text: "Cancel" )
        end row

    end column

    ' RE-ENTRY INTO THE EVALUATOR FROM A REAL QT SIGNAL. The handler prints
    ' rather than assigning: gBASIC has no closures, so a handler is a function
    ' and a write to an enclosing name would be a function-local -- which is
    ' the read-then-shadow rule run_core.sh exists for, not a defect here.
    ' TWO LINES, AND THE SPLIT IS LOAD-BEARING. The first needs nothing but the
    ' handler having run, so a handler fired during CONSTRUCTION is visible in
    ' the ordering; the second reads the live widget, which is what says the
    ' signal arrived with the window actually built. With one line doing both,
    ' a connect-time fire raises inside `qt.read` and prints NOTHING -- so the
    ' ordering control could not see it, which is how the perturbation written
    ' for that control came back green.
    on save.clicked
        print "SIGNAL save.clicked"
        print "SIGNAL read -> " + qt.read(customer, "name")
    end on
end window

program main()
    ' THE PROGRESS LINE, FIRST. A crash takes block-buffered stdout with it, so
    ' this line arriving at all is part of what the no-display tier asserts one
    ' fixture over.
    print "before open"

    check("the declaration is still just a record", customer.kind, "window")
    check("and the handler carries something callable",
          type(customer.items[1].handler), "function")

    ' THE SUBJECT MUST EXIST, and that is new: `form to edit cust` has named a
    ' subject since the declaration shipped and nothing required one to be
    ' there, so a form bound to nothing opened quietly and could never
    ' populate. It is read from the frame that calls `qt.open` -- "a place to
    ' read in the frame where the subject lives" (§0.2), that frame being
    ' exactly the one asking.
    cust = { id: "A-1000" }

    check("qt.open answers", qt.open(customer), true)
    print "opened"

    ' POPULATION, which is the binding's first direction: a field named `id`
    ' takes `cust.id` because THE BINDING IS THE NAME (§2.2).
    check("a field bound by name is filled in", qt.read(customer, "id"), "A-1000")

    ' A PROPERTY SET AT CONSTRUCTION, read back THROUGH QT rather than from our
    ' own record -- which is the only thing that distinguishes a property that
    ' reached the widget from one we merely remembered.
    check("a button's text reached the widget", qt.read(customer, "save"), "Save")

    ' `label:` REACHES THE WIDGET, which it did NOT for the first four
    ' increments of this backend: the option lives in `options` and
    ' construction read the TOP-LEVEL `label`, which only a labelled CONTAINER
    ' (`group "Credit"`) ever sets -- so every form row was unlabelled and
    ' every check box was blank. Nothing caught it because every check in every
    ' fixture read a VALUE or some other property, and an unlabelled widget
    ' raises nothing.
    '
    ' A CHECK IS WHERE IT IS VISIBLE: its caption IS its `text`, so the
    ' property read says whether the label arrived. A field's label becomes a
    ' separate QLabel, which has no name and so cannot be read back -- stated
    ' here rather than left as a silent gap, and the `cells` tier asserts the
    ' field case through GEOMETRY instead, a label taking a column of its own.
    check("a check box's `label:` reached it as its text",
          qt.read(customer, "hold", "text"), "On hold")

    ' AN ABSENT FIELD LEAVES THE PART EMPTY and does not raise -- the same
    ' answer `r.missing` gives on a record, and what a form filling in a NEW
    ' record needs, every field there being absent until somebody types.
    check("a field whose name the record lacks starts empty",
          qt.read(customer, "name"), "")
    check("writing reaches it", qt.write(customer, "name", "Ada Lovelace"), true)
    check("and reads back", qt.read(customer, "name"), "Ada Lovelace")

    ' MONEY THROUGH THE ONE RENDERER (PLAT-RENDER), so a typed value reaches a
    ' widget in the words the language itself uses.
    m {USD}= "1250.50"
    check("money is written in gBASIC's own words", qt.write(customer, "limit", m), true)
    check("and reads back as that text", qt.read(customer, "limit"), "1250.50")

    check("a check box starts clear", qt.read(customer, "hold"), "false")
    check("and can be set", qt.write(customer, "hold", true), true)
    check("reading a box answers its state", qt.read(customer, "hold"), "true")

    ' THE RECORD FOLLOWS THE WIDGETS, GATHERED FROM BOTH FORMS. `cust` is
    ' edited by two forms here -- one in the column, one inside the `group` --
    ' which is how a long form is laid out, and the fold must therefore visit
    ' every form carrying that subject rather than the first. Nothing above
    ' this point would notice if it visited only one.
    back = qt.subject(customer, "cust")
    check("a field untouched by the fold keeps its captured value", back.id, "A-1000")
    check("the first form's field follows the widget", back.name, "Ada Lovelace")
    ' GUARDED WITH `has`, because a fold that visited only the FIRST form
    ' leaves `limit` absent -- and `back.limit` on a record that lacks it
    ' RAISES, which would kill the fixture and hide the mismatch that had
    ' already named the problem. run_discovery.sh records that exact trap.
    check("the SECOND form's field follows too",
          has(back, "limit") and string(back.limit) = "1250.50", true)
    ' THE KIND IS KEPT, which the obvious implementation gets wrong: a widget
    ' holds TEXT, so folding back blindly makes `limit` a string that still
    ' PRINTS as 1250.50 -- which is why the check is on `type` and not only on
    ' the digits, the same trap the finio amount tripwire records.
    check("and `type: {USD}` folds back AS money",
          has(back, "limit") and type(back.limit) = "money", true)
    check("a check box folds back as a boolean",
          has(back, "hold") and type(back.hold) = "boolean", true)
    check("with the state it is actually in",
          has(back, "hold") and back.hold = true, true)

    ' THE SIGNAL. `click` is invoked BY NAME through QMetaObject, which emits
    ' the real `clicked` signal through the real connection -- so what is being
    ' tested is the whole path and not a short cut around it.
    check("invoking a method by name", qt.invoke(customer, "save", "click"), true)
    print "after click"

    ' REFUSALS, each beside a legal neighbour above it.
    on error goto next
    x = qt.read(customer, "nosuchpart")
    if error then
        print "refused a part that does not exist: " + error.message
    else
        print "MISMATCH reading an absent part was allowed"
    end if

    on error goto next
    x = qt.invoke(customer, "save", "nosuchmethod")
    if error then
        print "refused a method that does not exist: " + error.message
    else
        print "MISMATCH invoking an absent method was allowed"
    end if

    on error goto next
    x = qt.open(customer)
    if error then
        print "refused opening the same window twice: " + error.message
    else
        print "MISMATCH the same window opened twice"
    end if

    print "done"

    ' ------------------------------------------------- A RULE IN A LAYOUT
    ' THE ORIENTATION IS THE ONE THING THAT CAN BE BUILT WRONG, and it is
    ' asserted as a DIFFERENCE between the two: a column is divided by a
    ' HORIZONTAL line and a row by a VERTICAL one, which is the opposite of
    ' what the direction's name suggests until you picture it. Either reading
    ' alone passes on a backend that hardcodes one shape.
    check("a column's rule is horizontal",
          qt.read(customer, "rule", "frameShape"), "HLine")
    check("and a row's divider is vertical",
          qt.read(customer, "bar", "frameShape"), "VLine")
    ' AND IT IS A REAL WIDGET, which is why it may be named at all: Studio
    ' styles its own rule, so one that could not be addressed could not be
    ' themed.
    qt.set(customer, "rule", "styleSheet", "color: #808080")
    check("a rule can be themed like anything else",
          qt.read(customer, "rule", "styleSheet"), "color: #808080")

end program
