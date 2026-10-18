' `choice` -- one of a list, and the two doors its list can come through.
' Under QT_QPA_PLATFORM=offscreen: real Qt, no display.
'
' WHY IT EXISTS, decided by a real program rather than by Qt's catalogue. BAG's
' area editor gives every area six exits and each one NAMES ANOTHER AREA; an
' item names its location. A `field` means typing an id, and a `table` is a grid
' rather than a dropdown, so there was no substitute at all.
'
' AND WHY `list` WAS NOT ADDED, which is the same measurement answering the
' other way: a one-column `table` already IS a picker, `qt.selected` already
' addresses it, and since `qt.refresh` it follows a changing list. A `list`
' would be a second way to say that.
'
' TWO DOORS BECAUSE BAG NEEDS BOTH IN ONE PROGRAM: the directions a trigger
' can fire on are FIXED and the areas are NOT. So `options: [...]` is declared
' for the first, and `qt.options(win, part, list)` fills the second from data.
' Both go through ONE filler, because a choice filled one way and refilled the
' other must hold the same thing.
'
' SELF-CHECKING AND FORCED: a dropdown with the wrong items in it is still a
' dropdown. An empty one looks like a list nobody has filled; one holding the
' previous room's exits looks entirely ordinary. Neither raises.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

function refused_write(part, value)
    on error goto handle
    qt.write(ed, part, value)
    return "ACCEPTED"
handle:
    m = error.message
    error.clear()
    return m
end function

function refused_options(part, list)
    on error goto handle
    n = qt.options(ed, part, list)
    return "ACCEPTED " + string(n)
handle:
    m = error.message
    error.clear()
    return m
end function

window ed( title: "Editor", width: 460, height: 380 )
    column( margin: 8, spacing: 6 )
        form to edit area
            field name( label: "Name" )
            ' A MEMO IN A FORM, which is the other half of §0.10 being bound:
            ' a description is data, and a read-only `field` could not wrap.
            memo description( label: "Description" )
            ' A FIXED LIST, declared -- knowable at load time, so the record
            ' the declaration binds carries it BY VALUE.
            choice facing( label: "Facing",
                           options: ["north", "south", "east", "west"] )
            ' A LIST FROM DATA, filled after the window is open.
            choice leads_to( label: "Leads to" )
        end form

        ' A `to show` FORM, for the display-only half.
        form to show seen
            choice frozen( label: "Was", options: ["one", "two"] )
            memo prose( label: "Prose" )
        end form

        field plain( label: "Not a choice" )
    end column
end window

program main()
    area = { name: "The Garden",
             description: "A walled garden." + chr(10) + "Gone to seed.",
             facing: "south", leads_to: "" }
    seen = { frozen: "two", prose: "Read me." }
    places = ["(nowhere)", "The Cellar", "The Garden", "The Tunnel"]

    if not qt.can_display() then
        print "SKIP no display"
        return
    end if
    qt.open(ed)

    print "-- the DECLARED list, which the record carries by value"
    check("it has every item", qt.read(ed, "facing", "count"), 4)
    ' AND IT IS BOUND: the subject said "south", so that is what is showing --
    ' not item 0, which is what an unbound choice would show and which looks
    ' perfectly ordinary.
    check("and the subject chose one of them", qt.read(ed, "facing"), "south")
    check("by index", qt.read(ed, "facing", "currentIndex"), 1)

    print "-- the DATA list, filled after open"
    check("it starts empty", qt.read(ed, "leads_to", "count"), 0)
    check("qt.options answers how many it put in",
          qt.options(ed, "leads_to", places), 4)
    check("and they are there", qt.read(ed, "leads_to", "count"), 4)
    check("showing the first", qt.read(ed, "leads_to"), "(nowhere)")

    print "-- choosing, and refusing one that is not there"
    qt.write(ed, "leads_to", "The Tunnel")
    check("a value in the list is chosen", qt.read(ed, "leads_to"), "The Tunnel")
    ' `setCurrentText` ON AN UNEDITABLE BOX SILENTLY DOES NOTHING in Qt, so
    ' without this refusal a program setting an absent value would read back
    ' the OLD one and nothing would say so -- a plausible wrong answer, which
    ' is this backend's whole failure class.
    check("one that is not is refused BY NAME",
          contains(refused_write("leads_to", "Atlantis"),
                   "has no choice `Atlantis`"), true)
    check("and the old choice is untouched",
          qt.read(ed, "leads_to"), "The Tunnel")

    print "-- refilling CLEARS the selection, which is the honest behaviour"
    ' A LIST THAT HAS CHANGED MAY NOT CONTAIN WHAT WAS CHOSEN, and keeping an
    ' index into a list that has moved is how a form reports a value nobody
    ' picked.
    check("refilling answers the new count",
          qt.options(ed, "leads_to", ["only one"]), 1)
    check("and the selection moved with it",
          qt.read(ed, "leads_to"), "only one")

    print "-- a choice FOLDS BACK, which is what made it worth binding"
    ' MEASURED BEFORE THIS WORKED: `qt_bindable_word` admitted only `field`
    ' and `check`, so a choice rendered perfectly and bound not at all -- the
    ' widget plainly showed a value and the fold answered an empty string.
    shown = qt.options(ed, "leads_to", places)
    qt.write(ed, "leads_to", "The Cellar")
    back = qt.subject(ed, "area")
    check("the fold sees the chosen one", back.leads_to, "The Cellar")
    check("and the declared one too", back.facing, "south")

    print "-- a MEMO in a form binds too, both ways"
    check("it populated from the subject",
          len(split(qt.read(ed, "description"), chr(10))), 2)
    qt.write(ed, "description", "Changed.")
    back = qt.subject(ed, "area")
    check("and folds back", back.description, "Changed.")

    print "-- `to show` locks both of them"
    ' A QComboBox HAS NO `readOnly`, so display-only falls to `enabled` --
    ' which is right, a disabled box being unchangeable. A QPlainTextEdit does
    ' have one, so a memo keeps its scrollbar and loses its typing.
    check("a shown choice is disabled", qt.read(ed, "frozen", "enabled"), false)
    check("a shown memo is readOnly", qt.read(ed, "prose", "readOnly"), true)
    check("CONTROL: the edit form's are neither",
          qt.read(ed, "leads_to", "enabled"), true)
    check("CONTROL: nor its memo", qt.read(ed, "description", "readOnly"), false)
    ' AND THE SHOWN RECORD DOES NOT FOLLOW, which is the whole difference
    ' between the two words.
    kept = qt.subject(ed, "seen")
    check("a shown subject comes back untouched", kept.frozen, "two")

    print "-- refusals, each beside its nearest legal neighbour"
    check("a list of options on something that is not a choice is refused",
          contains(refused_options("plain", ["a"]), "is not a `choice`"), true)
    check("and an absent part says THAT instead",
          contains(refused_options("nope", ["a"]), "has no `nope`"), true)
    ' THE CONTROL for both: a real choice still takes a list.
    check("CONTROL: a real choice still accepts one",
          qt.options(ed, "facing", ["up", "down"]), 2)
    qt.stop()
end program
