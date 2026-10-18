' THE `source` WIDGET -- a code editor, which is the one piece Studio needs
' that this vocabulary had no word for (docs/qt_gui_design.md §0.14). Under
' QT_QPA_PLATFORM=offscreen: real Qt, real widgets, no display.
'
' SELF-CHECKING NOT GOLDEN AND FORCED HARDER THAN ANYWHERE ELSE IN THIS
' BACKEND, because the defect is INVISIBLE RATHER THAN PLAUSIBLE: an absent
' syntax definition still applies the theme's normal-text format to every
' line, so an editor highlighting NOTHING looks exactly like an editor
' highlighting correctly -- measured 2026-10-18 at 3 format ranges against 8.
' "Highlighting ran" is therefore a vacuous assertion, and a golden would
' record an unhighlighted editor as expected and defend it.
'
' A `source` IS A `memo` WITH FOUR DIFFERENCES, and that is how it is asserted
' -- each one against a `memo` in the same window rather than as a value,
' since every value check here also passes on a backend that built a plain
' memo and called it a source.
'
' WHAT THIS FIXTURE CANNOT SEE, stated rather than left to be assumed: gBASIC
' cannot read a QTextLayout's format ranges, so "the text is really coloured
' differently" is not assertable from here. It lives in tests/qt/source_probe.cpp,
' which the suite builds and runs as its own tier -- and which carries the
' control that an ABSENT definition gives FEWER ranges, the only check that
' separates highlighting from a highlighter merely being attached.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

function ok(what, condition)
    if condition then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": the condition did not hold"
    end if
end function

window ed( title: "Editor", width: 640, height: 420 )
    column( margin: 8 )
        ' THE SUBJECT, beside the `memo` it must differ from.
        source code( language: "gBASIC", theme: "dark" )
        memo   log( label: "" )
        ' A `source` WITH NO LANGUAGE IS A PLAIN CODE EDITOR and a legitimate
        ' thing to want -- an author editing something the engine has never
        ' heard of. It must therefore be silent rather than warn.
        source plain( placeholderText: "no language here" )
        ' ANOTHER LANGUAGE ENTIRELY, which is what says the name is honoured:
        ' `*.bas` is claimed by FreeBASIC at the same priority as gBASIC, so a
        ' backend that looked a definition up by EXTENSION would answer
        ' FreeBASIC for our own files with no error at all. Asking for
        ' FreeBASIC and GETTING it, beside asking for gBASIC and getting that,
        ' is the pair that pins by-name lookup.
        source other( language: "FreeBASIC" )
    end column
end window

' BOUND IN A FORM, which a `source` must be: Studio's editor holds the text of
' whichever file is open, and that is a record field like any other.
window editor( title: "Bound", width: 520, height: 320 )
    form to edit doc
        field  title( label: "Name" )
        source body( language: "gBASIC" )
    end form
end window

window viewer( title: "Read only", width: 520, height: 320 )
    form to show doc
        source body( language: "gBASIC" )
    end form
end window

program main()
    if not qt.can_display() then
        print "SKIP no display"
        return
    end if

    print "-- the build's own answer, which is a question a program may ask"
    ' ANSWERS RATHER THAN REFUSING, like qt.can_display: a build without
    ' KSyntaxHighlighting still edits text, so a program needs to choose
    ' rather than be stopped.
    ok("qt.can_highlight answers a boolean",
       qt.can_highlight() = true or qt.can_highlight() = false)

    qt.open(ed)

    print "-- a source is a memo with four differences"
    ' (1) IT DOES NOT WRAP. The opposite default from `text`, and for the
    ' opposite reason: a wrapped line of code makes a line number stop
    ' matching the file, where an unwrapped paragraph truncates.
    check("the editor does not wrap", qt.read(ed, "code", "lineWrapMode"),
          "NoWrap")
    check("and a memo still does", qt.read(ed, "log", "lineWrapMode"),
          "WidgetWidth")
    ' (2) A NARROWER TAB. Asserted as a DIFFERENCE, never a value: the width
    ' is four spaces of the system's monospaced font, so the number is a fact
    ' about this machine's fonts and would make the check a transcript.
    ok("a tab is narrower than Qt's default",
       qt.read(ed, "code", "tabStopDistance") <
       qt.read(ed, "log", "tabStopDistance"))
    ' (3) A MONOSPACED FONT, asked of the system rather than named -- a
    ' hardcoded family is how a port acquires a fallback nobody chose.
    ok("the editor's font is not the memo's",
       qt.read(ed, "code", "font") != qt.read(ed, "log", "font"))
    ' (4) AND THE HIGHLIGHTER, which is the one thing here the by-name
    ' property route cannot reach, being an object rather than a property.
    ok("a memo has no highlighting at all",
       is_unknown(qt.highlighting(ed, "log")))

    print "-- what is actually highlighting it: the oracle"
    ' THE NAME IS THE ORACLE and the FILE is the half the name cannot be. A
    ' definition the user installed into their own home SHADOWS the one this
    ' repo ships and reports the IDENTICAL name -- measured, 7 format ranges
    ' against 8 -- so a check on the name alone cannot tell which file was
    ' read. Both are reported; the name is pinned and the file is asserted to
    ' be a real path, since which of the two wins is the machine's business.
    applied = qt.highlighting(ed, "code")
    check("the definition applied is the one asked for", applied.language,
          "gBASIC")
    ok("and it names the file it came from", len(applied.file) > 0)
    ok("which is an XML definition", ends_with(applied.file, ".xml"))

    print "-- the name is what is looked up, not the extension"
    ' WITHOUT THIS PAIR, every check above passes on a backend that resolves
    ' `*.bas` by extension -- which answers FreeBASIC for gBASIC source, with
    ' no error, and highlights plausibly and wrongly.
    check("asking for FreeBASIC gives FreeBASIC",
          qt.highlighting(ed, "other").language, "FreeBASIC")
    ok("so the two editors disagree about their language",
       qt.highlighting(ed, "code").language !=
       qt.highlighting(ed, "other").language)

    print "-- a source with no language is a plain editor, and is silent"
    check("nothing is applied", is_unknown(qt.highlighting(ed, "plain")), true)
    ' AND IT IS STILL AN EDITOR, which is the half that stops "no language"
    ' from being a broken widget.
    qt.write(ed, "plain", "anything at all")
    check("and it still holds text", qt.read(ed, "plain"), "anything at all")

    print "-- it is an editor: text goes in and comes back"
    ' BRACKETED, because a line break is only a continuation inside brackets.
    code = ("' a comment" + chr(10) + "print " + chr(34) + "hi" + chr(34) +
            chr(10) + "x = 42")
    qt.write(ed, "code", code)
    check("every byte survives, newlines included",
          qt.read(ed, "code"), code)
    check("including the line count", len(split(qt.read(ed, "code"), chr(10))),
          3)
    qt.stop()

    print "-- a source binds in a form, like any other value-bearing part"
    doc = { title: "hello.bas", body: "print " + chr(34) + "x" + chr(34) }
    qt.open(editor)
    check("the form populated it from the record",
          qt.read(editor, "body"), doc.body)
    check("and it is editable", qt.read(editor, "body", "readOnly"), false)
    ' THE FOLD, which is what makes it a form part rather than a widget that
    ' happens to sit in one.
    qt.write(editor, "body", "edited")
    back = qt.subject(editor, "doc")
    check("and the record follows the widget", back.body, "edited")
    check("with the other field untouched", back.title, "hello.bas")
    qt.stop()

    print "-- `to show` makes it read-only, as it does a memo"
    qt.open(viewer)
    check("a shown source cannot be typed into",
          qt.read(viewer, "body", "readOnly"), true)
    shown = qt.subject(viewer, "doc")
    qt.write(viewer, "body", "tampered")
    after = qt.subject(viewer, "doc")
    check("and its record does not follow", after.body, shown.body)
    qt.stop()

    print "-- the builder: a source a program generates"
    ' §4's other door. A built record gets the declaration's own load-time
    ' rules through `qt.check`, so a generated editor cannot be built wrong
    ' more quietly than a written one.
    built = { kind: "window", name: "gen", options: { title: "Gen" },
              items: [ { kind: "container", word: "column", options: {},
                         items: [ { kind: "part", word: "source",
                                    name: "pane",
                                    options: { language: "gBASIC" } } ] } ] }
    check("a built source passes qt.check", qt.check(built), true)
    qt.open(built)
    check("and is highlighted the same way",
          qt.highlighting(built, "pane").language, "gBASIC")
    check("and does not wrap either",
          qt.read(built, "pane", "lineWrapMode"), "NoWrap")
    qt.stop()

    print "-- refusals, each beside its nearest legal neighbour"
    ' THE CONTROLS ARE THE POINT: `language` and `theme` are not Qt properties,
    ' so without a rule `setProperty` would store a dynamic property that
    ' NOTHING READS -- an option that silently does nothing, which is this
    ' backend's whole failure class.
    refuse("a language on a button",
           "only a `source` carries `language`")
    refuse("a theme with no language", "needs a `language:` too")
    refuse("a theme that is not one", "is not a theme")
    refuse("a language that is not text", "needs the language's name as text")
    ' AND THE NEIGHBOUR, WHICH IS THE LOAD-BEARING HALF: the same two options
    ' on a real `source` are ACCEPTED. Without it every refusal above is
    ' satisfied by a backend that rejects `language:` and `theme:` outright,
    ' and the feature would be a set of error messages.
    legal = { kind: "window", name: "legal", options: { title: "Legal" },
              items: [ { kind: "container", word: "column", options: {},
                         items: [ { kind: "part", word: "source",
                                    name: "s",
                                    options: { language: "gBASIC",
                                               theme: "light" } } ] } ] }
    qt.open(legal)
    check("while a real source takes both language and theme",
          qt.highlighting(legal, "s").language, "gBASIC")
    qt.stop()
end program

' A REFUSAL, CAUGHT. Each case is a window built as a RECORD rather than
' declared, because a declaration cannot be written four ways in one file --
' and the rules are the same either way by construction, one checker serving
' both adapters (§0.9).
function refuse(what, needle)
    on error goto caught
    bad = refusal_window(what)
    qt.open(bad)
    print "MISMATCH " + what + " was ACCEPTED"
    return nothing
caught:
    if contains(error.message, needle) then
        print "ok   refused: " + what
    else
        print "MISMATCH " + what + ": the message was " + error.message
    end if
    error.clear()
    return nothing
end function

function refusal_window(what)
    opts = {}
    word = "source"
    if what = "a language on a button" then
        word = "button"
        opts = { label: "Go", language: "gBASIC" }
    end if
    if what = "a theme with no language" then
        opts = { theme: "dark" }
    end if
    if what = "a theme that is not one" then
        opts = { language: "gBASIC", theme: "Dark" }
    end if
    if what = "a language that is not text" then
        opts = { language: 42 }
    end if
    return { kind: "window", name: "bad_" + string(len(what)),
             options: { title: "Bad" },
             items: [ { kind: "container", word: "column", options: {},
                        items: [ { kind: "part", word: word, name: "s",
                                   options: opts } ] } ] }
end function
