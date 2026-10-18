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
