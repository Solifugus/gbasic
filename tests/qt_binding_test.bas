' TWO-WAY BINDING: `form to show` AND `form to edit` (docs/qt_gui_design.md §2.2).
' Runs under QT_QPA_PLATFORM=offscreen, which is real Qt with no display.
'
' SELF-CHECKING AND FORCED, more sharply than the backend fixture beside it:
' every defect here leaves A WINDOW WITH PLAUSIBLE VALUES IN IT. A form that
' populated from the wrong field still shows text; one that never populated
' shows an empty field, which is what a new record looks like; a fold-back that
' turned every field into text still prints the same digits; and a `show` form
' that quietly accepted typing looks exactly like one that did not, because
' nothing raises either way. A golden would record any of them as expected.
'
' SO THE DIRECTION IS ASSERTED AS A DIFFERENCE THROUGHOUT. `to show` and
' `to edit` populate IDENTICALLY and part company on two things only -- whether
' the parts take typing, and whether the record follows them -- so a check on
' either one alone passes on a backend that treats both directions the same.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window editor( title: "Editor" )
    form to edit person
        field name(  label: "Name" )
        field age(   label: "Age" )
        field owed(  label: "Owed", type: {USD} )
        check active( label: "Active" )
        button save( label: "Save" )
    end form
end window

window viewer( title: "Viewer" )
    form to show person
        field name(  label: "Name" )
        check active( label: "Active" )
    end form
end window

window fresh_form( title: "New" )
    form to edit blank
        field name( label: "Name" )
        field town( label: "Town" )
    end form
end window

window orphan( title: "Orphan" )
    form to edit nowhere
        field a( label: "A" )
    end form
end window

window wrongkind( title: "WrongKind" )
    form to edit astring
        field a( label: "A" )
    end form
end window

program main()
    person = { name: "Ada Lovelace", age: 36, owed: {USD}"500.00", active: true }

    check("the edit form opens", qt.open(editor), true)
    check("the show form opens", qt.open(viewer), true)

    ' ---------------------------------------------------- POPULATION, BOTH WAYS
    ' THE BINDING IS THE NAME (§2.2), so there is nothing to look up -- and the
    ' two directions must populate the SAME, or a later difference could be a
    ' difference in populating rather than in direction.
    check("a bound field is filled from the record", qt.read(editor, "name"),
          "Ada Lovelace")
    check("a number arrives in gBASIC's own words", qt.read(editor, "age"), "36")
    check("so does money", qt.read(editor, "owed"), "500.00")
    check("and a boolean reaches the box", qt.read(editor, "active"), "true")
    check("a SHOW form populates identically", qt.read(viewer, "name"),
          "Ada Lovelace")
    check("including its box", qt.read(viewer, "active"), "true")
    ' THE FIELDS MUST DIFFER FROM EACH OTHER, or every check above is satisfied
    ' by a backend writing one value into every part it can find.
    check("and the parts hold DIFFERENT values",
          qt.read(editor, "name") = qt.read(editor, "age"), false)

    ' ------------------------------------------------- DIRECTION, DIFFERENCE 1
    ' A ONE-WAY FORM TAKES NO TYPING, and the reason is not neatness: the
    ' alternative is a SILENT DISCARD -- somebody types into a display form,
    ' nothing complains, and `qt.subject` never reports it. Which property says
    ' so is a fact about the widget, so a QLineEdit answers `readOnly` (which
    ' still allows selecting and copying, what a reader wants) and a QCheckBox,
    ' having no such property, is disabled instead.
    check("a show form's field is read-only", qt.read(viewer, "name", "readOnly"),
          true)
    check("CONTROL: an edit form's field is NOT",
          qt.read(editor, "name", "readOnly"), false)
    check("a show form's box is disabled", qt.read(viewer, "active", "enabled"),
          false)
    check("CONTROL: an edit form's box is NOT",
          qt.read(editor, "active", "enabled"), true)

    ' ------------------------------------------------- DIRECTION, DIFFERENCE 2
    ' THE RECORD FOLLOWS THE WIDGETS, read AT THE MOMENT IT IS ASKED. §2.2
    ' proposed a watcher per field in each direction; the second direction
    ' cannot work that way, because a widget's signal fires inside
    ' QApplication::exec() beneath `qt.run()` and the frame current then is
    ' whichever one called `qt.run` -- there is no frame to write back into. A
    ' record being a VALUE is what makes reading on demand enough.
    w = qt.write(editor, "name", "Ada L")
    w = qt.write(editor, "age", "37")
    w = qt.write(editor, "owed", "612.50")
    w = qt.write(editor, "active", false)
    after = qt.subject(editor, "person")
    check("the edited field follows", after.name, "Ada L")
    check("an edited box follows", after.active, false)

    ' THE KIND IS KEPT, which the obvious implementation gets wrong: a widget
    ' holds TEXT, so folding back blindly turns `{ age: 36 }` into
    ' `{ age: "37" }` -- a record whose shape silently changed, and which still
    ' PRINTS 37. So these are checks on `type` and not on the digits.
    check("a number folds back AS a number", type(after.age), "number")
    check("with the value typed", after.age, 37)
    check("`type: {USD}` folds back AS money", type(after.owed), "money")
    check("with the value typed", string(after.owed), "612.50")
    check("a box folds back AS a boolean", type(after.active), "boolean")

    ' AND THE CALLER'S OWN RECORD IS UNTOUCHED -- a record is a value, so the
    ' fold cannot reach back into the variable the subject was read from.
    check("the caller's record is unchanged", person.name, "Ada Lovelace")

    ' THE CONTROL THAT MAKES THE DIRECTION MEAN SOMETHING. A `show` form's
    ' record must NOT follow its widgets. Without this check, `to show` and
    ' `to edit` differ only in whether typing is possible, and a backend that
    ' folded both back would pass everything above.
    w = qt.write(viewer, "name", "typed over")
    held = qt.subject(viewer, "person")
    check("a SHOW form's record does not follow its widgets", held.name,
          "Ada Lovelace")

    ' ------------------------------------------------------- THE NEW-RECORD CASE
    ' AN ABSENT FIELD LEAVES THE PART EMPTY AND DOES NOT RAISE, the same answer
    ' `r.missing` gives on a record. THE COST IS STATED RATHER THAN HIDDEN: a
    ' MISSPELLED part name inside a form is therefore not caught either, and
    ' the refusal that would catch it is the one that breaks this case -- a
    ' form filling in a new record has every field absent until somebody types.
    blank = {}
    check("a form over an empty record opens", qt.open(fresh_form), true)
    check("and its parts are empty", qt.read(fresh_form, "name"), "")
    w = qt.write(fresh_form, "name", "Grace")
    made = qt.subject(fresh_form, "blank")
    check("a field absent at open is present after an edit", made.name, "Grace")
    check("one nobody typed in is present and empty", made.town, "")
    check("so the record gained both parts", len(keys(made)), 2)

    ' ------------------------------------------------------------- REFUSALS
    on error goto next
    x = qt.open(orphan)
    if error then
        print "NOSUBJECT: " + error.message
        error.clear()
    else
        print "MISMATCH a form bound to nothing opened quietly"
    end if

    astring = "not a record"
    x = qt.open(wrongkind)
    if error then
        print "NOTARECORD: " + error.message
        error.clear()
    else
        print "MISMATCH a form bound to a string opened quietly"
    end if

    x = qt.subject(editor, "nosuchsubject")
    if error then
        print "NOSUCHFORM: " + error.message
        error.clear()
    else
        print "MISMATCH an unbound name answered a record"
    end if

    ' A `type:` CAN REFUSE WHAT WAS TYPED, and it must refuse rather than fold
    ' back something that is not money.
    w = qt.write(editor, "owed", "not money at all")
    x = qt.subject(editor, "person")
    if error then
        print "BADVALUE: " + error.message
        error.clear()
    else
        print "MISMATCH text that is not money folded back anyway"
    end if
end program
