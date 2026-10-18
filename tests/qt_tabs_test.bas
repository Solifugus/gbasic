' `tabs` / `tab "Name"` AND `qt.set` -- §2.5's tab widget, and the write half
' of the by-name property route. Under QT_QPA_PLATFORM=offscreen: real Qt, no
' display.
'
' MEASURED BEFORE A LINE WAS WRITTEN, which shaped both halves. `tabs` and
' `tab "General"` ride productions that already existed (the bare container and
' the `group "Credit"` labelled form), so they cost NO grammar. A NAMED
' container with no options costs none either -- but one WITH options
' (`tabs settings( movable: true )` and a body) costs FOUR reduce/reduce and
' bison reports the rule useless, because after `IDENT IDENT ( ... ) NEWLINE`
' the parser cannot tell a named LEAF from a container head. So the vocabulary
' carries A NAME OR OPTIONS AND NOT BOTH, and `qt.set` is what makes that
' affordable rather than a thing to design around.
'
' SELF-CHECKING AND FORCED: a tab that was never added is a book with fewer
' pages, a page built without a layout is an empty tab, and a property set on
' the wrong object is a widget that looks untouched. None of them raises.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window prefs( title: "Preferences", width: 440, height: 320 )
    column( margin: 8, spacing: 8 )
        tabs settings
            tab "General"
                field who(  label: "Name" )
                check live( label: "Live" )
            end tab
            tab "Advanced"
                field depth( label: "Depth" )
                ' A TABLE INSIDE A TAB, which is the payoff of a page being a
                ' COLUMN rather than a bare QWidget: every other directive
                ' works inside one with no further vocabulary.
                table rows_v to show lines
                    field a( label: "A" )
                end table
            end tab
        end tabs
        row
            stretch()
            button go( text: "Go", enabled: false )
        end row
    end column

    menu "File"
        item quit_app( label: "Quit" )
    end menu

    on settings.currentChanged
        print "SIGNAL settings.currentChanged"
    end on
end window

program main()
    lines = [ { a: "one" }, { a: "two" } ]
    check("a window with tabs opens", qt.open(prefs), true)

    ' --------------------------------------------------------------- THE BOOK
    check("one page per declared tab", qt.read(prefs, "settings", "count"), 2)
    check("and the first is showing",
          qt.read(prefs, "settings", "currentIndex"), 0)

    ' A PAGE IS A COLUMN, so everything works inside one. Asserted through
    ' parts on BOTH pages, since a book that built only its first page would
    ' satisfy a check on page one alone.
    check("a part on the first page is reachable",
          qt.write(prefs, "who", "Ada"), true)
    check("and reads back", qt.read(prefs, "who"), "Ada")
    check("a part on the SECOND page too", qt.write(prefs, "depth", "9"), true)
    check("and reads back", qt.read(prefs, "depth"), "9")
    check("a whole TABLE inside a tab is built and filled",
          qt.cell(prefs, "rows_v", 1, "a"), "two")

    ' AND WHAT IS OUTSIDE THE BOOK IS STILL LAID OUT -- a QTabWidget has no
    ' layout of its own, so a backend that mistook it for one would swallow
    ' the row beside it.
    check("the row beside the tabs is still there",
          qt.read(prefs, "go", "text"), "Go")

    ' ------------------------------------------------ qt.set: THE WRITE HALF
    ' THE ORDINARY CASE FIRST, because it is the one with no workaround before
    ' this: a program wants to enable a button once something has happened,
    ' and the declaration is written before anything happens.
    check("a button starts disabled, as declared",
          qt.read(prefs, "go", "enabled"), false)
    check("setting it answers", qt.set(prefs, "go", "enabled", true), true)
    check("and it took effect", qt.read(prefs, "go", "enabled"), true)

    ' A NAMED CONTAINER IS CONFIGURED HERE rather than in its head, which is
    ' what makes "a name or options, not both" affordable.
    check("a named container's property starts at Qt's default",
          qt.read(prefs, "settings", "movable"), false)
    check("and can be set", qt.set(prefs, "settings", "movable", true), true)
    check("which took effect", qt.read(prefs, "settings", "movable"), true)

    ' AND IT REACHES A QAction, so the write half covers everything the read
    ' half does -- a menu item is a QObject and not a widget.
    check("a menu item's property can be set",
          qt.set(prefs, "quit_app", "enabled", false), true)
    check("and reads back", qt.read(prefs, "quit_app", "enabled"), false)

    ' SWITCHING PAGE IS A PROPERTY, which is also what fires the book's own
    ' signal -- so the handler is driven with no human in front of it.
    check("the page can be changed",
          qt.set(prefs, "settings", "currentIndex", 1), true)
    check("and the book says so",
          qt.read(prefs, "settings", "currentIndex"), 1)

    ' ------------------------------------------------------------- REFUSALS
    ' THE TWO HALVES OF ONE ROUTE MUST AGREE ABOUT WHAT A PROPERTY NAME IS,
    ' which is the rule `qt.read`'s own refusal already follows: a setter that
    ' accepted an unknown name would store a dynamic property nothing reads --
    ' an option silently doing nothing.
    on error goto next
    x = qt.set(prefs, "go", "texx", "x")
    if error then
        print "BADPROP: " + error.message
        error.clear()
    else
        print "MISMATCH an unknown property was accepted by qt.set"
    end if
    x = qt.set(prefs, "nope", "text", "x")
    if error then
        print "BADPART: " + error.message
        error.clear()
    else
        print "MISMATCH an absent part was accepted by qt.set"
    end if
end program
