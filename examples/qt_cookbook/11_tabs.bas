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
