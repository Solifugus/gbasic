' ICONS (§0.23) -- `icon:` on a leaf or a menu item, `qt.set(... "icon" ...)`,
' and `qt.has_icon`. Under QT_QPA_PLATFORM=offscreen, real QIcons, no display.
'
' Takes the path of a real PNG as its one argument, made by the runner, because
' gBASIC can write the bytes but a committed binary fixture nothing can
' reproduce is a fixture that tests itself.
'
' IT IS THE ONE PROPERTY THE BY-NAME ROUTE CANNOT CARRY, and that is measured
' rather than assumed: `setProperty("icon", QString(...))` returns FALSE for
' both a theme name and a file path, and the icon stays null -- QVariant will
' not convert a string to a QIcon. So `icon` has a branch of its own, in the
' ONE door the declaration's options and `qt.set` both go through, which is
' what makes them unable to disagree about what an icon source is.
'
' THREE SOURCES, TOLD APART BY FACTS AND NOT BY GUESSES. A `SP_` name is one of
' QStyle's 81 StandardPixmap keys -- Qt's own namespace -- and measured, those
' work with NO icon theme installed, which is the only source that always does.
' An existing FILE is asked of the filesystem, so it is a fact rather than a
' shape. Anything else is a freedesktop theme name.
'
' AND THE TWO FAILURES ARE DIFFERENT IN KIND. A path that is not there is a
' fact we can check and an intent that is unambiguous, so it is REFUSED. A
' theme name that resolves to nothing is a fact about THE MACHINE -- measured,
' this one has no icon theme at all (`themeName()` empty, `hasThemeIcon
' ("document-save")` false) -- so refusing would make `icon: "document-save"`
' fail on exactly the headless machines the release targets. It WARNS, which is
' the position §0.14 takes for a missing syntax definition and for the same
' reason: the program is perfectly usable without the picture, and the failure
' is otherwise INVISIBLE, a button with no icon looking exactly like one that
' was never meant to have one.
'
' `qt.has_icon` IS THE ORACLE AND IT WAS NOT FORESEEN. It came from asking what
' a check could observe: a QIcon has NO TEXT FORM, and measured, a button
' carrying a real PNG and one carrying nothing BOTH answered "" through
' `qt.read` -- so without it every check here would pass on a backend that
' resolved nothing, and `qt.read` on an icon would be answering a wrong answer
' rather than no answer. It refuses now and names this instead.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

' DECLARED ICONS, from the two sources that work everywhere. A theme name is
' deliberately NOT declared here -- it would warn on this machine and the
' fixture asserts clean stderr; the warning gets its own tier in the runner.
window app( title: "Icons", width: 420, height: 300 )
    menu "File"
        item save_it( label: "Save", icon: "SP_DialogSaveButton" )
        item plain_it( label: "No picture" )
    end menu
    column
        button standard( label: "Standard", icon: "SP_DialogOkButton" )
        button bare( label: "Bare" )
        check tick( label: "Tick" )
        text words( label: "Just words" )
        table grid to show rows
            field name( label: "Name" )
        end table
    end column
end window

program main(args)
    rows = [{ name: "a" }]
    png = args[0]

    qt.open(app)

    ' ---- a standard name, which needs no theme at all --------------------
    check("a declared SP_ name gave a button an icon",
          qt.has_icon(app, "standard"), true)
    check("and a menu item one too", qt.has_icon(app, "save_it"), true)
    ' THE CONTROL: without it, "it has an icon" is satisfied by a backend that
    ' reports true for everything.
    check("a part that declared none has none", qt.has_icon(app, "bare"), false)
    check("and nor does a menu item that declared none",
          qt.has_icon(app, "plain_it"), false)

    ' ---- a real file ------------------------------------------------------
    qt.set(app, "bare", "icon", png)
    check("a file path gave it one", qt.has_icon(app, "bare"), true)

    ' ---- clearing is legal and silent ------------------------------------
    ' A program swapping a play icon for a pause one has to be able to take one
    ' away, and falling through would ask the theme for an icon called "".
    qt.set(app, "bare", "icon", "")
    check("an empty source clears it", qt.has_icon(app, "bare"), false)
    qt.set(app, "bare", "icon", "SP_DialogCancelButton")
    check("and it can be set again", qt.has_icon(app, "bare"), true)

    ' ---- a check box carries one too, since it is a QAbstractButton ------
    qt.set(app, "tick", "icon", png)
    check("a check box can carry an icon", qt.has_icon(app, "tick"), true)

    ' ---- refusals, each beside its nearest legal neighbour ---------------
    on error goto next
    ' A `SP_` NAME QT DOES NOT KNOW IS A MISSPELLING, not a machine without a
    ' theme -- Qt's own namespace -- so this one is refused where a theme name
    ' warns. Both halves of that distinction are asserted, here and in the
    ' runner's warning tier.
    qt.set(app, "bare", "icon", "SP_NoSuchThing")
    check("an unknown SP_ name is refused", error.message,
          "qt.set: `SP_NoSuchThing` is not one of Qt's standard icon names")
    error.clear()

    qt.set(app, "bare", "icon", "/no/such/dir/missing.png")
    check("a path that is not there is refused, naming it", error.message,
          "qt.set: there is no icon file at `/no/such/dir/missing.png`")
    error.clear()

    qt.set(app, "bare", "icon", args[1])
    check("a file that is not an image is refused", error.message,
          "qt.set: `" + args[1] + "` is not an image Qt can read")
    error.clear()

    ' A PART THAT CANNOT CARRY ONE names its kind, because "no icon" would read
    ' as an answer rather than as a category mistake.
    qt.has_icon(app, "words")
    check("a text cannot carry an icon", error.message,
          "qt.has_icon: a QLabel cannot carry an icon")
    error.clear()

    qt.has_icon(app, "grid")
    check("and nor can a table", error.message,
          "qt.has_icon: a QTableWidget cannot carry an icon")
    error.clear()

    qt.has_icon(app, "nope")
    check("an unknown part is refused", error.message,
          "qt.has_icon: this window has no `nope`")
    error.clear()

    ' ---- `qt.read` ON AN ICON REFUSES AND NAMES THE REMEDY ---------------
    ' Measured, a QIcon's text form is EMPTY whether or not an icon is set, so
    ' answering "" would be a WRONG answer rather than no answer -- which is
    ' worse, and is what this refusal exists for.
    qt.read(app, "standard", "icon")
    check("reading an icon as text is refused", error.message,
          "qt.read: an icon has no text form; ask `qt.has_icon(win, part)` " +
          "whether there is one")
    error.clear()
    on error stop

    ' ---- and the ordinary properties are untouched -----------------------
    ' The control that says the icon branch did not capture every property: a
    ' button's own text still reads back as text.
    check("a button's text still reads normally",
          qt.read(app, "standard", "text"), "Standard")
    check("and a check box's checked state as a boolean",
          type(qt.read(app, "tick", "checked")), "boolean")

    qt.close(app)
end program
