# Qt cookbook

Worked recipes for the `qt` module — a declarative `window` block rendered with
Qt 6 Widgets.

**This page cannot lie.** Every code block below is a real file under
`examples/qt_cookbook/`, and every output block is that file's actual stdout,
compared byte for byte by `tests/run_qt_cookbook.sh`.

Design and rationale: [qt_gui_design.md](qt_gui_design.md). The verb and
vocabulary surface: [reference.md](reference.md)'s Qt section. One example per
aspect, with longer commentary: `examples/qt/`. The language tour's chapter:
[tutorial.md](tutorial.md) § Windows (Qt).

Run any recipe yourself:

```
QT_QPA_PLATFORM=offscreen GBASIC_PATH=stdlib ./gbasic examples/qt_cookbook/01_first_window.bas
```

---

## Two things to know before reading any recipe

**`QT_QPA_PLATFORM=offscreen` is not a test-only arrangement.** It is how a
gBASIC GUI program renders on a headless server, and it is what lets every
recipe here build real widgets, interrogate them, and print a golden with
nothing on screen.

**No recipe calls `qt.run()`.** A real program ends with it: `qt.run()` hands Qt
the thread until something calls `qt.stop()`, and gBASIC's own work — a timer, a
webserver, an actor mailbox — is still serviced while it holds it. A *recipe*
would simply hang there, with nobody to click anything, so each one fires what a
person would have clicked with `qt.invoke` and then stops. That is also the
`check` idiom every program in `examples/qt/` carries, and it is worth writing
into your own programs as you go.

---

## A first window

A window is **declared**, not assembled. The block binds a name to an
introspectable record and costs no display — which is why `qt.check` can apply
its rules on a text-only machine, and why `qt.open` runs that same check before
it builds anything.

`qt.can_display()` **answers rather than raising**, including on a build with no
Qt at all, so a program degrades instead of dying. That guard is the idiom
rather than a nicety: without a display Qt itself aborts *uncatchably*, and a
crash takes buffered output with it, so the failure would read as line 1.

<!--CODE:01_first_window-->

```basic
' A FIRST WINDOW: declare it, open it, read and write its parts.
'
' A window is DECLARED. The block below binds a name to an introspectable
' record and costs no display -- which is why `qt.check` can apply its rules on
' a text-only machine, and why `qt.open` runs that same check before building
' anything.
window greeter( title: "Greeter", width: 360, height: 160 )
    column( margin: 10, spacing: 8 )
        field who( label: "Your name" )
        row
            stretch()
            button hello( text: "Say hello" )
        end row
        text said( label: "" )
    end column

    on hello.clicked
        qt.write(greeter, "said", "Hello, " + qt.read(greeter, "who") + "!")
    end on
end window

program main(args)
    ' ANSWERS RATHER THAN RAISING, including on a build with no Qt at all --
    ' so a program degrades instead of dying. Without a display Qt itself
    ' aborts uncatchably, which is why this guard is the idiom.
    if not qt.can_display() then
        print "no display"
        return
    end if

    print "the declaration passes its own rules: " + string(qt.check(greeter))
    qt.open(greeter)

    ' EVERY NAMED PART IS REACHABLE BY NAME.
    qt.write(greeter, "who", "Ada")
    print "the field holds:   " + qt.read(greeter, "who")

    ' A real program ends with `qt.run()`, which hands Qt the thread until
    ' something calls `qt.stop()`. A recipe fires the button itself instead, so
    ' it finishes without anybody clicking.
    qt.invoke(greeter, "hello", "click")
    print "after the click:   " + qt.read(greeter, "said")

    ' ANY QT PROPERTY, BY NAME, IN THE KIND QT DECLARES IT.
    print "the button's text: " + qt.read(greeter, "hello", "text")
    print "the field accepts:  " + string(qt.read(greeter, "who", "maxLength")) + " characters"
    qt.stop()
end program
```

<!--OUT:01_first_window-->

```
the declaration passes its own rules: true
the field holds:   Ada
after the click:   Hello, Ada!
the button's text: Say hello
the field accepts:  32767 characters
```

---

## A form is a record on screen

Most business screens are a record. Say so, and nothing populates a widget
by hand. `qt.subject` reads the widgets **at the moment you ask** and folds them
into the record the form captured at open — nothing is tracked between, so no
change can be missed.

Each field **keeps its kind**: `limit` comes back as money because the field
declared `type: {USD}`, not as the text the widget holds. And the caller's own
record is untouched, because a gBASIC record is a *value*.

<!--CODE:02_form_and_record-->

```basic
' A FORM IS A RECORD ON SCREEN. Say so, and nothing populates a widget by hand.
'
' `form to edit cust` takes `cust` from the frame that opens the window and
' fills every part from the field of the same name. `form to show cust` is the
' same thing one way: the parts are read-only and the record never follows.
window customer( title: "Customer", width: 420, height: 240 )
    form to edit cust
        field name(  label: "Name" )
        field limit( label: "Credit limit", type: {USD} )
        check hold(  label: "On hold" )
        memo  notes( label: "" )
    end form
end window

program main(args)
    if not qt.can_display() then
        print "no display"
        return
    end if

    ' THE SUBJECT LIVES IN THIS FRAME. The form reads it at open.
    cust = { name: "Ada Lovelace", limit: {USD}2500.00, hold: false,
             notes: "Pays on time." }
    qt.open(customer)

    print "the form filled itself:"
    print "  name:    " + qt.read(customer, "name")
    print "  limit:   " + qt.read(customer, "limit")
    print "  on hold: " + string(qt.read(customer, "hold"))

    ' A PERSON EDITS; here the program does the typing.
    qt.write(customer, "limit", {USD}4000.00)
    qt.write(customer, "hold", true)

    ' AND THE RECORD IS FOLDED BACK ON DEMAND -- read at the moment you ask,
    ' never tracked between, so no change can be missed. Each field KEEPS ITS
    ' KIND: `limit` comes back as money because the field declared
    ' `type: {USD}`, not as the text the widget holds.
    back = qt.subject(customer, "cust")
    print "after editing:"
    print "  limit:   " + string(back.limit) + "  (type " + type(back.limit) + ")"
    print "  on hold: " + string(back.hold) + "  (type " + type(back.hold) + ")"
    print "  notes:   " + back.notes
    ' THE CALLER'S OWN RECORD IS UNTOUCHED, because a gBASIC record is a VALUE.
    print "  and `cust` itself still says " + string(cust.limit)
    qt.stop()
end program
```

<!--OUT:02_form_and_record-->

```
the form filled itself:
  name:    Ada Lovelace
  limit:   2500.00
  on hold: false
after editing:
  limit:   4000.00  (type money)
  on hold: true  (type boolean)
  notes:   Pays on time.
  and `cust` itself still says 2500.00
```

---

## Where a handler keeps state

**The thing every author hits first.** gBASIC has no closures, and a
handler fires beneath `qt.run()`. It *can read* a variable in `main` and it
*cannot rebind* one — a plain assignment there silently makes a handler-local,
and warning **2104** says so and names the remedy.

So a stateful window keeps its state in **one record**, and handlers mutate its
fields: `g.count = ...` reaches `main`'s record because a field write resolves
through the frame chain. A bare `count = count + 1` would not.

<!--CODE:03_state_in_handlers-->

```basic
' WHERE A HANDLER KEEPS STATE -- the one thing every author hits first.
'
' gBASIC has no closures, and a handler fires beneath `qt.run()`. It CAN READ a
' variable in `main` and it CANNOT REBIND one: a plain assignment there
' silently makes a handler-local instead, and warning 2104 says so and names
' the remedy. So a stateful window keeps its state in ONE RECORD, and handlers
' mutate its fields -- `g.count = ...` reaches main's record because a field
' write resolves through the frame chain.
window tally( title: "Tally", width: 280, height: 150 )
    column( margin: 10, spacing: 6 )
        text shown( label: "0" )
        row
            button up(    text: "Add one" )
            button reset( text: "Reset" )
        end row
    end column

    on up.clicked
        g.count = g.count + 1
        show_count()
    end on
    on reset.clicked
        g.count = 0
        show_count()
    end on
end window

' A FUNCTION CALLED FROM A HANDLER can read the same record and write widgets.
' It must `return nothing`, or warning 2101 fires on the discarded result.
function show_count()
    qt.write(tally, "shown", string(g.count) + " so far")
    return nothing
end function

program main(args)
    ' ONE RECORD, declared before the window opens.
    g = { count: 0 }

    if not qt.can_display() then
        print "no display"
        return
    end if
    qt.open(tally)

    qt.invoke(tally, "up", "click")
    qt.invoke(tally, "up", "click")
    qt.invoke(tally, "up", "click")
    print "after three clicks the widget says: " + qt.read(tally, "shown")
    ' AND MAIN CAN SEE IT, which is the whole point: the handler mutated the
    ' record this frame holds.
    print "and main's own record says:        " + string(g.count)

    qt.invoke(tally, "reset", "click")
    print "after reset:                       " + string(g.count)
    qt.stop()
end program
```

<!--OUT:03_state_in_handlers-->

```
after three clicks the widget says: 3 so far
and main's own record says:        3
after reset:                       0
```

---

## Laying a screen out

An author never writes a coordinate. `row` and `column` pack; `stretch()`
absorbs the leftover space, which is why this vocabulary carries **no alignment
properties at all** — to push something right, put a stretch to its left.

`cells` is the one container with coordinates, and it *aligns* columns across
its rows, which a row of rows cannot. `qt.at` asks the **layout** rather than
any record of where things were put, so it reports where a widget really is —
and that is how you discover a labelled `field` takes **two** cells.

<!--CODE:04_layout-->

```basic
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
```

<!--OUT:04_layout-->

```
code sits at row 0, column 1
qty  sits at row 0, column 3
note sits at row 1, column 1 and spans 3
a labelled field takes two cells, which is why code is at column 1
```

---

## A list and the record under it

The commonest shape a business screen takes. A `table` is **named** where a
form is not: a table shows many rows, so the table is what a handler and
`qt.selected` address. Its subject is a *list*, and each `field` is a **column**
read from every element.

A heading comes from `label:` and a cell from `name:` — two different sources,
which makes the heading an independent check that the columns are mapped the way
the declaration says.

**And note what the handler does:** it mutates the subject's *fields* and then
refreshes. It cannot *rebind* `chosen`, so writing `chosen.who` is what reaches
the record `main` holds.

<!--CODE:05_a_list-->

```basic
' A LIST AND THE RECORD UNDER IT -- the commonest shape a business screen
' takes, and the one `table` was designed for.
'
' A `table` is NAMED where a form is not, because a table shows MANY rows and
' the table itself is what a handler and `qt.selected` address. Its subject is
' a LIST, and each `field` inside is a COLUMN read from every element -- the
' column's `name` says which field, its `label` says what the heading reads.
window orders( title: "Orders", width: 620, height: 360 )
    column( margin: 10, spacing: 8 )
        table lines to show rows
            field placed( label: "Date" )
            field who(    label: "Customer" )
            field total(  label: "Total" )
        end table

        group "The chosen order"
            form to show chosen
                field who(   label: "Customer" )
                field total( label: "Total" )
            end form
        end group
    end column

    on lines.itemSelectionChanged
        pick()
    end on
end window

function pick()
    at = qt.selected(orders, "lines")
    if is_unknown(at) then
        return nothing
    end if
    row = g.rows[at]
    ' MUTATE THE SUBJECT'S FIELDS, NEVER REBIND IT. A handler cannot rebind
    ' `chosen` -- gBASIC has no closures, so the assignment would make a
    ' handler-local and warning 2104 would say so -- but writing its FIELDS
    ' reaches the record `main` holds. `qt.refresh` then redraws the form from
    ' it, addressed by the form's SUBJECT because a form has no name.
    chosen.who = row.who
    chosen.total = row.total
    drew = qt.refresh(orders, "chosen")
    return nothing
end function

program main(args)
    g = { rows: [] }
    g.rows = [ { placed: "2026-03-01", who: "Ada Lovelace",   total: {USD}420.00 },
               { placed: "2026-03-04", who: "Alan Turing",    total: {USD}98.50 },
               { placed: "2026-03-09", who: "Grace Hopper",   total: {USD}1210.00 } ]


    if not qt.can_display() then
        print "no display"
        return
    end if
    ' THE SUBJECTS ARE READ AT OPEN, so both are in place before the window is.
    rows = g.rows
    chosen = g.rows[0]
    qt.open(orders)

    print "the table drew " + string(len(g.rows)) + " rows"
    ' A HEADING COMES FROM `label:` AND A CELL FROM `name:` -- two different
    ' sources, which is what makes the heading an independent check that the
    ' columns are mapped the way the declaration says.
    print "heading 1 is:      " + qt.heading(orders, "lines", 1)
    print "cell (1, who) is:  " + qt.cell(orders, "lines", 1, "who")

    ' CHOOSING A ROW FROM THE PROGRAM fires the same handler a click would.
    qt.select(orders, "lines", 2)
    print "chosen row:        " + string(qt.selected(orders, "lines"))
    print "the form now says: " + qt.read(orders, "who")
    print "               and " + qt.read(orders, "total")
    qt.stop()
end program
```

<!--OUT:05_a_list-->

```
the table drew 3 rows
heading 1 is:      Customer
cell (1, who) is:  Alan Turing
chosen row:        2
the form now says: Grace Hopper
               and 1210.00
```

---

## Menus, shortcuts and submenus

These cost **no grammar at all** — every shape was already a production the
vocabulary had, and only the words were new. An `item` is a `QAction` rather
than a widget, which is why `shortcut:`, `checkable:` and `enabled:` work with
nothing in the backend naming them.

A menu belongs to the **window**, never to a layout, and is refused inside one:
Qt would place a menu bar wherever the layout said. `qt.menus` is the only thing
that can witness the **structure**, because the parts registry is flat and a bar
is a tree — an item in the wrong menu is invisible to a lookup by name. A
separator answers `unknown`, not `""`: it *has* no caption.

<!--CODE:06_menus-->

```basic
' A MENU BAR, SUBMENUS, SEPARATORS AND SHORTCUTS -- and it cost no grammar.
'
' Every shape here was already a production the vocabulary had: `menu "File"` is
' what `group "Credit"` uses, `item open(...)` is the named leaf, `separator()`
' is `stretch()`, and a submenu is the same production once more.
'
' A MENU BELONGS TO THE WINDOW, not to a layout: putting one inside a `row`
' would render a menu bar in the middle of the screen, so it is refused.
' An ITEM IS A QAction rather than a widget, which is why `shortcut:`,
' `checkable:` and `enabled:` all work with nothing in the backend naming them
' -- they are ordinary properties reached by name.
window editor( title: "Menus", width: 480, height: 300 )

    menu "File"
        item act_new(  label: "New",  shortcut: "Ctrl+N" )
        item act_open( label: "Open", shortcut: "Ctrl+O" )
        separator()
        ' DISABLED IN THE DECLARATION, because there is nothing to save yet.
        item act_save( label: "Save", shortcut: "Ctrl+S", enabled: false )
    end menu

    menu "View"
        item wrap( label: "Wrap long lines", checkable: true, checked: true )
        menu "Theme"
            item light( label: "Light", checkable: true, checked: true )
            item dark(  label: "Dark",  checkable: true )
        end menu
    end menu

    column( margin: 10 )
        field title( label: "Title" )
        memo  body(  label: "" )
    end column

    ' TYPING ENABLES Save, which is the ordinary reason a menu item's state
    ' changes: a fact about the program rather than about the declaration.
    on title.textChanged
        qt.set(editor, "act_save", "enabled", len(qt.read(editor, "title")) > 0)
    end on
end window

program main(args)
    if not qt.can_display() then
        print "no display"
        return
    end if
    qt.open(editor)

    ' THE BAR IS A TREE AND THE PARTS REGISTRY IS FLAT, so `qt.menus` is the
    ' only thing that can witness the STRUCTURE -- an item in the wrong menu or
    ' a submenu hung off the bar is invisible to a lookup by name.
    print "the bar:        " + string(qt.menus(editor))
    print "File holds:     " + string(qt.menus(editor, 0))
    print "View holds:     " + string(qt.menus(editor, 1))
    ' A SEPARATOR ANSWERS `unknown`, NOT "": it HAS no caption, where an item
    ' with `label: ""` has an empty one.
    print ("entry 2 of File is a separator: " +
           string(is_unknown(qt.menus(editor, 0)[2])))

    print "Save's shortcut: " + qt.read(editor, "act_save", "shortcut")
    print "Save enabled:    " + string(qt.read(editor, "act_save", "enabled"))
    qt.write(editor, "title", "notes.txt")
    print "after typing:    " + string(qt.read(editor, "act_save", "enabled"))
    qt.stop()
end program
```

<!--OUT:06_menus-->

```
the bar:        ["File","View"]
File holds:     ["New","Open",unknown,"Save"]
View holds:     ["Wrap long lines","Theme"]
entry 2 of File is a separator: true
Save's shortcut: Ctrl+S
Save enabled:    false
after typing:    true
```

---

## Choosing one of a list

A `field` means typing an id the author has to remember. A `choice` shows
the names while the program keeps the ids, and that translation is the whole
reason it exists.

A declared `options:` list is for the fixed case — a direction, a status — and
`qt.options` is for anything that moves. BAG's area editor needs both in one
window, which is the measurement that said support both rather than one.
`qt.write` on a choice **refuses a name that is not in the list** rather than
silently doing nothing, which is what Qt's own `setCurrentText` does.

<!--CODE:07_choosing-->

```basic
' CHOOSING ONE OF A LIST. A `field` means typing an id the author has to
' remember; a `choice` shows the names and the program keeps the ids.
'
' ITS ITEMS COME FROM DATA, so a declared `options:` list is for the fixed case
' -- a direction, a status -- and `qt.options` is for anything that moves. BAG's
' area editor needs both in one window, which is the measurement that said
' support both rather than one.
window move_form( title: "Choosing", width: 420, height: 220 )
    form to edit plan
        ' FIXED, so it is typed out.
        choice heading( label: "Direction",
                        options: ["north", "south", "east", "west"] )
        ' NOT FIXED: filled from data below.
        choice leads_to( label: "Leads to" )
    end form
end window

program main(args)
    ' THE DATA AND ITS IDS. The dropdown shows names; the record keeps ids,
    ' which is the translation a `choice` exists to make.
    areas = [ { id: 1, name: "The Cellar" },
              { id: 2, name: "The Garden" },
              { id: 3, name: "The Tunnel" } ]

    if not qt.can_display() then
        print "no display"
        return
    end if
    plan = { heading: "north", leads_to: "" }
    qt.open(move_form)

    ' A SENTINEL FIRST, so "nowhere" is choosable rather than absent.
    names = ["(nowhere)"]
    for each a in areas
        names = append(names, a.name)
    end for
    shown = qt.options(move_form, "leads_to", names)
    print "the dropdown offers " + string(shown) + " choices"

    ' CHOOSING BY NAME. `qt.write` on a choice REFUSES a name that is not in
    ' the list rather than silently doing nothing, which is what Qt's own
    ' setCurrentText does on an uneditable box.
    qt.write(move_form, "leads_to", "The Garden")
    print "the box shows:      " + qt.read(move_form, "leads_to")

    ' AND BACK TO AN ID, which is the program's half of the translation.
    chosen = qt.read(move_form, "leads_to")
    goes_to = 0
    for each a in areas
        if a.name = chosen then
            goes_to = a.id
        end if
    end for
    print "which is area id:   " + string(goes_to)
    print "the direction is:   " + qt.read(move_form, "heading")
    qt.stop()
end program
```

<!--OUT:07_choosing-->

```
the dropdown offers 4 choices
the box shows:      The Garden
which is area id:   2
the direction is:   north
```

---

## Keeping the screen current

A declaration is read **once**, when the window opens — so a table built
from a list that later grows would show the old list for ever. `qt.refresh`
re-reads a subject and redraws from it, addressing a **table by its name** and a
**form by its subject**, because a form has no name.

**The important half is how the handler changes the data.** `append` mutates
*in place* and resolves through the frame chain, so it grows the very list
`main` holds. A snapshot (`rows = g.rows`) would leave the refresh re-reading an
unchanged copy — which is a real mistake this recipe was written by making.

<!--CODE:08_keeping_current-->

```basic
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
        drew = qt.refresh(board, "jobs")
        return nothing
    end on

    on done.clicked
        at = qt.selected(board, "jobs")
        if not is_unknown(at) then
            tasks[at].state = "done"
            drew = qt.refresh(board, "jobs")
            ' AND THE FORM, by its SUBJECT. Mutate the subject's FIELDS rather
            ' than rebinding it: a handler cannot rebind `current`.
            current.what = tasks[at].what
            current.state = tasks[at].state
            drew = qt.refresh(board, "current")
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
    qt.invoke(board, "add", "click")
    qt.invoke(board, "add", "click")
    print "after adding twice:   " + string(len(tasks)) + " rows"
    print "the new last row is:  " + qt.cell(board, "jobs", 3, "what")

    qt.select(board, "jobs", 1)
    qt.invoke(board, "done", "click")
    print "row 1's state is now: " + qt.cell(board, "jobs", 1, "state")
    print "and the form says:    " + qt.read(board, "state")
    qt.stop()
end program
```

<!--OUT:08_keeping_current-->

```
the table starts with 2 rows
after adding twice:   4 rows
the new last row is:  Task 4
row 1's state is now: done
and the form says:    done
```

---

## A code editor

`memo` holds prose; code differs in four ways, none of which is a Qt
property — so `source` is a word of its own. **A highlighter is not a property**
either: it is an object attached to a document, so nothing a by-name lookup can
reach, which is why it needed a word at all.

`language:` names a **definition**, never a file extension: `*.bas` is claimed
by FreeBASIC at the same priority as gBASIC, so a lookup by extension picks
FreeBASIC with no error and highlights plausibly and wrongly. An absent
definition is a **warning**, not a refusal — the editor still works, and an
unhighlighted one looks entirely ordinary, so there is nothing on screen to
notice.

From the source tree this needs `GBASIC_SYNTAX_PATH=editors/kate`, the same deal
as `GBASIC_PATH=stdlib`; after `make install` it is unnecessary.

<!--CODE:09_code_editor-->

```basic
' A CODE EDITOR. `memo` holds prose; code is different in four ways, none of
' which is a Qt property -- so `source` is a word of its own rather than an
' option on `memo`.
'
' A HIGHLIGHTER IS NOT A PROPERTY, which is why it needed a word at all: it is
' an object attached to a document, so nothing a by-name property lookup can
' reach. `language:` names a DEFINITION, never a file extension -- `*.bas` is
' claimed by FreeBASIC at the same priority as gBASIC, so a lookup by extension
' picks FreeBASIC with no error and highlights plausibly and wrongly.
window pad( title: "Editor", width: 640, height: 400 )
    column( margin: 8, spacing: 6 )
        text heading( label: "A source widget, highlighted by its name" )
        source code( language: "gBASIC", theme: "dark" )
        memo  prose( label: "" )
    end column
end window

program main(args)
    if not qt.can_display() then
        print "no display"
        return
    end if
    qt.open(pad)

    sample = ("' a comment" + chr(10) + "program main()" + chr(10) +
              "    x = 42" + chr(10) + "end program")
    qt.write(pad, "code", sample)
    qt.write(pad, "prose", sample)

    ' WHAT IS ACTUALLY HIGHLIGHTING IT. `qt.highlighting` answers a record, or
    ' `unknown` when nothing is -- and an absent definition is a WARNING rather
    ' than a refusal, because the editor still works and an unhighlighted one
    ' looks entirely ordinary: the theme's normal text is applied to every line
    ' either way, so there is nothing on screen to notice.
    h = qt.highlighting(pad, "code")
    if is_unknown(h) then
        print "highlighting:  none installed (the editor still works)"
    else
        print "highlighting:  " + h.language
    end if
    print "this build can: " + string(qt.can_highlight())

    ' A `source` IS A `memo` WITH FOUR DIFFERENCES, and they read better beside
    ' a memo than as values.
    print "-- against a memo in the same window"
    print ("wrap:          source=" +
           string(qt.read(pad, "code", "lineWrapMode")) + "  memo=" +
           string(qt.read(pad, "prose", "lineWrapMode")))
    print ("narrower tab:  " + string(qt.read(pad, "code", "tabStopDistance") <
                                      qt.read(pad, "prose", "tabStopDistance")))
    print ("fonts differ:  " + string(qt.read(pad, "code", "font") !=
                                      qt.read(pad, "prose", "font")))
    print "memo has none: " + string(is_unknown(qt.highlighting(pad, "prose")))
    print "text survives: " + string(len(qt.read(pad, "code")) = len(sample))
    qt.stop()
end program
```

<!--OUT:09_code_editor-->

```
highlighting:  gBASIC
this build can: true
-- against a memo in the same window
wrap:          source=NoWrap  memo=WidgetWidth
narrower tab:  true
fonts differ:  true
memo has none: true
text survives: true
```

---

## A window built from data

The one thing a `window` block cannot write, because a declaration has no
loop in it. **There is no second API**: the block binds a *record*, and
`qt.open` asks only that a record carry `kind: "window"` — so building that
record is the other door, and binding, handlers, properties and `qt.at` all work
exactly as they do for a declared window.

`qt.check` gives a built window the declaration's **own load-time rules**, which
is what makes building one safe: a part with no name, two parts sharing one, a
misspelled word or a handler naming nothing are refused with the same sentences
— and `qt.open` runs the check itself, so a validator nobody called cannot be
skipped.

<!--CODE:10_built_from_data-->

```basic
' A WINDOW BUILT FROM DATA -- the one thing a `window` block cannot write,
' because a declaration has no loop in it.
'
' THERE IS NO SECOND API. The block binds a RECORD, and `qt.open` asks only
' that a record carry `kind: "window"` -- so building that record is the other
' door, and everything else (binding, handlers, properties, `qt.at`) works
' exactly as it does for a declared window.
'
' `qt.check` GIVES A BUILT WINDOW THE DECLARATION'S OWN LOAD-TIME RULES, which
' is what makes building one safe: a part with no name, two parts sharing one,
' a misspelled word or a handler naming nothing are all refused with the same
' sentences, and `qt.open` runs the check itself so a validator nobody called
' cannot be skipped.

function row_for(line)
    ' ONE ROW OF A GRID, as a record. `cells` aligns columns across rows, which
    ' is what makes a generated table of fields line up.
    return { kind: "container", word: "row", options: {},
             items: [ { kind: "part", word: "text", name: "d" + line.id,
                        options: { label: line.what } },
                      { kind: "part", word: "field", name: "q" + line.id,
                        options: { text: string(line.qty) } } ] }
end function

function invoice_for(lines)
    rows = []
    for each l in lines
        rows = append(rows, row_for(l))
    end for
    return { kind: "window", name: "invoice",
             options: { title: "Invoice", width: 420, height: 320 },
             items: [ { kind: "container", word: "cells", options: {},
                        items: rows } ] }
end function

program main(args)
    lines = [ { id: 1, what: "Consulting", qty: 3 },
              { id: 2, what: "Travel",     qty: 1 },
              { id: 3, what: "Licence",    qty: 12 } ]

    invoice = invoice_for(lines)

    ' THE RULES APPLY BEFORE ANYTHING IS BUILT, and need no display and no Qt.
    print "the built record passes qt.check: " + string(qt.check(invoice))

    if not qt.can_display() then
        print "no display"
        return
    end if
    qt.open(invoice)

    print "it generated " + string(len(lines)) + " rows"
    print "row 0 shows:  " + qt.read(invoice, "d1")
    print "row 2 holds:  " + qt.read(invoice, "q3")
    ' AND THE GEOMETRY IS REAL, asked of the layout rather than of any record
    ' of where we put things.
    at = qt.at(invoice, "q3")
    print "q3 sits at row " + string(at.row) + ", column " + string(at.column)
    qt.stop()

    ' A WINDOW THE RULES REJECT IS REFUSED, in the declaration's own words.
    on error goto refused
    bad = { kind: "window", name: "bad", options: {},
            items: [ { kind: "container", word: "column", options: {},
                       items: [ { kind: "part", word: "field", options: {} } ] } ] }
    x = qt.check(bad)
    print "MISMATCH: a part with no name was accepted"
    return
refused:
    print "a part with no name is refused: " + error.message
    error.clear()
end program
```

<!--OUT:10_built_from_data-->

```
the built record passes qt.check: true
it generated 3 rows
row 0 shows:  Consulting
row 2 holds:  12
q3 sits at row 2, column 1
a part with no name is refused: window: 'field' needs a name -- `field id( ... )` -- because that is what a handler and a binding address (at column[0] > field[0])
```

---

## A book of pages

`tabs` holds `tab`s, and each page is given a column layout — which is why a
field, a row, a group or a whole `table` works inside one with no further
vocabulary. A `tab` **needs a caption and takes no name**; the `tabs` *is*
named, because it has state a handler can ask about where a page does not.

A `QTabWidget` has no layout of its own, so anything placed directly in one
would be **dropped** rather than mislaid — which is why a `tab` is required
around whatever goes on a page.

**A container carries a name or options, never both.** `tabs pages( ... )`
costs four reduce/reduce conflicts in the grammar and is refused, which is why
an option that moves is set at run time with `qt.set` instead. And the parts
registry is **flat** — a page is not a namespace, so a part on any page is
reachable by name whichever page is in front.

<!--CODE:11_tabs-->

```basic
' A BOOK OF PAGES. `tabs` holds `tab`s, and each page is given a column layout
' -- which is why a field, a row, a group or a whole table works inside one
' with no further vocabulary.
'
' A `tab` NEEDS A CAPTION AND TAKES NO NAME; the `tabs` IS named, because it has
' state a handler can ask about (which page is showing) where a page does not.
' A QTabWidget has no layout of its own, so anything placed directly in one
' would be DROPPED rather than mislaid -- which is why a `tab` is required.
window settings( title: "Settings", width: 520, height: 300 )
    column( margin: 10 )
        tabs pages
            tab "General"
                field who(  label: "Name" )
                field mail( label: "Email" )
            end tab
            tab "Advanced"
                check deep(  label: "Verbose logging" )
                check retry( label: "Retry on failure", checked: true )
            end tab
            tab "Limits"
                cells
                    row
                        field soft( label: "Soft", text: "100" )
                        field hard( label: "Hard", text: "500" )
                    end row
                end cells
            end tab
        end tabs
    end column
end window

program main(args)
    if not qt.can_display() then
        print "no display"
        return
    end if
    qt.open(settings)

    ' THE `tabs` IS NAMED, so its state is reachable by name like any other
    ' property -- `currentIndex` is Qt's own, and nothing in this backend knows
    ' about it.
    print "pages:         " + string(qt.read(settings, "pages", "count"))
    print "showing page:  " + string(qt.read(settings, "pages", "currentIndex"))

    ' A CONTAINER CARRIES A NAME OR OPTIONS, NEVER BOTH -- `tabs pages( ... )`
    ' costs four reduce/reduce conflicts in the grammar and is refused, which is
    ' why an option that moves is set at run time instead.
    qt.set(settings, "pages", "movable", true)
    print "movable now:   " + string(qt.read(settings, "pages", "movable"))

    qt.set(settings, "pages", "currentIndex", 2)
    print "after turning: " + string(qt.read(settings, "pages", "currentIndex"))

    ' PARTS ON ANY PAGE ARE REACHABLE BY NAME, whichever page is in front:
    ' the registry is flat, and a page is not a namespace.
    qt.write(settings, "who", "Ada")
    print "a part on page 0: " + qt.read(settings, "who")
    at = qt.at(settings, "hard")
    print ("and a grid on page 2 still has coordinates: column " +
           string(at.column))
    qt.stop()
end program
```

<!--OUT:11_tabs-->

```
pages:         3
showing page:  0
movable now:   true
after turning: 2
a part on page 0: Ada
and a grid on page 2 still has coordinates: column 3
```

---

## Theming the application, and one widget that opts out

Per-widget styling needed **nothing new**: `styleSheet` is an ordinary
`Q_PROPERTY` of every `QWidget`, so it arrives through the same by-name route
as `text` or `enabled`. What that route cannot reach is *one sheet for the
application*, because that belongs to the `QApplication` rather than to any
widget — so `qt.theme` is the verb for it.

**It needs no display.** A sheet set before any window exists is remembered and
applied when there is a `QApplication` to put it on, so a program themes first
and opens second — which means the first half of this recipe runs on a
text-only machine.

Qt resolves the cascade, so **a widget's own sheet wins** over the
application's, which is what makes an exception expressible. That is worth
asserting rather than assuming: a theme applied by *writing* onto every widget
would satisfy a check that only asked whether the app sheet was set, and would
silently flatten the exception.

<!--CODE:12_theming-->

```basic
' ONE STYLESHEET FOR THE WHOLE APPLICATION, and a widget that overrides it.
'
' PER-WIDGET STYLING NEEDED NOTHING NEW: `styleSheet` is an ordinary
' Q_PROPERTY of every QWidget, so it arrives through the same by-name route as
' `text` or `enabled`. What that route CANNOT reach is one sheet for the
' application, because that belongs to the QApplication rather than to any
' widget -- so `qt.theme` is the verb for it.
window panel( title: "Theming", width: 420, height: 220 )
    column( margin: 10, spacing: 8 )
        text heading( label: "Themed by the application" )
        field normal(  label: "Ordinary" )
        ' ITS OWN SHEET, declared. Qt resolves the cascade, so a widget's own
        ' sheet wins over the application's -- which is what makes an
        ' exception expressible at all.
        field special( label: "Its own colours",
                       styleSheet: "background: #ffd; color: #600;" )
        button close( text: "Close" )
    end column
end window

program main(args)
    ' IT NEEDS NO DISPLAY. A sheet set before any window exists is REMEMBERED
    ' and applied when there is a QApplication to put it on -- so a program
    ' themes first and opens second, and this half of the recipe runs on a
    ' text-only machine.
    qt.theme("QWidget { background: #2e3440; color: #eceff4; }")
    print ("a theme can be set before anything opens: " +
                 string(len(qt.theme()) > 0))

    if not qt.can_display() then
        print "no display"
        return
    end if
    qt.open(panel)

    ' THE APPLICATION'S SHEET IS WHAT WAS ASKED FOR.
    print ("the app sheet holds the dark background: " +
                 string(contains(qt.theme(), "#2e3440")))

    ' AND THE WIDGET THAT DECLARED ITS OWN STILL HAS IT. A theme applied by
    ' WRITING onto every widget would pass a check that only asked whether the
    ' app sheet was set, and would silently flatten this.
    print ("the ordinary field has no sheet of its own: " +
                 string(len(qt.read(panel, "normal", "styleSheet")) = 0))
    print ("the special one kept its own:               " +
                 string(contains(qt.read(panel, "special", "styleSheet"), "#ffd")))

    ' CHANGING IT AT RUN TIME is the same call. A dark and a light theme are
    ' two strings; there is no theme registry to keep in step.
    qt.theme("")
    print "and clearing it leaves nothing: " + string(len(qt.theme()) = 0)
    qt.stop()
end program
```

<!--OUT:12_theming-->

```
a theme can be set before anything opens: true
the app sheet holds the dark background: true
the ordinary field has no sheet of its own: true
the special one kept its own:               true
and clearing it leaves nothing: true
```

---

## Qt owns the thread, and gBASIC is still serviced

**The only recipe that calls `qt.run()`**, and the architectural claim the
whole backend rests on — so it is worth watching rather than taking on trust.

Why Qt's loop is the one that waits: Qt opens **nested** loops — a modal
dialog, a menu, a drag — and under the other arrangement gBASIC's own `poll()`
never runs for the dialog's whole life. Measured during the design at a 651 ms
gap over a 600 ms dialog, which is a webserver answering nothing. So gBASIC's
sources ride a Qt timer, and a `timer`, a webserver and an actor mailbox all
keep running while `qt.run()` holds the thread. **Nothing here arranges it**:
`qt.run()` is one call and `timer.every` is the same verb any other program
uses.

Two things learned by getting them wrong, both on the page rather than in a
comment: the `watch` goes **inside** `program` — at the top level beside one it
registers nothing and says nothing about it (warning **2106**) — and it works
inside because `qt.run()` *blocks* in that frame, so `main`'s frame is still
alive for the watcher to reach. And **`qt.stop()` does not cancel your
timers**, which is right (a program may have work left after its window
closes) but means a repeating timer left live keeps gBASIC's own post-main loop
running and the program never exits: measured at 197 further ticks before a
timeout killed it.

<!--CODE:13_the_event_loop-->

```basic
' QT OWNS THE THREAD AND gBASIC IS STILL SERVICED. This is the only recipe
' that calls `qt.run()`, and it is the architectural claim the whole backend
' rests on -- so it is worth watching rather than taking on trust.
'
' WHY QT'S LOOP IS THE ONE THAT WAITS: Qt opens NESTED loops -- a modal dialog,
' a menu, a drag -- and under the other arrangement gBASIC's own poll never
' runs for the dialog's whole life. Measured during the design: a 651ms gap
' over a 600ms dialog, which is a webserver answering nothing. So gBASIC's
' sources ride a Qt timer, and a `timer`, a webserver and an actor mailbox all
' keep running while `qt.run()` holds the thread.
'
' NOTHING HERE ARRANGES IT. `qt.run()` is one call and `timer.every` is the
' same verb any other program uses.
window clock( title: "Still ticking", width: 320, height: 180 )
    column( margin: 10, spacing: 6 )
        text ticks( label: "0 ticks" )
        memo log(   label: "" )
    end column
end window

program main(args)
    if not qt.can_display() then
        print "no display"
        return
    end if

    seen = 0
    qt.open(clock)
    beat = timer.every(0.1)

    ' THE `watch` GOES INSIDE `program`. At the top level beside a `program`
    ' block it registers NOTHING and says nothing about it (warning 2106), and
    ' it works here because `qt.run()` BLOCKS in this frame -- so main's frame
    ' is still alive and the watcher can reach `seen` in it.
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            seen = seen + 1
            ' ORDINARY gBASIC WRITING INTO A Qt WIDGET, from inside Qt's loop.
            qt.write(clock, "ticks", string(seen) + " ticks")
            qt.write(clock, "log",
                     qt.read(clock, "log") + "tick " + string(seen) + chr(10))
            if seen >= 3 then
                ' BOTH, AND THE CANCEL MATTERS: `qt.stop()` DOES NOT CANCEL
                ' YOUR TIMERS, and it should not -- a program may have work
                ' left after its window closes. But leave a repeating timer
                ' live and gBASIC's own post-main loop keeps running, so the
                ' program never exits. Measured during the design at 197
                ' further ticks before a timeout killed it.
                cancelled = timer.cancel(beat)
                qt.stop()
            end if
        end while
    end watch

    ' AND IT RETURNS, which is the other half: `qt.run()` gives the thread
    ' back when something calls `qt.stop()`, so a program carries on
    ' afterwards.
    rc = qt.run()
    print "ticks serviced while Qt held the thread: " + string(seen)
    print "the window reads back:                   " + qt.read(clock, "ticks")
    print("and the memo grew to lines:              " +
          string(len(split(qt.read(clock, "log"), chr(10))) - 1))
    print "qt.run returned, so the program continues"
end program
```

<!--OUT:13_the_event_loop-->

```
ticks serviced while Qt held the thread: 3
the window reads back:                   3 ticks
and the memo grew to lines:              3
qt.run returned, so the program continues
```

---
