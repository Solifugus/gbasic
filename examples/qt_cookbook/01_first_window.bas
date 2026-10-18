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
