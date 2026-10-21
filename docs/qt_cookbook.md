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
vocabulary. A `tab` **needs a caption**, so a declared one spends its single
slot there and cannot also be named; the `tabs` *is* named, because it has state
a handler can ask about. (Since §0.27 a `tab` may carry a name in a **record**,
which is what gives `qt.refresh(win, name, items)` an identity to keep a page
by — the caption being the thing that changes.)

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

## Asking before acting

A statement and a question, which is most of what a real application needs
from a dialog. `qt.confirm` answers a **boolean** rather than a button code —
two buttons is the whole of what it offers, and anything richer is a window
shown modally, which is a different thing with a layout of its own. **No is
the default button**, because a question answered by pressing Return without
reading should not be the destructive one.

**A modal dialog is modal to the whole program**, and that is worth knowing
before you build on it. Qt's loop survives the nested one — that is why
direction A was chosen — but gBASIC is not serviced while a **watcher body** is
on the stack. So:

| dialog raised | gBASIC's own sources |
|---|---|
| from `main`, outside any watcher | keep running — a `timer` fires, and can answer the box |
| **inside a watcher body** | **suspended** until it is answered |

The second is the normal case and it is wider than it looks: a `timer` body, a
webserver handler, an actor mailbox watcher and a button clicked from any of
them all run inside one. So a `timer`, a webserver and an actor mailbox all
stop until the question is answered. `tests/run_qt_dialog.sh` asserts that with
a real `server` on a real port rather than stating it, because a limitation
nobody can observe is one nobody will believe.

That row used to say *"from a handler"*. A fixture with **no handler in it**
stalls identically, which is what corrected it: two guards hold the stall, each
sufficient alone — Qt will not re-deliver the one timer gBASIC's sources ride
on while its handler is on the stack, and `watcher_drain` declines to re-enter
while a watcher body is running.

A recipe has nobody to click, so a timer answers for it — which in a real
program is how a dialog **times out**. `qt.answer` refuses when nothing is
open rather than doing nothing, since a program left waiting for somebody who
is not there is a hang and not a failure.

<!--CODE:14_asking_first-->

```basic
' ASKING BEFORE ACTING. A statement and a question, which is most of what a
' real application needs from a dialog.
'
' `qt.confirm` ANSWERS A BOOLEAN rather than a button code: two buttons is the
' whole of what it offers, and anything richer is a window shown modally, which
' is a different thing with a layout of its own. NO IS THE DEFAULT BUTTON,
' because a question answered by pressing Return without reading should not be
' the destructive one.
'
' A DIALOG IS MODAL TO THE WHOLE PROGRAM, and that is worth knowing before you
' build on it. Qt's loop survives the nested one, but gBASIC cannot re-enter
' while the interpreter is suspended inside its own handler -- so a dialog
' raised FROM A HANDLER (somebody clicked, and the program asks) suspends a
' `timer`, a webserver and an actor mailbox until it is answered. A dialog
' raised OUTSIDE a handler leaves them running, which is why the timer below
' can answer this one.
window app( title: "Asking first", width: 360, height: 170 )
    column( margin: 10, spacing: 8 )
        text state( label: "nothing has happened" )
        field who(  label: "Customer" )
    end column
end window

program main(args)
    g = { want: true, asked: 0 }

    if not qt.can_display() then
        print "no display"
        return
    end if
    qt.open(app)
    qt.write(app, "who", "Ada Lovelace")

    ' A RECIPE HAS NOBODY TO CLICK, so a timer answers for one. In a real
    ' program this is how a dialog TIMES OUT -- and `qt.answer` refuses when
    ' nothing is open rather than doing nothing, because a program left
    ' waiting for somebody who is not there is a hang and not a failure.
    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            ok = qt.answer(g.want)
        end while
    end watch

    ' THE QUESTION. Guarding a destructive act is the commonest use there is.
    g.want = false
    if qt.confirm(app, "Delete " + qt.read(app, "who") + "?") then
        qt.write(app, "state", "deleted")
    else
        qt.write(app, "state", "kept")
    end if
    print "answered no  -> " + qt.read(app, "state")

    g.want = true
    if qt.confirm(app, "Delete " + qt.read(app, "who") + "?") then
        qt.write(app, "state", "deleted")
    else
        qt.write(app, "state", "kept")
    end if
    print "answered yes -> " + qt.read(app, "state")

    ' THE STATEMENT, which has one button and so is answered either way.
    told = qt.inform(app, "Customer deleted.")
    print "inform returned " + string(told)

    cancelled = timer.cancel(beat)
    qt.stop()
end program
```

<!--OUT:14_asking_first-->

```
answered no  -> kept
answered yes -> deleted
inform returned true
```

---

## Choosing a file

Three verbs, because they ask three different questions — *which existing
file?*, *what shall I call it?*, *which folder?* — and each refuses the options
that mean nothing to it rather than ignoring them. `qt.open_file` was rejected
as a name: it reads as though it opens the file, and `f = qt.open_file(app)`
looks like it answers contents or a handle. These verbs **choose**; your
program does the reading.

**The answer is text, not a `{file}`.** Three sibling verbs would otherwise
answer two kinds, and a chooser has no declaration to read a kind from — what
Qt holds is a path. So the modifier goes where the value is used:
`read({file}path)`.

**Cancelling answers `unknown`, never `""`.** The empty string is the plausible
wrong answer: `if path = "" then` happens to work while `read({file}path)` does
not. `unknown` is what forces you to notice, and `if path then` on it *raises*
rather than quietly taking the wrong branch.

`filter:` takes text or a **list**, joined with Qt's own `;;` for you — two
semicolons meaning "or" is a Qt-ism nobody should have to know. The four
options are `title:`, `folder:`, `name:` and `filter:`, closed and refused by
name with a near-miss hint, because an ignored misspelling is a chooser that
opens in the wrong folder with the wrong filter and says nothing.
`folder:` and `name:` take a `{dir}` or `{file}` as happily as text, which the
recipe below found: `temp_dir()` answers a `{dir}`, so the most natural
spelling there is was refused before that.

A recipe has nobody to click, so a timer answers for one — and here `qt.answer`
takes the **path**, because that is what a chooser's answer is. `false` is a
cancel, which is what a timeout wants to say; `true` is refused, since it would
have to mean "accept *what?*" and guessing which file somebody meant is the one
thing this verb must not do.

<!--CODE:15_choosing_a_file-->

```basic
' CHOOSING A FILE. Three verbs, because they ask three different questions --
' which existing file, what shall I call it, which folder -- and each refuses
' the options that mean nothing to it rather than ignoring them.
'
' THE ANSWER IS TEXT, NOT A {file}. Three sibling verbs would otherwise answer
' two kinds, and a chooser has no declaration to read a kind from: what Qt
' holds is a path. So `{file}` goes where the value is used, which is one
' inline modifier.
'
' CANCELLING ANSWERS `unknown`, NEVER "". The empty string is the plausible
' wrong answer -- `if path = "" then` happens to work while `read({file}path)`
' does not -- so `unknown` is what forces you to notice, and `if path then` on
' it raises rather than quietly taking the wrong branch.
'
' A RECIPE HAS NOBODY TO CLICK, so a timer answers for one. `qt.answer` takes
' the PATH here rather than true or false, because that is what a chooser's
' answer is; `false` is a cancel, and `true` is refused, since it would have to
' mean "accept what?".
'
' THE SCRATCH DIRECTORY IS THE RECIPE'S OWN, so this prints the same thing on
' every machine. A real program would start wherever the user last was.
window app( title: "Choosing a file", width: 380, height: 160 )
    column( margin: 10, spacing: 8 )
        text state( label: "nothing chosen yet" )
    end column
end window

program main(args)
    g = { mode: "path", want: "" }

    if not qt.can_display() then
        print "no display"
        return
    end if

    work = temp_dir()
    report = write({file}(work + "/report.txt"), "the quarterly numbers" + chr(10))

    qt.open(app)

    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            if g.mode = "cancel" then
                ok = qt.answer(false)
            else
                ok = qt.answer(g.want)
            end if
        end while
    end watch

    ' WHICH EXISTING FILE. A list of filters is joined with Qt's own `;;` for
    ' you, which is a spelling nobody should have to know.
    g.want = work + "/report.txt"
    chosen = qt.choose_file(app, { title: "Open a report", folder: work,
        filter: ["Text files (*.txt)", "All files (*)"] })
    print "chose    " + file_name(chosen)
    print "and it reads " + string(len(read({file}chosen))) + " bytes"

    ' WHAT SHALL I CALL IT. The file need not exist yet, which is the whole
    ' difference from the verb above.
    g.want = work + "/summary.txt"
    saving = qt.choose_save(app, { title: "Save as", folder: work,
        name: "summary.txt" })
    print ("saving   " + file_name(saving) + ", exists already: " +
        string(exists({file}saving)))
    done = write({file}saving, "a summary" + chr(10))

    ' WHICH FOLDER.
    g.want = work
    folder = qt.choose_folder(app, { title: "Where to put things",
        folder: work })
    print "folder   has " + string(count(files({dir}folder, "*.txt"))) + " text files"

    ' AND WHAT A CANCEL LOOKS LIKE, which is the branch every program needs.
    g.mode = "cancel"
    none = qt.choose_file(app, { folder: work })
    if is_unknown(none) then
        print "cancelled, so nothing to open"
    else
        print "cancelled, but got " + none
    end if

    cancelled = timer.cancel(beat)
    gone = remove_tree({dir}work)
    qt.stop()
end program
```

<!--OUT:15_choosing_a_file-->

```
chose    report.txt
and it reads 22 bytes
saving   summary.txt, exists already: false
folder   has 2 text files
cancelled, so nothing to open
```

---

## Closing a window

`qt.close(win)` is the other half of `qt.open`, and **a close is bookkeeping
rather than a hide**. Qt leaves a dismissed window alive and still holding its
values — it does not set `WA_DeleteOnClose` — so a program that went on reading
one would be reading values off a screen nobody is looking at, and nothing
would raise. After a close, `qt.read`, `qt.write` and `qt.subject` all refuse by
name, and `qt.is_open` answers `false`.

**Which makes `on <win>.closed` the place to save.** It fires *before* the
window stops being open, so it is the one moment a program can still read what
somebody typed — and it fires however the window closed, including the window
manager's own button, which is the case you cannot test and your users will do
every day. A program with nothing to save needs no handler; one that has
something has exactly one place to put it.

**The window is a part of itself.** Its own name reaches it in the same by-name
table every part lives in, so `qt.read(app, "app", "windowTitle")`,
`qt.set(app, "app", ...)` and `qt.invoke(app, "app", "close")` all work with no
extra vocabulary. The one cost is a collision: **a part may not take the
window's name**, and is refused by name at load time, because it would
otherwise register over the window and `on app.closed` would reach a widget.

**`qt.is_open` answers where the others refuse**, like `qt.can_display`,
because a window can close *without the program asking*. Once the person can
dismiss one, every other verb starts refusing for a reason that is nobody's
mistake, and a program with no way to ask would have to wrap each call in
`on error` to learn something it could simply be told.

**And `qt.stop()` is not a close.** It means *give me the thread back* and
nothing more: the window stays open, stays readable, and no `closed` handler
fires. Qt's own `quit()` closes every top-level window, which is why gBASIC
does not use it — asking for the thread back should not dismiss what is on
screen.

**Reopening builds a fresh window**, so a bound form captures its subject again
and what was typed before the close is gone. A close is not a hide you can
undo.

<!--CODE:16_closing_a_window-->

```basic
' CLOSING A WINDOW -- and knowing when the person closed it for you.
'
' `qt.close(win)` IS THE OTHER HALF OF `qt.open`, and a close is BOOKKEEPING
' rather than a hide: Qt leaves a dismissed window alive and still holding its
' values, so a program that went on reading one would be reading off a screen
' nobody is looking at. After a close, `qt.read`, `qt.write` and `qt.subject`
' all refuse by name and `qt.is_open` answers false.
'
' WHICH MAKES `on <win>.closed` THE PLACE TO SAVE. It fires BEFORE the window
' stops being open, so it is the one moment a program can still read what
' somebody typed -- and it fires however the window closed, including the
' window manager's own button, which is the case you cannot test and your users
' will do every day.
'
' THE WINDOW IS A PART OF ITSELF. Its own name reaches it in the same by-name
' table every part lives in, so `qt.read(app, "app", "windowTitle")`,
' `qt.set(app, "app", ...)` and `qt.invoke(app, "app", "close")` all work with
' no extra vocabulary. The one cost: a part may NOT take the window's name, and
' is refused by name if it tries.
window app( title: "Closing", width: 360, height: 180 )
    column( margin: 10, spacing: 8 )
        form to edit cust
            field who(   label: "Customer" )
            field limit( label: "Limit", type: {USD} )
        end form
    end column

    ' SAVE HERE. `g` is one record whose fields handlers mutate, which is where
    ' GUI state lives in gBASIC -- a handler cannot rebind a variable in `main`
    ' (warning 2104 says so and names this remedy), but it can write a field.
    on app.closed
        g.saved  = qt.subject(app, "cust")
        g.closes = g.closes + 1
    end on
end window

program main(args)
    g = { saved: {}, closes: 0 }

    if not qt.can_display() then
        print "no display -- nothing to open"
        return
    end if

    cust = { who: "Ada Lovelace", limit: {USD}"500.00" }
    qt.open(app)
    print "open:    " + string(qt.is_open(app))

    ' THE WINDOW ITSELF, ADDRESSED BY ITS OWN NAME.
    print "title:   " + qt.read(app, "app", "windowTitle")
    x = qt.set(app, "app", "windowTitle", "Closing (edited)")
    print "renamed: " + qt.read(app, "app", "windowTitle")

    ' Somebody types.
    y = qt.write(app, "who", "Grace Hopper")

    ' CLOSING RUNS THE HANDLER FIRST.
    ok = qt.close(app)
    print "closed:  " + string(qt.is_open(app))
    print "saved:   " + g.saved.who + ", " + string(g.saved.limit)

    ' AND AFTERWARDS THE WINDOW REFUSES, which is the point: there is nothing
    ' on screen to read. `qt.is_open` is how you ask without raising.
    on error goto next
    stale = qt.read(app, "who")
    if error then
        print "after:   " + error.message
        error.clear()
    end if

    ' REOPENING BUILDS A FRESH WINDOW. The form captures its subject again, so
    ' what was typed before the close is gone -- a close is not a hide you can
    ' undo.
    qt.open(app)
    print "reopened who: " + qt.read(app, "who")
    print "reopened title: " + qt.read(app, "app", "windowTitle")

    ' AND `qt.stop()` IS NOT A CLOSE. It means "give me the thread back" and
    ' nothing more: the window stays open, stays readable, and no `closed`
    ' handler fires. (Qt's own `quit()` closes every top-level window; gBASIC
    ' deliberately does not use it.)
    print "closes so far: " + string(g.closes)
    qt.stop()
end program
```

<!--OUT:16_closing_a_window-->

```
open:    true
title:   Closing
renamed: Closing (edited)
closed:  false
saved:   Grace Hopper, 500.00
after:   window `app` is not open
reopened who: Ada Lovelace
reopened title: Closing
closes so far: 1
```

---

## A window shown modally

A message box offers two buttons and that is the whole of it, so **anything
richer is an ordinary `window` shown modally** — a form to fill in, a list to
choose from. `qt.open_modal` blocks until somebody answers, and answers **true**
for an accept and **false** for a reject, a close, or the window being
dismissed.

**It is the same window.** One shared open path, so the declaration, the
validation, the binding and the handlers are the same code as an ordinary
window's — nothing in the recipe below is modal-specific except the two verbs
that answer it.

**`qt.accept` and `qt.reject` are not `qt.answer`.** That one drives a *dialog*
from the program when nobody is there (a timeout, an unattended run, a suite);
these are the person's own answer arriving through a button they pressed. On a
window that was not opened modally they are refused, naming the verb that would
have made it one.

**It closes the window before returning**, which is why `on <window>.closed` is
where you read the form — the same rule an ordinary window follows. That is a
deliberate choice rather than a side effect: `accept` leaves the widget alive
while the window manager's button closes it, so without this `false` would mean
two different states and every `qt.close` afterwards would need an `qt.is_open`
guard. The cost is stated: read the subject in the handler, not after the call.

Note the second half of the recipe. The handler fires on a **cancel** too, so
`g.typed` is filled either way — what decides whether to *use* it is
`qt.open_modal`'s answer, not the handler.

**And it does not starve the rest of the program.** Opened from `main` —
*"show this form and wait"* — gBASIC's own sources keep running: a webserver
answers and a `timer` fires, which is exactly what lets this recipe answer its
own window with nobody there to click. The limitation is the other case, and
`qt.confirm` already has it: a modal window raised from **inside a handler**
suspends every source until it is answered, because what cannot happen is
re-entering the interpreter, not re-entering Qt's loop.

<!--CODE:17_a_modal_window-->

```basic
' A WINDOW SHOWN MODALLY -- what `qt.confirm` is not.
'
' A message box offers two buttons and that is the whole of it, so anything
' richer -- a form to fill in, a list to choose from -- is an ordinary `window`
' declaration shown modally. IT IS THE SAME WINDOW: one shared open path, so
' the declaration, the binding and the handlers are the same code, and nothing
' here is modal-specific except the two verbs that answer it.
'
' `qt.open_modal` BLOCKS until somebody answers, and answers TRUE for an accept
' and FALSE for a reject, a close, or the window being dismissed. It CLOSES THE
' WINDOW BEFORE RETURNING, which is why the `closed` handler is where you read
' the form -- the same rule an ordinary window follows.
'
' AND IT DOES NOT STARVE THE REST OF THE PROGRAM. Opened from `main`, gBASIC's
' own sources keep running: a webserver answers and a `timer` fires, which is
' what lets this recipe answer its own window. The limitation is the other case
' and `qt.confirm` already has it: a modal window raised from INSIDE a handler
' suspends every source until it is answered.
window edit( title: "Customer", width: 360, height: 200 )
    column( margin: 10, spacing: 8 )
        form to edit cust
            field who(   label: "Name" )
            field limit( label: "Limit", type: {USD} )
        end form
        row
            stretch()
            button save(   label: "Save" )
            button cancel( label: "Cancel" )
        end row
    end column

    ' TWO VERBS, AND THEY ARE NOT `qt.answer`. That one drives a DIALOG from
    ' the program when nobody is there -- a timeout, an unattended run. These
    ' are the person's own answer arriving through a button they pressed.
    on save.clicked
        x = qt.accept(edit)
    end on
    on cancel.clicked
        x = qt.reject(edit)
    end on

    ' THE ONE PLACE THE FORM IS READ, because the next thing that happens is
    ' the window closing. `g` is one record whose fields handlers mutate, which
    ' is where GUI state lives in gBASIC.
    on edit.closed
        g.typed = qt.subject(edit, "cust")
    end on
end window

program main(args)
    g = { typed: {}, ticks: 0, press: "save" }

    if not qt.can_display() then
        print "no display -- nothing to open"
        return
    end if

    cust = { who: "Ada Lovelace", limit: {USD}"500.00" }

    ' A RECIPE HAS NOBODY TO CLICK, so a timer answers for one -- and that it
    ' CAN is the measurement that matters: the timer only fires because
    ' gBASIC's sources are serviced inside the modal loop.
    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.ticks = g.ticks + 1
            if g.ticks = 2 then
                y = qt.write(edit, "who", "Grace Hopper")
                z = qt.invoke(edit, g.press, "click")
            end if
        end while
    end watch

    ' SAVED.
    if qt.open_modal(edit) then
        print "saved:     " + g.typed.who + ", " + string(g.typed.limit)
    else
        print "cancelled"
    end if
    print "limit kind: " + type(g.typed.limit)
    print "open after: " + string(qt.is_open(edit))

    ' CANCELLED. The same window, the same declaration, a different button --
    ' and the handler still read the form, because `closed` fires either way.
    ' What decides whether to USE it is the answer, not the handler.
    g.ticks = 0
    g.press = "cancel"
    if qt.open_modal(edit) then
        print "saved:     " + g.typed.who
    else
        print "cancelled, and the form read anyway: " + g.typed.who
    end if

    cancelled = timer.cancel(beat)
end program
```

<!--OUT:17_a_modal_window-->

```
saved:     Grace Hopper, 500.00
limit kind: money
open after: false
cancelled, and the form read anyway: Grace Hopper
```

---

## 18. A context menu

A right-click that offers what the clicked row can do. A **`popup`** is a menu
that is not on the bar, and it spends its one slot the other way round from
`menu`: a container carries a *name* or *options* and never both, so
`menu "File"` spends it on the caption a person reads on the bar — and a
context menu has no bar to be read on, so what it needs is a **name**, which is
how `qt.popup` raises it at all. Both halves are refused, so a `popup` with a
caption and a `menu` with a name each get a refusal naming the remedy.

**Declaring the handler is what asks for it.** Every Qt widget starts out
letting Qt put up its own menu, and under that setting
`customContextMenuRequested` is **never emitted** — so without this the handler
would be connected to something that never happens: the right-click does
nothing, and nothing is raised. Writing the handler is enough. The cost is
stated rather than hidden: your menu *replaces* the one Qt would have shown, so
a `memo` or `source` with a context handler loses its own cut and paste, which
is why a part with no handler keeps Qt's.

**The row is `qt.selected`, with nothing new to learn.** Qt moves the selection
on a right-press, so the handler asks the same question a left click asks and
the signal's position is never needed. What a row *offers* is decided by
**hiding** what it does not: `visible` is an ordinary property of a menu item,
so one menu is built once in the declaration and narrowed per row. And
`qt.popup` answers **`false`** when nothing in it is left visible — an empty
context menu reads as a control that failed, and a row that offers nothing is an
ordinary outcome rather than an error. The count looks *into* a submenu too, so
a menu whose every item is hidden behind one empty submenu still answers
`false` rather than putting up an arrow that leads nowhere.

**`qt.right_click` is how a program checks its own menu** with nobody there to
click. It sends a press, a release and then the context event, in the order a
mouse produces them, so the selection moves as it would for a person — a bare
context event would fire the handler over whatever was selected before. It is
also the only way to reach a `table`, `memo` or `source` at all: those answer a
right-click on their *scrolling area* rather than on the widget, which nothing
in gBASIC can name.

`qt.menus(win, "row_actions")` reads a popup's entries, which is how you check
that an item landed in the menu you meant. A number there is a position on the
bar and text is a popup's name — a popup is not on the bar, so it *has* no
position.

<!--CODE:18_a_context_menu-->

```basic
' A CONTEXT MENU -- a right-click that offers what the clicked row can do.
'
' A `popup` IS A MENU THAT IS NOT ON THE BAR, and it spends its one slot the
' other way round from `menu`. A container carries a NAME or OPTIONS and never
' both, so `menu "File"` spends it on the caption a person reads on the bar --
' and a context menu has no bar to be read on, so what it needs is a NAME,
' which is how `qt.popup` raises it at all.
'
' DECLARING THE HANDLER IS WHAT ASKS FOR IT. Every Qt widget starts out letting
' Qt put up its own menu, and under that setting the signal below is NEVER
' EMITTED -- so this would otherwise be a handler connected to something that
' never happens, with the right-click doing nothing and nothing raised. Writing
' the handler is enough; the cost is that your menu REPLACES the one Qt would
' have shown, so a `memo` with a context handler loses its own cut and paste.
'
' THE ROW IS `qt.selected`, with nothing new to learn: Qt moves the selection on
' a right-press, so the handler asks the same question a left click asks. What
' this row OFFERS is decided by HIDING what it does not -- one menu built once,
' narrowed per row -- and `qt.popup` answers FALSE when nothing in it is left
' visible, because an empty context menu reads as a control that failed.

window app( title: "Files", width: 460, height: 300 )
    popup row_actions
        item open_it( label: "Open" )
        item rename_it( label: "Rename..." )
        separator()
        item delete_it( label: "Delete" )
    end popup

    table files to show listing
        field name( label: "Name" )
        field kind( label: "Kind" )
    end table

    on files.customContextMenuRequested
        row = g.listing[qt.selected(app, "files")]
        g.asked = row.name
        ' A FOLDER CANNOT BE OPENED OR DELETED HERE, so those two go away.
        ' `visible` is an ordinary property of a menu item.
        qt.set(app, "open_it", "visible", row.kind = "file")
        qt.set(app, "delete_it", "visible", row.kind = "file")
        g.offered = qt.popup(app, "row_actions")
    end on

    on open_it.triggered
        g.opened = g.asked
    end on
end window

program main(args)
    if not qt.can_display() then
        print "no display, so no window -- the declaration is still checked"
        return
    end if

    listing = [{ name: "ledger.bas", kind: "file" },
               { name: "reports",    kind: "folder" },
               { name: "notes.txt",  kind: "file" }]
    ' ONE RECORD, because a handler cannot rebind a plain variable in `main`.
    g = { listing: listing, asked: "", offered: false, opened: "" }

    qt.open(app)

    ' RIGHT-CLICK THE FOLDER. `qt.right_click` drives it the way a mouse does,
    ' which is how a program can check its own menu with nobody there.
    qt.right_click(app, "files", 1)
    print "right-clicked:  " + g.asked
    print "menu appeared:  " + string(g.offered)
    print "open offered:   " + string(qt.read(app, "open_it", "visible"))

    ' AND NOW A FILE, where all three apply.
    qt.right_click(app, "files", 2)
    print "right-clicked:  " + g.asked
    print "open offered:   " + string(qt.read(app, "open_it", "visible"))

    qt.invoke(app, "open_it", "trigger")
    print "opened:         " + g.opened

    ' WHAT THE MENU HOLDS, in order. A separator has no caption of its own.
    print "the menu:       " + string(qt.menus(app, "row_actions"))

    qt.close(app)
end program
```

<!--OUT:18_a_context_menu-->

```
right-clicked:  reports
menu appeared:  true
open offered:   false
right-clicked:  notes.txt
open offered:   true
opened:         notes.txt
the menu:       ["Open","Rename...",unknown,"Delete"]
```

---

## 19. The wheel

Ctrl + wheel zooms the editor — what every code editor does, and what gBASIC
Studio uses a scroll controller for.

**A wheel is an event, not a signal.** A Qt widget has no wheel signal; it
*receives* one. So `on code.wheel` is handled the way `on <window>.closed` is,
and the handler asks `qt.wheel()` for the event rather than taking arguments.
`dy` is in **notches** — one click of the wheel — and **positive means away
from you**. (That is Qt's sign and the opposite of GTK's, so a handler ported
from a GTK program has its comparison the wrong way round.)

**The zoom needs no verb.** `font` is an ordinary property that reads back as
text — `"monospace,9,-1,5,400,..."` — so the size is just a number in a list:
split it, change it, write it back. There is no `qt.zoom` because nothing needs
one.

**`qt.swallow()` is what stops the editor also scrolling**, and it is not
optional: Ctrl + wheel scrolls a text view *faster* than a plain wheel, so
without it the view jumps about while the text resizes. A handler that
**declines** — by simply not calling it — leaves the wheel alone, which is what
keeps ordinary scrolling working. So the rule is *only with Ctrl*, exactly as
Studio's is, and a part that declared no wheel handler is not filtered at all.

`qt.scroll` drives the wheel the way a mouse does, for a program checking
itself, and answers whether anything **claimed** the event. `notches` may be
fractional, because a trackpad sends less than one. And `qt.scrolled` /
`qt.scroll_to` read and set the position — which is how *"reopen the editor
where I left it"* is written, and also the only way to see that swallowing
really stopped the view rather than merely reporting that it had.

<!--CODE:19_the_wheel-->

```basic
' THE WHEEL -- Ctrl + wheel zooms the editor, which is what every code editor
' does and what gBASIC Studio uses a scroll controller for.
'
' A WHEEL IS AN EVENT, NOT A SIGNAL. A Qt widget has no wheel signal -- it
' RECEIVES one -- so `on code.wheel` is handled the way `on <window>.closed` is,
' and the handler asks `qt.wheel()` for the event rather than taking arguments.
' `dy` is in NOTCHES (one click of the wheel) and positive means away from you.
'
' THE ZOOM NEEDS NO VERB. `font` is an ordinary property that reads back as
' text -- "monospace,9,-1,5,400,..." -- so the size is just a number in a list:
' split it, change it, write it back.
'
' `qt.swallow()` IS WHAT STOPS THE EDITOR ALSO SCROLLING, and it is not
' optional: Ctrl + wheel scrolls a text view FASTER than a plain wheel, so
' without it the view jumps about while the text resizes. A handler that
' DECLINES leaves the wheel alone, which is what keeps ordinary scrolling
' working -- so the rule is "only with Ctrl", exactly as Studio's is.

window app( title: "Zoom", width: 520, height: 360 )
    column( margin: 8, spacing: 6 )
        text hint( label: "Ctrl + wheel over the code zooms it." )
        source code()
    end column

    on code.wheel
        ev = qt.wheel()
        if not ev.ctrl then
            ' NOTHING, and the event is left alone so the editor scrolls.
            return
        end if
        f = split(qt.read(app, "code", "font"), ",")
        bigger = 1
        if ev.dy < 0 then
            bigger = 0 - 1
        end if
        qt.set(app, "code", "font", f[0] + "," + string(number(f[1]) + bigger))
        g.zooms = g.zooms + 1
        qt.swallow()
    end on
end window

program main(args)
    if not qt.can_display() then
        print "no display, so no window -- the declaration is still checked"
        return
    end if

    g = { zooms: 0 }
    qt.open(app)

    lines = []
    for i = 1 to 200
        lines = append(lines, "print " + string(i))
    end for
    x = qt.write(app, "code", join(lines, chr(10)))

    size = number(split(qt.read(app, "code", "font"), ",")[1])
    print "font at first:   " + string(size)

    ' A PLAIN WHEEL SCROLLS and does not zoom. `qt.scroll` drives the wheel the
    ' way a mouse does, and answers whether anything CLAIMED the event.
    qt.scroll_to(app, "code", 50)
    claimed = qt.scroll(app, "code", -2)
    print ("plain wheel:     claimed " + string(claimed) +
           ", zooms " + string(g.zooms))
    print ("                 and it scrolled to " +
           string(qt.scrolled(app, "code")))

    ' CTRL + WHEEL ZOOMS and claims the event, so the view stays put.
    was = qt.scrolled(app, "code")
    claimed = qt.scroll(app, "code", 3, "ctrl")
    print ("ctrl+wheel:      claimed " + string(claimed) +
           ", zooms " + string(g.zooms))
    print ("                 font now " +
           string(number(split(qt.read(app, "code", "font"), ",")[1])))
    print "                 view stayed at " + string(qt.scrolled(app, "code") = was)

    qt.close(app)
end program
```

<!--OUT:19_the_wheel-->

```
font at first:   9
plain wheel:     claimed false, zooms 0
                 and it scrolled to 56
ctrl+wheel:      claimed true, zooms 1
                 font now 10
                 view stayed at true
```

---

## 20. Remembering a layout

Reading and setting the pane sizes of a `split` — what *"put my windows back
where I left them"* is made of.

**A split cannot be named.** Its one slot carries the direction (`across` or
`down`), because a container takes a name *or* options and never both — so
there is nothing to look up, and a split is addressed by naming a part **inside
it**. That is the same rule `qt.pane` already follows, which is why the sizes
are simply part of what `qt.pane` answers rather than a verb of their own.

**What you set is a ratio, not a measurement.** Qt adds the drag handle and the
layout's margins, and re-proportions everything when the window is resized — so
asking for 200 and 700 gives you something near that *ratio* and not those two
numbers back. Store them and set them again and the layout looks the same,
which is what anybody actually wants; compare them for equality and you will be
disappointed.

**A wrong count is refused**, which is worth knowing because Qt's own behaviour
is silent: one size on a two-pane split *collapses* the second pane, and three
sizes on a two-pane split honour none of them. Both leave a perfectly
ordinary-looking layout that is not the one you asked for, so the refusal names
both numbers. A negative size is refused too, rather than quietly becoming a
collapse.

**Zero is legal**, and is how a panel is hidden without being forgotten.

Each split is addressed separately, by a part of its own — so setting the inner
one leaves the outer alone, which the recipe prints. A part inside a `scroll`
reports the **scroll** rather than the split beyond it, because the walk stops
at the nearest region.

<!--CODE:20_remembering_a_layout-->

```basic
' REMEMBERING A LAYOUT -- reading and setting the pane sizes of a `split`, which
' is what "put my windows back where I left them" is made of.
'
' A SPLIT CANNOT BE NAMED. Its one slot carries the direction (`across` or
' `down`), because a container takes a name OR options and never both -- so
' there is nothing to look up, and a split is addressed by naming a part INSIDE
' it. That is the same rule `qt.pane` already follows, so the sizes are simply
' part of what `qt.pane` answers.
'
' WHAT YOU SET IS A RATIO, NOT A MEASUREMENT. Qt adds the drag handle and the
' layout's margins, and it re-proportions everything when the window is
' resized -- so asking for 200 and 700 gives you something near that ratio and
' NOT those two numbers back. Store them and set them again and the layout
' looks the same, which is what anybody actually wants; compare them for
' equality and you will be disappointed.
'
' A WRONG COUNT IS REFUSED, which is worth knowing because Qt's own behaviour
' is silent: one size on a two-pane split COLLAPSES the second pane, and three
' sizes on a two-pane split honour none of them. Both leave a perfectly
' ordinary-looking layout that is not the one you asked for.
'
' ZERO IS LEGAL, and is how a panel is hidden without being forgotten.

window app( title: "Layout", width: 900, height: 420 )
    split across
        scroll browser
            column( margin: 4 )
                text f1( label: "one.bas" )
                text f2( label: "two.bas" )
            end column
        end scroll
        split down
            source code( plainText: "x = 1" )
            table console to show rows
                field line( label: "Line" )
            end table
        end split
    end split
end window

program main(args)
    if not qt.can_display() then
        print "no display, so no window -- the declaration is still checked"
        return
    end if

    rows = [{ line: "ready" }]
    qt.open(app)

    ' WHAT THE SPLIT LOOKS LIKE NOW. `browser` is a part of the outer split, so
    ' naming it is how the outer split is addressed.
    outer = qt.pane(app, "browser")
    print ("the outer split is " + outer.direction + " with " +
           string(outer.panes) + " panes")
    print "the browser is pane " + string(outer.index)

    ' SET IT. Think of these as a ratio: a narrow browser beside a wide editor.
    qt.resize_panes(app, "browser", [200, 700])
    now = qt.pane(app, "browser").sizes
    print "browser narrower than the rest: " + string(now[0] < now[1])

    ' THE INNER SPLIT IS A DIFFERENT SPLIT, addressed by a part of its own.
    qt.resize_panes(app, "code", [300, 100])
    inner = qt.pane(app, "code").sizes
    print "editor taller than the console: " + string(inner[0] > inner[1])
    ' AND THE OUTER ONE DID NOT MOVE.
    still = qt.pane(app, "browser").sizes
    print "the outer split stayed put:     " + string(still[0] = now[0])

    ' REMEMBER AND RESTORE, which is the whole point. In a real program these
    ' go into `persist` with the rest of the session.
    saved = { browser: qt.pane(app, "browser").sizes,
              editor:  qt.pane(app, "code").sizes }
    qt.resize_panes(app, "browser", [450, 450])
    even = qt.pane(app, "browser").sizes
    print ("after a change, even:           " +
           string(even[0] = even[1]))
    qt.resize_panes(app, "browser", saved.browser)
    back = qt.pane(app, "browser").sizes
    print "and restored to narrow again:   " + string(back[0] < back[1])

    ' HIDING A PANEL: zero, and it comes back.
    qt.resize_panes(app, "browser", [0, 900])
    print ("browser hidden:                 " +
           string(qt.pane(app, "browser").sizes[0] = 0))
    qt.resize_panes(app, "browser", saved.browser)
    print ("and shown again:                " +
           string(qt.pane(app, "browser").sizes[0] > 0))

    ' A WRONG COUNT IS REFUSED, naming both numbers.
    on error goto next
    qt.resize_panes(app, "browser", [500])
    print "one size for two panes:         " + error.message
    error.clear()
    on error stop

    qt.close(app)
end program
```

<!--OUT:20_remembering_a_layout-->

```
the outer split is across with 2 panes
the browser is pane 0
browser narrower than the rest: true
editor taller than the console: true
the outer split stayed put:     true
after a change, even:           true
and restored to narrow again:   true
browser hidden:                 true
and shown again:                true
one size for two panes:         qt.resize_panes: that split has 2 panes and 1 size was given
```

---

## 21. Icons

A picture on a button or a menu item.

**`icon:` is the one option that is not an ordinary property.** Qt will not turn
a string into an icon by itself, so this is the single name with a conversion
behind it — and it is the *same* conversion in the declaration and in
`qt.set`, so the two cannot disagree about what a source is.

**Three sources, told apart by facts.** A name beginning **`SP_`** is one of
Qt's own eighty-one standard icons, which work with **no desktop icon theme
installed** — the only source that always does, and the right choice for
anything that must run on a server. A string that **names an existing file** is
that file, which is what an application shipping its own icons uses. Anything
else is a **freedesktop theme name**.

**The two kinds of failure are answered differently**, because they are
different in kind. A path that is not there, a file that is not an image and a
misspelled `SP_` name are all **refused** — each is a fact that can be checked,
and the intent is unambiguous. A theme name this machine's theme lacks is a
**warning**, never a raise: that is a fact about the *machine*, and refusing
would stop a program running on a server with no icon theme at all.

**Which is why `qt.has_icon` exists.** A QIcon has no text form — a button
carrying a real picture and one carrying nothing both read back as `""` — so
`qt.read` on an icon refuses and names this instead, an empty answer for
something that *is* set being a wrong answer rather than no answer. It is also
how a program decides to fall back to a text-only button when the machine
turns out to have no theme.

An empty source **clears** it, which is what a play/pause button needs.

<!--CODE:21_icons-->

```basic
' ICONS -- a picture on a button or a menu item.
'
' `icon:` IS THE ONE OPTION THAT IS NOT AN ORDINARY PROPERTY. Qt will not turn
' a string into an icon by itself, so this is the single name with a conversion
' behind it -- and it is the same conversion in the declaration and in `qt.set`,
' so the two cannot disagree about what a source is.
'
' THREE SOURCES, TOLD APART BY FACTS. A name beginning `SP_` is one of Qt's own
' eighty-one standard icons, which work with NO desktop icon theme installed --
' the only source that always does, and the right choice for anything that must
' run on a server. A string that names an EXISTING FILE is that file, which is
' what an application shipping its own icons uses. Anything else is a
' freedesktop theme name.
'
' THE TWO KINDS OF FAILURE ARE ANSWERED DIFFERENTLY, because they are different
' in kind. A path that is not there, a file that is not an image and a
' misspelled `SP_` name are all REFUSED -- each is a fact that can be checked
' and the intent is unambiguous. A theme name this machine's theme lacks is a
' WARNING, never a raise: that is a fact about the machine, and refusing would
' stop a program running on a server with no icon theme at all.
'
' WHICH IS WHY `qt.has_icon` EXISTS. A QIcon has no text form -- a button
' carrying a real picture and one carrying nothing both read back as "" -- so
' `qt.read` on an icon refuses and names this instead. It is also how a program
' decides to fall back to a text-only button.

window app( title: "Icons", width: 440, height: 240 )
    menu "File"
        item act_open( label: "Open", icon: "SP_DialogOpenButton" )
        item act_quit( label: "Quit", icon: "SP_DialogCloseButton" )
    end menu
    column( margin: 10, spacing: 8 )
        text hint( label: "Qt's own icons work with no desktop theme." )
        row
            button ok_it(     label: "OK",     icon: "SP_DialogOkButton" )
            button cancel_it( label: "Cancel", icon: "SP_DialogCancelButton" )
            button plain_it(  label: "Plain" )
            stretch()
        end row
    end column
end window

program main(args)
    if not qt.can_display() then
        print "no display, so no window -- the declaration is still checked"
        return
    end if

    qt.open(app)

    ' WHETHER ONE ARRIVED. There is no other way to ask: an icon has no text.
    print "OK has an icon:      " + string(qt.has_icon(app, "ok_it"))
    print "Cancel has one:      " + string(qt.has_icon(app, "cancel_it"))
    print "Plain has none:      " + string(qt.has_icon(app, "plain_it"))
    print "the menu item has:   " + string(qt.has_icon(app, "act_open"))

    ' SET ONE AT RUN TIME, through the same conversion.
    qt.set(app, "plain_it", "icon", "SP_DialogApplyButton")
    print "and now Plain has:   " + string(qt.has_icon(app, "plain_it"))

    ' AND TAKE IT AWAY, which a play/pause button needs.
    qt.set(app, "plain_it", "icon", "")
    print "cleared again:       " + string(qt.has_icon(app, "plain_it"))

    ' THE REFUSALS, each naming what was wrong.
    on error goto next
    qt.set(app, "ok_it", "icon", "SP_NotAThing")
    print "a misspelled Qt name: " + error.message
    error.clear()
    qt.set(app, "ok_it", "icon", "/no/such/icon.png")
    print "a path that is not there: " + error.message
    error.clear()
    qt.read(app, "ok_it", "icon")
    print "reading one as text: " + error.message
    error.clear()
    on error stop

    qt.close(app)
end program
```

<!--OUT:21_icons-->

```
OK has an icon:      true
Cancel has one:      true
Plain has none:      false
the menu item has:   true
and now Plain has:   true
cleared again:       false
a misspelled Qt name: qt.set: `SP_NotAThing` is not one of Qt's standard icon names
a path that is not there: qt.set: there is no icon file at `/no/such/icon.png`
reading one as text: qt.read: an icon has no text form; ask `qt.has_icon(win, part)` whether there is one
```

---

## 22. A big result

A table that **asks** for its cells instead of being shown them — which is how
you put a million rows on screen.

`table x to show <list>` **holds every row**. That is right for a form's lines
or a file list, and wrong for a query that came back with a hundred thousand
rows: building a widget per cell costs memory and time proportional to the
*data* rather than to the *window*.

`table x to ask <function>` **keeps none of it**. The subject is a function
name rather than a list; it is called with a row and a column (both counted
from 0) and answers that one cell, as it becomes visible. Tell the table how
many rows there are with `qt.rows`, and tell it again whenever the data changes
— setting the count is what makes Qt re-ask, so it is also the refresh.

**The cost is the viewport, not the table.** Measured: Qt asks for about 27
cells whether the table has a hundred rows or a million, and scrolling to the
end asks for a few dozen more rather than walking everything in between. This
recipe prints its own callback's call count, so you can see it — 64 cells to
draw a million rows.

**Whatever the callback returns is rendered by the same renderer `print` uses**,
so a number, a date or money arrives in the words the language itself uses.
There is no formatting to do and nothing to convert.

`qt.cell`, `qt.heading`, `qt.select` and `qt.selected` all work on either kind
of table, so a program need not know which it declared. A form may **not**
`ask`: it shows or edits one record whose fields it can read, so there is
nothing for it to ask for.

<!--CODE:22_a_big_result-->

```basic
' A BIG RESULT -- a table that ASKS for its cells instead of being shown them,
' which is how you put a million rows on screen.
'
' `table x to show <list>` HOLDS EVERY ROW. That is right for a form's lines or
' a file list, and wrong for a query that came back with a hundred thousand
' rows: building a widget per cell costs memory and time proportional to the
' data rather than to the window.
'
' `table x to ask <function>` KEEPS NONE OF IT. The subject is a FUNCTION NAME,
' not a list -- it is called with a row and a column and answers that one cell,
' as it becomes visible. Tell the table how many rows there are with `qt.rows`,
' and tell it again whenever the data changes.
'
' THE COST IS THE VIEWPORT, NOT THE TABLE. Measured: Qt asks for about 27 cells
' whether the table has a hundred rows or a million, and scrolling to the end
' asks for a few dozen more rather than walking everything in between. This
' recipe prints its own callback's call count, so you can see it.
'
' WHATEVER THE CALLBACK RETURNS IS RENDERED BY THE SAME RENDERER `print` USES,
' so a number, a date or money arrives in the words the language itself uses --
' there is no formatting to do and nothing to convert.

window app( title: "Results", width: 560, height: 300 )
    column( margin: 8, spacing: 6 )
        text hint( label: "A table that asks for what it needs to draw." )
        table results to ask cell_of
            field num(   label: "#" )
            field who(   label: "Customer" )
            field total( label: "Total" )
            field when(  label: "Placed" )
        end table
    end column
end window

' THE CELL. Called with a row and a column, both counted from 0. In a real
' program this reads from wherever the rows actually live -- a database cursor,
' a file, a frame -- and here it computes them so the recipe needs nothing.
function cell_of(row, column)
    g.asked = g.asked + 1
    if column = 0 then
        return row + 1
    end if
    if column = 1 then
        return "customer " + string(row)
    end if
    if column = 2 then
        ' MONEY, which arrives formatted because the one renderer does it.
        return {USD}string(12 + row)
    end if
    return {date}"2026-01-01"
end function

program main(args)
    if not qt.can_display() then
        print "no display, so no window -- the declaration is still checked"
        return
    end if

    g = { asked: 0, ticks: 0 }
    qt.open(app)

    ' HOW MANY ROWS THERE ARE. This is a verb and not part of the declaration
    ' because it changes: a results grid is refilled by every query.
    qt.rows(app, "results", 1000000)
    print "rows:          " + string(qt.rows(app, "results"))

    ' READ ANY CELL, which asks the callback for exactly that one.
    print "row 0:         " + string(qt.cell(app, "results", 0, "who"))
    print "row 999999:    " + string(qt.cell(app, "results", 999999, "who"))
    print "money reads:   " + string(qt.cell(app, "results", 0, "total"))
    print "a date reads:  " + string(qt.cell(app, "results", 0, "when"))

    ' AND NOW LET QT DRAW IT, so the count below is what painting a
    ' million-row table actually costs. The timer hands the thread back; see
    ' the event-loop recipe for why that is how a `check` run ends.
    g.asked = 0
    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.ticks = g.ticks + 1
            if g.ticks >= 2 then
                qt.stop()
            end if
        end while
    end watch
    qt.run()
    cancelled = timer.cancel(beat)

    print "cells asked for to draw a MILLION rows: " + string(g.asked)
    print "which is fewer than a thousand:         " + string(g.asked < 1000)

    qt.close(app)
end program
```

<!--OUT:22_a_big_result-->

```
rows:          1000000
row 0:         customer 0
row 999999:    customer 999999
money reads:   12.00
a date reads:  2026-01-01
cells asked for to draw a MILLION rows: 64
which is fewer than a thousand:         true
```

---

## 23. A changing notebook

A container whose **set of children changes** while the window is up.

A `window` block cannot contain a loop, so a window whose shape depends on data
is **built as a record** (recipe 10) — and that record is read once, at
`qt.open`. One tab per open document is the case that makes the limit obvious:
opening a file has to add a tab, closing one has to take it away, and neither
should disturb what you have typed in the others.

`qt.refresh(win, name, items)` reconciles a named container against the same
`items` array the record holds. It is the **third form** of the verb that
already re-shows a table or a form, and it answers how many children it placed.

**What it keeps is the point.** A child whose **name and word** are unchanged is
kept — the same widget, so its text, its caret, its scroll position and its
**undo stack** all survive. A child the record no longer mentions is **deleted**,
and its parts leave the window with it, so `qt.read` on a closed document's
editor refuses rather than answering from something nobody can see. A new child
is built through the same code `qt.open` uses, so a window cannot render one way
at open and another afterwards. A `tab`'s caption is re-read.

**A tab carries a `name` here**, which a `window` block cannot spell: a declared
`tab` spends its one slot on the caption, because that is what a person reads on
it. A record has no such limit, and the name is what the reconcile keeps a page
by — keying on the caption would not do, because **the caption is the thing that
changes**: an edited document's tab reads `* notes.bas`.

A `tabs`, a `column`, a `row` and a `group` can be reconciled. A `split` cannot
be named at all (its one slot is `across` or `down`) and a `cells` grid has
declared positions, so both are refused by name rather than quietly doing
nothing. A child with **no** name is always rebuilt — there is nothing to match
it by.

`qt.tabs(win, name)` answers a tab set's captions and `qt.children(win, name)`
the names of a container's children, both in order. They exist because neither
is readable any other way: `QTabWidget::tabText` takes an argument, which
`qt.invoke` cannot pass, and `qt.at` reads coordinates in a `cells` grid only.

<!--CODE:23_a_changing_notebook-->

```basic
' A CHANGING NOTEBOOK -- a container whose set of children changes while the
' window is up, which is what `qt.refresh(win, name, items)` is for.
'
' THE DECLARATION CANNOT CONTAIN A LOOP, so a window whose shape depends on
' data is BUILT as a record (recipe 10) -- and until this verb existed that
' record was frozen the moment the window opened. One tab per open document is
' the obvious case: opening a file has to add a tab, closing one has to take it
' away, and neither should disturb what you have typed in the others.
'
' RECONCILED BY NAME AND WORD. A child whose name and word are unchanged is
' KEPT -- the same widget, so the text, the caret, the scroll position and the
' undo stack all survive -- a new one is BUILT, an absent one is DELETED, and a
' tab's caption is re-read from the record.
'
' WHICH IS WHY A TAB CARRIES A NAME HERE. A tab written in a `window` block
' spends its one slot on the caption, because that is what a person reads; a
' RECORD has no such limit, and the name is what a reconcile keeps the page by.
' Keying on the caption would not do: the caption is the thing that CHANGES,
' since an edited document's tab reads `* notes.bas`.
'
' Run it:
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt_cookbook/23_a_changing_notebook.bas

function page_for(doc)
    return { kind: "container", word: "tab", name: "p" + doc.id,
             label: doc.title,
             items: [ { kind: "part", word: "source", name: "ed" + doc.id,
                        options: { plainText: doc.text } } ] }
end function

function pages_for(docs)
    out = []
    for each d in docs
        out = append(out, page_for(d))
    end for
    return out
end function

function notebook_for(docs)
    return { kind: "window", name: "editor",
             options: { title: "Editor", width: 520, height: 300 },
             items: [ { kind: "container", word: "tabs", name: "book",
                        items: pages_for(docs) } ] }
end function

program main(args)
    open_docs = [ { id: 1, title: "notes.bas", text: "print 1" } ]
    editor = notebook_for(open_docs)

    if not qt.can_display() then
        print "No display is available, so there is no window to open."
        return
    end if

    qt.open(editor)
    print "tabs at the start:   " + join(qt.tabs(editor, "book"), ", ")

    ' SOMEBODY TYPES. This is the state that has to survive everything below.
    x = qt.write(editor, "ed1", "print 1" + chr(10) + "print 2")

    ' A SECOND FILE IS OPENED: append to the list, rebuild the items, reconcile.
    open_docs = append(open_docs, { id: 2, title: "main.bas", text: "run()" })
    n = qt.refresh(editor, "book", pages_for(open_docs))
    print "after opening one:   " + join(qt.tabs(editor, "book"), ", ")
    print "the reconcile placed " + string(n) + " pages"
    print("and the typing survived: " +
          string(qt.read(editor, "ed1") = "print 1" + chr(10) + "print 2"))

    ' THE FIRST FILE IS EDITED, so its caption gains a marker. Only the caption
    ' moves -- the page is the same widget, which is the whole point.
    open_docs[0].title = "* notes.bas"
    n = qt.refresh(editor, "book", pages_for(open_docs))
    print "after an edit:       " + join(qt.tabs(editor, "book"), ", ")
    print("still the same page: " +
          string(qt.read(editor, "ed1") = "print 1" + chr(10) + "print 2"))

    ' AND ONE IS CLOSED. An absent child is DELETED, so its parts leave the
    ' window with it -- `qt.read` on a closed document's editor refuses, which
    ' is what stops a program reading off a screen nobody is looking at.
    open_docs = [ open_docs[0] ]
    n = qt.refresh(editor, "book", pages_for(open_docs))
    print "after closing one:   " + join(qt.tabs(editor, "book"), ", ")
    print "children by name:    " + join(qt.children(editor, "book"), ", ")
    on error goto next
    gone = qt.read(editor, "ed2")
    if error then
        print "the closed one is gone: " + error.message
        error.clear()
    end if
    on error stop

    qt.close(editor)
end program
```

<!--OUT:23_a_changing_notebook-->

```
tabs at the start:   notes.bas
after opening one:   notes.bas, main.bas
the reconcile placed 2 pages
and the typing survived: true
after an edit:       * notes.bas, main.bas
still the same page: true
after closing one:   * notes.bas
children by name:    p1
the closed one is gone: this window has no part named `ed2`
```

---

## 24. A margin with marks

A `source`'s **margin**, and what sits in it.

It is not a ruler. Line numbers are worth having on their own — gBASIC's
diagnostics are `line:column` and a reader has nothing to count against — but
the margin's real job is to carry a **mark per line**: a `▶` where each runnable
section starts, an icon where the parse failed, a dot where somebody set a
breakpoint.

```basic
source code( language: "gBASIC", line_numbers: true, marks: true )
```

The two options are independent and both are **refused on anything but a
`source`**, because a `memo` has no margin and an option that was accepted and
did nothing would be invisible.

**The five categories are `section`, `error`, `warning`, `note` and `break`**,
and the set is closed — an unknown one is refused by name with a near-miss hint,
because a category decides a glyph and a priority and the backend has to know
both. Three of them are gBASIC's own diagnostic severities, so a program showing
diagnostics has one category per severity rather than flattening them.

**A line carries one glyph**, decided by two rules. A mark the *person* placed
outranks one the program *computed*, so `break` wins everything — a breakpoint
is the one mark somebody set deliberately and has to see in order to remove it.
Among computed marks the more urgent wins: error, warning, note, with `section`
lowest but above `note`, so the run affordance is not hidden by the least urgent
thing there is.

**Line 1 is the first line.** `source_outline` reports `start_line = 1` and a
parse diagnostic carries a 1-based line, so a line number read out of either
goes straight in with no arithmetic. Line 0 is **refused** rather than dropped,
since a silently dropped mark would report success and draw nothing.

**Setting a category replaces its whole list.** That is what a gutter
recomputed on every redraw wants: there is no record of what was already drawn
to keep in step, and an empty list clears it. `qt.marks` reads back **what was
set**, not what is currently drawable — a program may mark line 40 and then
shorten the document, and the list is the program's while the drawing is the
widget's.

**Clicking the margin** is an event rather than a signal, the margin being a
plain child widget that emits nothing, and the handler **asks** which line it
was. The filter sits on the margin itself, so an ordinary press in the text is
still an ordinary caret move.

`qt.gutter(win, part)` answers `{ line_numbers, marks, width }`, or `unknown`
for a `source` with no margin. The `width` follows the **digit count**, so a
thousand-line document reserves more room than a nine-line one.

<!--CODE:24_a_margin_with_marks-->

```basic
' A MARGIN WITH MARKS -- line numbers, a mark per line, and a breakpoint you
' can click to set. Under QT_QPA_PLATFORM=offscreen, so it runs with no screen.
'
' IT IS NOT A RULER, which is the whole reason the margin carries categories
' rather than a line-number flag: a `section` mark says "this is a thing you
' can Run and it starts here", an `error` mark is the only thing in the window
' saying WHERE a syntax error is, and a `break` mark is a breakpoint somebody
' set. The three diagnostic categories are gBASIC's own severities, so a
' program showing diagnostics has one category per severity.

window ed( title: "Editor", width: 620, height: 420 )
    column
        source code( language: "gBASIC", line_numbers: true, marks: true )
        text hint( label: "click the margin to set a breakpoint" )
    end column

    ' THE MARGIN IS AN EVENT, NOT A SIGNAL -- it is a plain child widget and
    ' emits nothing -- and the handler ASKS which line it was, the rule
    ' `qt.wheel()` already follows.
    on code.gutter
        c = qt.gutter_click()
        b = qt.marks(ed, "code", "break")
        if contains(b, c.line) then
            qt.mark(ed, "code", "break", b excluding [c.line])
        else
            qt.mark(ed, "code", "break", concat(b, [c.line]))
        end if
        append(g.clicked, c.line)
    end on
end window

program main( args )
    g = { clicked: [] }
    if not qt.can_display() then
        print "no display -- nothing to show"
        return
    end if

    qt.open(ed)
    qt.write(ed, "code", join([ "function total(rows)",
                                "    t = 0",
                                "    for each r in rows",
                                "        t = t + r.amount",
                                "    end for",
                                "    return t",
                                "end function" ], chr(10)))

    ' WHAT THE MARGIN IS, or `unknown` for a `source` that has none.
    gut = qt.gutter(ed, "code")
    print "numbers: " + string(gut.line_numbers) + "   marks: " + string(gut.marks)
    print "reserved a width:            " + string(gut.width > 0)

    ' A SECTION STARTS AT LINE 1 -- 1-BASED, like `source_outline`'s own
    ' `start_line` and like every diagnostic gBASIC prints, so a line number
    ' read out of one goes straight in here with no arithmetic.
    qt.mark(ed, "code", "section", [1])
    qt.mark(ed, "code", "warning", [4])
    print "section lines:               " + string(qt.marks(ed, "code", "section"))
    print "warning lines:               " + string(qt.marks(ed, "code", "warning"))

    ' SETTING A CATEGORY REPLACES IT, which is what a gutter recomputed on
    ' every redraw wants: there is no list of what was already drawn to keep.
    qt.mark(ed, "code", "warning", [2, 5])
    print "warning, recomputed:         " + string(qt.marks(ed, "code", "warning"))
    qt.mark(ed, "code", "warning", [])
    print "warning, cleared:            " + string(qt.marks(ed, "code", "warning"))

    ' A BREAKPOINT, SET THE WAY A PERSON SETS ONE. `qt.gutter_press` is what
    ' stands in for the mouse; a line that is not on screen is refused,
    ' because a mouse could not land there either.
    qt.gutter_press(ed, "code", 4)
    qt.gutter_press(ed, "code", 6)
    print "lines clicked:               " + string(g.clicked)
    print "breakpoints:                 " + string(qt.marks(ed, "code", "break"))

    ' CLICKING THE SAME LINE AGAIN CLEARS IT -- the handler above decides that,
    ' not the margin: `qt.mark` replaces a list and the program says what the
    ' list is.
    qt.gutter_press(ed, "code", 4)
    print "after clicking 4 again:      " + string(qt.marks(ed, "code", "break"))

    ' A LINE CARRIES ONE GLYPH, and `break` outranks everything because a mark
    ' somebody PLACED must not be hidden by one the program COMPUTED.
    qt.mark(ed, "code", "error", [6])
    print "line 6 is both error and break, and break is what shows"

    qt.close(ed)
end program
```

<!--OUT:24_a_margin_with_marks-->

```
numbers: true   marks: true
reserved a width:            true
section lines:               [1]
warning lines:               [4]
warning, recomputed:         [2,5]
warning, cleared:            []
lines clicked:               [4,6]
breakpoints:                 [4,6]
after clicking 4 again:      [6]
line 6 is both error and break, and break is what shows
```

---

## 25. A window with furniture

Dock panes, a toolbar and a status bar — the things that make a window look
like an application rather than a form.

All three belong to **the window** rather than to a layout, which is the rule a
`menu` already follows and for the same reason: a window holding any of them is
a `QMainWindow`, and a `QMainWindow` places its dock areas, its status bar and
its tool bars around the content itself. A window that declares none of them is
untouched, and a window with furniture **cannot be shown modally** — those
things belong to a main window and a modal window is a dialog.

**A `docks` block carries the area, and its panes are tabbed together.** The
area is a word in the one free slot — `left`, `right`, `top` or `bottom` — which
is `split across`'s arrangement. Panes in one block become **tabs along that
edge** rather than a stack down it, which matters: five panes stacked down a
column means the ones below the fold are invisible, and a heading reading
"Errors" over a pane you cannot see says nothing. A second block is a second
edge.

**A `dock` carries a name *and* a caption** — the one shape in this vocabulary
that carries both. The caption is what a person reads on the pane's title bar;
the name is what `qt.set` and `qt.dock` address, and what Qt keys a saved layout
by. A pane is given a column, like a `tab` and a `scroll`, so anything works
inside one.

**A `toolbar` holds what a menu holds** — the same `item`s and `separator`s,
with `shortcut:`, `checkable:` and `icon:` all working on them. It carries a
name rather than a caption, because a saved layout is keyed by it.

**A `status` bar needs no verb of its own.** A `text` part in it is an ordinary
widget whose `text` property `qt.set` reaches, so there is nothing new to learn
— and `stretch()` pushes what follows to the right. There is one status bar per
window, so a second is refused rather than quietly merged into the first.

**`qt.dock(win, name)`** answers where a pane is and what it is doing, asked of
the window rather than of the declaration. **`qt.layout(win)`** and
**`qt.restore_layout(win, text)`** keep and put back the whole arrangement —
which area each pane is in, their order, which tab is raised, what is floating.
The text is Qt's own format: store it, do not read it. An arrangement saved
before the window's declaration changed is **reported, not raised**, because
finding one is an ordinary thing on startup.

<!--CODE:25_a_window_with_furniture-->

```basic
' A WINDOW WITH FURNITURE -- dock panes, a toolbar and a status bar. Under
' QT_QPA_PLATFORM=offscreen, so it runs with no screen.
'
' ALL THREE BELONG TO THE WINDOW rather than to a layout, which is the same
' rule a `menu` follows: a window holding any of them is a QMainWindow, and a
' QMainWindow places its dock areas, its status bar and its tool bars around
' the content itself. A window that declares none of them is untouched.

window ide( title: "Workbench", width: 880, height: 560 )
    toolbar main
        item open( label: "Open" )
        separator()
        item save( label: "Save" )
    end toolbar

    menu "File"
        item quit( label: "Quit" )
    end menu

    column
        source code( line_numbers: true )
    end column

    ' PANES IN ONE BLOCK ARE TABBED ALONG THAT EDGE, which is what an IDE does
    ' with a column of panels -- five stacked panes means the ones below the
    ' fold are invisible.
    docks right
        dock branches "Branches"
            text b( label: "main" )
        end dock
        dock results "Results"
            memo out()
        end dock
    end docks

    ' A SECOND BLOCK IS A SECOND EDGE.
    docks bottom
        dock console "Console"
            memo log()
        end dock
    end docks

    status
        text msg( label: "Ready" )
        stretch()
        text pos( label: "1:1" )
    end status
end window

program main( args )
    if not qt.can_display() then
        print "no display -- nothing to show"
        return
    end if

    qt.open(ide)

    ' WHERE A PANE IS, asked of the window rather than of the declaration.
    print "branches: " + qt.dock(ide, "branches").area + ", sharing a tab group with " + string(qt.dock(ide, "branches").tabbed_with)
    print "console:  " + qt.dock(ide, "console").area + ", sharing a tab group with " + string(qt.dock(ide, "console").tabbed_with)

    ' A STATUS BAR NEEDS NO VERB: a `text` part in it is an ordinary widget
    ' whose `text` property `qt.set` reaches, which is why there is nothing
    ' like `qt.status(...)` to learn.
    print "status says:              " + qt.read(ide, "msg", "text")
    qt.set(ide, "msg", "text", "Saved notes.bas")
    print "and after qt.set:         " + qt.read(ide, "msg", "text")

    ' A TOOLBAR HOLDS WHAT A MENU HOLDS -- the same items, so `checkable:` and
    ' `icon:` work on them too.
    print "a toolbar item's label:   " + qt.read(ide, "open", "text")
    qt.set(ide, "save", "checkable", true)
    print "and one can be checkable: " + string(qt.read(ide, "save", "checkable"))

    ' KEEP THE ARRANGEMENT. The text is Qt's own format -- store it, do not
    ' read it; `qt.dock` is what answers questions about a pane.
    saved = qt.layout(ide)
    print "the layout is text, kept as " + string(len(saved)) + " characters"

    ' A PERSON DRAGS A PANE OUT; the program puts it back.
    qt.set(ide, "branches", "floating", true)
    print "floated:                  " + string(qt.dock(ide, "branches").floating)
    print "restored:                 " + string(qt.restore_layout(ide, saved))
    print "floating now:             " + string(qt.dock(ide, "branches").floating)

    ' AND AN ARRANGEMENT SAVED BEFORE THE WINDOW CHANGED IS REPORTED, NOT
    ' RAISED -- an ordinary thing to find on startup.
    print "an unrecognised layout:   " + string(qt.restore_layout(ide, "bm90IG1pbmU="))

    qt.close(ide)
end program
```

<!--OUT:25_a_window_with_furniture-->

```
branches: right, sharing a tab group with 1
console:  bottom, sharing a tab group with 0
status says:              Ready
and after qt.set:         Saved notes.bas
a toolbar item's label:   Open
and one can be checkable: true
the layout is text, kept as 336 characters
floated:                  true
restored:                 true
floating now:             false
an unrecognised layout:   false
```

## 26. The tab that was clicked

A notebook whose right-click menu acts on the tab **under the pointer**, and a
`Copy file path` that works.

**A right-click on a tab does not make it the current tab**, and that is Qt
behaving correctly: right-clicking a background tab to close it must not first
switch you to it. So a handler that reads `currentIndex` acts on whichever tab
was *already* active — it closes the wrong document and raises nothing.
`qt.which_tab()` answers the one the event is about.

It answers **`unknown`** when the click was not over a tab, which is ordinary
rather than a corner: a right-click on the page emits the same signal, and the
page is where the text is. It also answers the index `tabCloseRequested`
carries, which is what makes `tabsClosable: true` usable — a zero-argument
handler cannot see that signal's argument either.

**`qt.right_click(win, part, n)` aims at a tab's own rectangle**, through the
tab bar, which is where a real click lands — the same third argument that aims
at a table or a tree row.

**`qt.copy(text)` takes no window.** There is one clipboard per application, so
there is nothing to name; `qt.clipboard()` reads it back, answering `unknown`
when there is no text on it rather than `""`. It copies **eagerly**, so the text
is there the moment the call returns — worth knowing because the GTK equivalent
does not, and a buffer that goes out of scope there gives a successful-looking
copy and an empty paste. A `{file}` or `{dir}` reference copies as its path. The
clipboard belongs to the windowing system, so there is none without a display
and `qt.copy` says so rather than naming a window.

The menu **stays up** until something dismisses it, and a visible menu grabs the
mouse — so a program driving itself calls `qt.invoke(win, menu, "hide")` before
doing anything else.

<!--CODE:26_the_tab_that_was_clicked-->

```basic
' THE TAB THAT WAS CLICKED, AND THE CLIPBOARD -- a notebook whose right-click
' menu acts on the tab under the pointer, and a `Copy file path` that works.
' Under QT_QPA_PLATFORM=offscreen, so it runs with no screen.
'
' THE TRAP THIS RECIPE IS ABOUT: a right-click on a tab does NOT make it the
' current tab, and that is Qt behaving correctly -- right-clicking a background
' tab to close it must not first switch you to it. So a handler that reads
' `currentIndex` acts on whichever tab was ALREADY ACTIVE, which closes the
' wrong document and raises nothing. `qt.which_tab()` answers the one the event
' is about.
'
' IT ANSWERS `unknown` when the click was not over a tab, which is ordinary
' rather than a corner: a right-click on the PAGE emits the same signal, and
' that is where the text is.

window app( title: "Notebook", width: 520, height: 300 )
    popup tab_menu
        item copy_path( label: "Copy file path" )
        item close_one( label: "Close" )
    end popup

    column
        tabs book
            tab "notes.bas"
                memo one( plainText: "' the first file" )
            end tab
            tab "util.bas"
                memo two( plainText: "' the second file" )
            end tab
            tab "main.bas"
                memo three( plainText: "' the third file" )
            end tab
        end tabs

        text said( label: "right-click a tab" )
    end column

    on book.customContextMenuRequested
        ' ASK WHICH TAB, then decide what the menu may offer for it. A program
        ' that reads `currentIndex` here is reading the wrong tab.
        which = qt.which_tab()
        if is_unknown(which) then
            qt.set(app, "said", "text", "that was the page, not a tab")
        else
            g.aimed = which
            qt.set(app, "said", "text",
                   "right-clicked " + g.paths[which])
            shown = qt.popup(app, "tab_menu")
        end if
    end on

    on copy_path.triggered
        ' AND THE CLIPBOARD TAKES NO WINDOW: there is one per application, so
        ' there is nothing to name. It copies eagerly, so the text is on the
        ' clipboard the moment this returns.
        ok = qt.copy(g.paths[g.aimed])
        qt.set(app, "said", "text", "copied " + qt.clipboard())
    end on
end window

program main(args)
    g = { aimed: 0,
          paths: [ "/src/notes.bas", "/src/util.bas", "/src/main.bas" ] }

    if not qt.can_display() then
        print "no display, so no window -- and no clipboard either"
        return
    end if

    qt.open(app)
    qt.set(app, "book", "currentIndex", 0)
    print "the first tab is current: " + string(qt.read(app, "book", "currentIndex"))

    ' A PERSON WOULD RIGHT-CLICK THE THIRD TAB. `qt.right_click`'s third
    ' argument aims at a tab's own rectangle, through the tab bar, which is
    ' where a real click lands.
    qt.right_click(app, "book", 2)
    print "the status line says:   " + qt.read(app, "said", "text")
    print "and the current tab is still: " + string(qt.read(app, "book", "currentIndex"))

    ' THE MENU IS STILL UP and a visible menu grabs the mouse, so a program
    ' driving itself hides it before doing anything else.
    qt.invoke(app, "tab_menu", "hide")

    ' NOW THE MENU ITEM, which is what a person would click next.
    qt.invoke(app, "copy_path", "trigger")
    print "after Copy file path:   " + qt.read(app, "said", "text")
    print "the clipboard holds:    " + qt.clipboard()

    ' AND A RIGHT-CLICK THAT MISSES EVERY TAB: the page emits the same signal.
    qt.right_click(app, "book")
    print "clicking the page:      " + qt.read(app, "said", "text")

    qt.close(app)
end program
```

<!--OUT:26_the_tab_that_was_clicked-->

```
the first tab is current: 0
the status line says:   right-clicked /src/main.bas
and the current tab is still: 0
after Copy file path:   copied /src/main.bas
the clipboard holds:    /src/main.bas
clicking the page:      that was the page, not a tab
```

