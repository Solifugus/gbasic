' A `table` -- the directive §7 of docs/qt_gui_design.md decided and nothing
' rendered. Runs under QT_QPA_PLATFORM=offscreen, which is real Qt, no display.
'
' SELF-CHECKING AND FORCED. Every defect a table can have leaves A GRID THAT
' LOOKS LIKE A TABLE: columns in the wrong order put real values under the
' wrong headings, a population that never ran looks like an empty result set,
' a row count off by one hides the last order, and a fold that lost its kinds
' still prints the same digits. None of them raises, so a golden would record
' the damaged grid as expected and defend it.
'
' THE SHAPE DIFFERS FROM A FORM IN ONE STRUCTURAL WAY and that is what most of
' this file is about: a form shows ONE record, a table shows MANY, so the
' subject is a LIST and each `field` is a COLUMN read from every element rather
' than a widget read once.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window ledger( title: "Ledger", width: 520, height: 300 )
    column( margin: 8, spacing: 8 )
        table orders to show lines
            field placed( label: "Date" )
            field who(    label: "Customer" )
            field total(  label: "Total", type: {USD} )
        end table
        row
            stretch()
            button close( text: "Close" )
        end row
    end column

    ' A HANDLER MAY NAME A TABLE. Only leaves registered a name before this, so
    ' `on orders.activated` on a perfectly real table was refused as naming
    ' nothing in the window -- the refusal that exists to catch a handler which
    ' can never fire, firing on one that can.
    ' `itemSelectionChanged` RATHER THAN `activated`, and the choice is what
    ' makes this provable: `activated` needs a double-click or Enter, which no
    ' fixture can produce, where `itemSelectionChanged` is a no-arg signal
    ' `qt.select` emits -- so the whole path (a handler whose target is a
    ' CONTAINER, connected by name, re-entering the evaluator) is driven
    ' without a human. A handler that was connected and never fired would
    ' otherwise look identical to one that worked.
    on orders.itemSelectionChanged
        print "SIGNAL orders.itemSelectionChanged"
    end on
end window

window sheet( title: "Sheet" )
    table stock to edit items
        field name( label: "Name" )
        field qty(  label: "Qty" )
    end table
end window

' A TEXT-ONLY EDIT TABLE. It exists because the fold-reads-the-widgets proof
' needs a cell whose EMPTY value is still legal: emptying a NUMBER column
' correctly REFUSES, which proves the fold reads the view but gives no value to
' compare. Both halves are asserted, on the table each one can be asserted on.
window notes_sheet( title: "Notes" )
    table notes to edit memos
        field tag(  label: "Tag" )
        field body( label: "Body" )
    end table
end window

window empty_table( title: "Empty" )
    table nothing_yet to show none_at_all
        field a( label: "A" )
    end table
end window

window bad_subject( title: "BadSubject" )
    table t1 to show notalist
        field a( label: "A" )
    end table
end window

window bad_element( title: "BadElement" )
    table t2 to show mixed
        field a( label: "A" )
    end table
end window

program main()
    lines = [ { placed: "2026-01-04", who: "Ada",   total: {USD}"120.50" },
              { placed: "2026-01-09", who: "Grace", total: {USD}"88.00"  },
              { placed: "2026-02-02", who: "Alan",  total: {USD}"1500.00" } ]
    items = [ { name: "bolt", qty: 4 }, { name: "nut", qty: 12 } ]
    memos = [ { tag: "a", body: "first" }, { tag: "b", body: "second" } ]

    check("a show table opens", qt.open(ledger), true)
    check("an edit table opens", qt.open(sheet), true)

    ' ------------------------------------------------- SHAPE, FROM THE DECLARATION
    check("one column per declared field",
          qt.read(ledger, "orders", "columnCount"), 3)
    check("one row per element of the subject",
          qt.read(ledger, "orders", "rowCount"), 3)
    check("and a different table gets its own shape",
          qt.read(sheet, "stock", "columnCount"), 2)

    ' ------------------------------------------------------------- POPULATION
    ' ASSERTED CELL BY CELL AND ADDRESSED BY THE COLUMN'S OWN NAME, because a
    ' column NUMBER is the one thing that changes when the declaration is
    ' reordered -- and then a check reads a real value from the wrong column
    ' and passes. The values must also DIFFER between rows and between columns,
    ' or every check here is satisfied by a backend writing one value into
    ' every cell it can find.
    check("the first row's text comes from the first element",
          qt.cell(ledger, "orders", 0, "who"), "Ada")
    check("the last row's from the last", qt.cell(ledger, "orders", 2, "who"),
          "Alan")
    check("a middle row is not the first either",
          qt.cell(ledger, "orders", 1, "who"), "Grace")
    check("money is written in gBASIC's own words",
          qt.cell(ledger, "orders", 0, "total"), "120.50")
    check("so a column is read from its OWN field",
          qt.cell(ledger, "orders", 0, "placed"), "2026-01-04")
    check("and two columns of one row DIFFER",
          qt.cell(ledger, "orders", 0, "who") = qt.cell(ledger, "orders", 0, "placed"),
          false)

    ' ------------------------------------------------- THE MAPPING ORACLE
    ' THE CAPTION COMES FROM `label:` AND THE DATA FROM `name` -- two
    ' different sources in the declaration -- so this is the only tier that
    ' pins the column MAPPING independently of the mapping itself. Everything
    ' above reads cells through the same by-name route population writes them
    ' with, so if BOTH were permuted the same way every check would pass while
    ' every value sat under the wrong heading. Agreement is not correctness.
    '
    ' FOUND BY PERTURBATION, not by design: "every column reads the FIRST
    ' field" and "a cell ignores the name and reads column 0" produced
    ' IDENTICAL output, which is the signature of checks that cannot separate
    ' two different defects.
    check("the first column's caption is the one declared for it",
          qt.heading(ledger, "orders", "placed"), "Date")
    check("and the second's is its own", qt.heading(ledger, "orders", "who"),
          "Customer")
    check("and the third's", qt.heading(ledger, "orders", "total"), "Total")
    ' AND THE TWO WAYS OF ASKING MUST AGREE, which is the half that makes this
    ' an oracle rather than a restatement. By NAME the caption is found through
    ' the same resolution `qt.cell` uses, so a PERMUTED resolution answers both
    ' consistently and neither can witness it; by POSITION the header is read
    ' in the order the declaration wrote it. Requiring agreement pins the
    ' name-to-column mapping against something that does not depend on it.
    '
    ' MEASURED: with only the by-name form, the two perturbations above gave
    ' identical output and this tier added nothing.
    check("asking by name and by position agree for column 0",
          qt.heading(ledger, "orders", "placed") = qt.heading(ledger, "orders", 0),
          true)
    check("and for column 1",
          qt.heading(ledger, "orders", "who") = qt.heading(ledger, "orders", 1),
          true)
    check("and for column 2",
          qt.heading(ledger, "orders", "total") = qt.heading(ledger, "orders", 2),
          true)
    ' AND THE CAPTIONS COME FROM THE DECLARATION ITSELF, read back as data --
    ' so the expected text is not a literal somebody could edit to match.
    cols = ledger.items[0].items[0].items
    check("the captions are the declaration's own labels, in its own order",
          qt.heading(ledger, "orders", 0) = cols[0].options.label and
          qt.heading(ledger, "orders", 2) = cols[2].options.label, true)
    check("so `Customer` sits over the customer, not over the date",
          qt.heading(ledger, "orders", "who") = "Date", false)

    ' --------------------------------------------------------- DIRECTION
    ' A `show` TABLE IS NOT EDITABLE AND IS STILL SELECTABLE, which is why it
    ' gets a third answer rather than the form's: a QTableWidget has no
    ' `readOnly`, and DISABLING it would stop a person choosing a row -- and
    ' choosing a row is reading, not editing.
    check("a show table takes no editing",
          qt.read(ledger, "orders", "editTriggers"), "NoEditTriggers")
    check("CONTROL: an edit table does",
          qt.read(sheet, "stock", "editTriggers") = "NoEditTriggers", false)
    check("and a show table is still enabled, so a row can be chosen",
          qt.read(ledger, "orders", "enabled"), true)

    ' --------------------------------------------------------- SELECTION
    ' NOTHING CHOSEN ANSWERS `unknown`, not -1 and not 0. Both of those are
    ' valid-looking indices, so either would let a program act on a row nobody
    ' picked -- the answer `materiality` and `principal()` already give for
    ' "nobody said".
    check("with nothing chosen, there is no row",
          is_unknown(qt.selected(ledger, "orders")), true)
    ' `qt.select` IS HOW A PROGRAM CHOOSES ONE -- a window opening with
     ' yesterday's order highlighted, or a row just added being made current.
    check("choosing a row answers", qt.select(ledger, "orders", 1), true)
    check("and that row is what comes back",
          qt.selected(ledger, "orders"), 1)
    check("and it is a number, so it can index the subject",
          type(qt.selected(ledger, "orders")), "number")
    ' GUARDED, because `lines[unknown]` RAISES -- and a perturbation that makes
    ' selection answer nothing would then kill the fixture and hide the two
    ' mismatches above, which had already named the problem. run_discovery.sh
    ' records that trap and this increment's own binding fixture hit it.
    at = qt.selected(ledger, "orders")
    check("which is what makes it compose with the list the program holds",
          not is_unknown(at) and lines[at].who = "Grace", true)
    ' AND IT CAN BE UNCHOSEN, so "nobody has chosen" is sayable in both
    ' directions rather than only readable.
    check("choosing nothing clears it", qt.select(ledger, "orders", unknown), true)
    check("and there is no row again",
          is_unknown(qt.selected(ledger, "orders")), true)

    ' ------------------------------------------------------------- THE FOLD
    ' THE RECORD FOLLOWS THE WIDGETS for a table exactly as for a form, and the
    ' kinds survive: a cell holds TEXT, so folding back blindly turns
    ' `{ qty: 4 }` into `{ qty: "4" }`, which still prints 4.
    ' GUARDED, because a perturbation that leaves cells EMPTY makes the number
    ' column's fold refuse -- correctly -- and an unguarded raise here would
    ' end the fixture and take every later tier with it. Measured: two
    ' perturbations did exactly that, and the mismatches above only survived
    ' because of --line-buffered. run_discovery.sh's recorded trap.
    on error goto next
    before = qt.subject(sheet, "items")
    if error then
        print "MISMATCH the fold of an untouched edit table raised: " + error.message
        error.clear()
        before = [ { name: "", qty: 0 }, { name: "", qty: 0 } ]
    end if
    check("the fold returns one row per table row", len(before), 2)
    check("a number column folds back AS a number", type(before[0].qty),
          "number")
    check("with its value", before[0].qty, 4)
    check("and text stays text", before[1].name, "nut")

    ' AND IT READS THE WIDGETS rather than handing back the capture it already
    ' holds -- which every check above is satisfied without, the capture and
    ' the cells agreeing until something changes one. `clearContents` is a
    ' no-arg Qt slot, so `qt.invoke` can empty the view with no human in front
    ' of it, and the fold must then report what the view says.
    check("a text-only edit table opens", qt.open(notes_sheet), true)
    kept = qt.subject(notes_sheet, "memos")
    check("CONTROL: its fold agrees with the capture to begin with",
          kept[1].body, "second")
    x = qt.invoke(notes_sheet, "notes", "clearContents")
    emptied = qt.subject(notes_sheet, "memos")
    check("the fold reports the emptied cell, not the captured value",
          emptied[1].body, "")

    ' AND EMPTYING A NUMBER COLUMN IS REFUSED rather than folded to zero -- a
    ' zero would be an ordinary-looking quantity nobody entered. The sentence
    ' is the MODIFIER'S own, which is the cause; what it cannot say is which
    ' row, the same stated gap the form's `type:` refusal has.
    x = qt.invoke(sheet, "stock", "clearContents")
    on error goto next
    y = qt.subject(sheet, "items")
    if error then
        print "EMPTYNUMBER: " + error.message
        error.clear()
    else
        print "MISMATCH an emptied number column folded back anyway"
    end if

    ' ------------------------------------------------------------- REFUSALS
    notalist = { a: 1 }
    x = qt.open(bad_subject)
    if error then
        print "NOTALIST: " + error.message
        error.clear()
    else
        print "MISMATCH a table bound to a record opened quietly"
    end if

    mixed = [ { a: 1 }, 7 ]
    x = qt.open(bad_element)
    if error then
        print "BADELEMENT: " + error.message
        error.clear()
    else
        print "MISMATCH a table over a list of numbers opened quietly"
    end if

    ' AN EMPTY LIST IS NOT AN ERROR -- a ledger with no orders yet is an
    ' ordinary state, and a table of nothing is the honest rendering of it.
    none_at_all = []
    check("an empty list opens", qt.open(empty_table), true)
    check("and the table has no rows",
          qt.read(empty_table, "nothing_yet", "rowCount"), 0)
    check("while still having its column",
          qt.read(empty_table, "nothing_yet", "columnCount"), 1)

    x = qt.cell(ledger, "orders", 0, "nosuch")
    if error then
        print "BADCOLUMN: " + error.message
        error.clear()
    end if
    x = qt.selected(ledger, "close")
    if error then
        print "NOTATABLE: " + error.message
        error.clear()
    end if
end program
