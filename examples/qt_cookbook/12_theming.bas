' A THEME FOR THE WHOLE APPLICATION, and one part that stands out.
'
' A THEME IS A RECORD OF PLAIN VALUES -- background, ink, the colour of a text
' surface, of a control, of a selection -- which every backend draws its own
' way. It names no toolkit, so the same program themes the same on any.
window panel( title: "Theming", width: 420, height: 220 )
    column( margin: 10, spacing: 8 )
        text heading( label: "Themed by the application" )
        field normal(  label: "Ordinary" )
        ' ONE PART MARKED, in the theme's accent: "outline" or "strong".
        field special( label: "Look here", emphasis: "outline" )
        button close( text: "Close" )
    end column
end window

program main(args)
    dark = { bg: "#2e3440", base: "#242933", ctl: "#3b4252", hi: "#434c5e",
             fg: "#eceff4", faint: "#7b8394", line: "#4c566a",
             sel: "#5e81ac", on_sel: "#ffffff", accent: "#d08770" }

    ' IT NEEDS NO DISPLAY. A theme set before any window exists is REMEMBERED
    ' and applied when the first one opens -- so a program themes first and
    ' opens second, and this half of the recipe runs on a text-only machine.
    ui.theme(dark)
    print "a theme can be set before anything opens: " + string(ui.theme().bg = "#2e3440")

    if not ui.can_display() then
        print "no display"
        return
    end if
    ui.open(panel)

    ' WHAT A PART IS DRAWN IN, read back from the window rather than from the
    ' record we passed -- so this says the theme REACHED the part.
    print ("the field is drawn on the theme's base:   " +
           string(panel.read("normal", "colors").base = "#242933"))

    ' AND THE MARK IS ON ONE PART ONLY. A theme applied by writing onto every
    ' part would satisfy the line above and flatten this.
    print ("the ordinary field is not marked:         " +
           string(panel.read("normal", "emphasis") = "none"))
    print ("the special one is:                       " +
           string(panel.read("special", "emphasis") = "outline"))

    ' CHANGING IT AT RUN TIME is the same call: a dark and a light theme are
    ' two records, and there is no registry to keep in step.
    light = dark
    light.bg = "#eceff4"
    light.base = "#ffffff"
    light.fg = "#2e3440"
    ui.theme(light)
    print ("and a second theme replaces the first:    " +
           string(panel.read("normal", "colors").base = "#ffffff"))
    ui.stop()
end program
