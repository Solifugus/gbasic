' BAG'S AREA EDITOR, IN A WINDOW -- the half of BAG that describes a world,
' where `gui.bas` is the half that plays one.
'
' Run it:
'     ./gbasic examples/bag/editor.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/bag/editor.bas check
'
' WHY THIS EXAMPLE EXISTS. The text BAG is a numbered menu -- "1) add area,
' 2) edit area, 3) set exit" -- and every field is a question with a typed
' answer, including "which area does north lead to?", answered with an id the
' author has to remember. A window answers that with a DROPDOWN, which is the
' whole reason `choice` was added: it is the one widget that had no substitute,
' and this program is the measurement that said so.
'
' THREE THINGS IT NEEDED THAT DID NOT EXIST, all found by writing it:
'
'   `memo`         a description is a paragraph, and nothing could show one
'   `choice`       an exit NAMES ANOTHER AREA, six times per area
'   `qt.refresh`   choosing a row must SHOW THAT AREA, and a form populated
'                  once at open -- so did a table, so neither the list nor the
'                  editor followed the data
'
' None of them is a nicety. Without the first a description is a one-line
' entry; without the second you type ids; without the third the editor shows
' the first area for ever. See §0.10, §0.13 and §0.11 of
' docs/qt_gui_design.md.
'
' AND THE SAME STATE RULE AS `gui.bas`: gBASIC has no closures and a handler
' sees the frame that called `qt.run`, so a handler CANNOT REBIND A SCALAR
' there -- warning 2104 names the remedy, "mutate a field of a shared record
' instead". So everything lives in one record `g` and every handler writes
' `g.something`.

window editor( title: "BAG: areas", width: 680, height: 520 )

    menu "Project"
        item act_new(   label: "New area", shortcut: "Ctrl+N" )
        separator()
        item act_quit(  label: "Quit", shortcut: "Ctrl+Q" )
    end menu

    column( margin: 10, spacing: 8 )

        text heading( label: "Describe a world",
                      styleSheet: "font-size: 14pt; font-weight: bold" )

        row
            group "Areas"
                table listing to show area_rows
                    field id(   label: "#" )
                    field name( label: "Name" )
                end table
            end group

            group "This area"
                column( spacing: 6 )
                    form to edit current
                        field name( label: "Name" )
                        ' A PARAGRAPH, which is what `memo` is for.
                        memo description( label: "Description" )
                    end form

                    ' SIX EXITS, EACH NAMING ANOTHER AREA -- the shape that
                    ' needed `choice`. Their lists are DATA (the areas that
                    ' exist), so they are filled with `qt.options` and not
                    ' declared.
                    group "Exits lead to"
                        cells
                            row
                                choice north( label: "North" )
                                choice south( label: "South" )
                            end row
                            row
                                choice east( label: "East" )
                                choice west( label: "West" )
                            end row
                            row
                                choice up(   label: "Up" )
                                choice down( label: "Down" )
                            end row
                        end cells
                    end group
                end column
            end group
        end row

        row
            button save( text: "Keep changes" )
            button fresh( text: "New area" )
            stretch()
            text saying( label: "" )
            stretch()
            button close( text: "Close" )
        end row
    end column

    ' CHOOSING A ROW SHOWS THAT AREA, which is `qt.refresh` on a FORM: the
    ' subject changes and the form is re-shown from it. Without the verb this
    ' handler would have to `qt.write` every part by hand, which is the whole
    ' of what binding exists to avoid.
    on listing.itemSelectionChanged
        at = qt.selected(editor, "listing")
        if not is_unknown(at) then
            g.chosen = at
            g.current = g.areas[at]
            load_current()
        end if
    end on

    on save.clicked
        keep()
    end on

    on fresh.clicked
        add_area()
    end on

    on act_new.triggered
        add_area()
    end on

    on act_quit.triggered
        qt.stop()
    end on

    on close.clicked
        qt.stop()
    end on
end window

' ------------------------------------------------------------------ the world

function starting_areas()
    return [ { id: 1, name: "The Cellar",
               description: "A cold stone cellar.",
               north: 2, south: 0, east: 0, west: 0, up: 0, down: 0 },
             { id: 2, name: "The Garden",
               description: "A walled garden gone to seed.",
               north: 0, south: 1, east: 0, west: 0, up: 0, down: 0 },
             { id: 3, name: "The Tunnel",
               description: "A low tunnel beyond the gate.",
               north: 0, south: 2, east: 0, west: 0, up: 0, down: 0 } ]
end function

' THE DROPDOWN'S ITEMS ARE NAMES AND THE RECORD HOLDS IDS, so the two have to
' be translated. `(nowhere)` is index 0 and means id 0, which is how BAG spells
' "no exit" -- so the list is the areas with that sentinel in front.
function exit_names()
    out = ["(nowhere)"]
    for each a in g.areas
        out = append(out, a.name)
    end for
    return out
end function

function name_of_id(id)
    for each a in g.areas
        if a.id = id then
            return a.name
        end if
    end for
    return "(nowhere)"
end function

function id_of_name(what)
    for each a in g.areas
        if a.name = what then
            return a.id
        end if
    end for
    return 0
end function

' WHAT THE TABLE SHOWS, which is a projection of the areas rather than the
' areas themselves: a table's `field`s are columns read from every element, and
' the editor's own record has six exits nobody wants in a list.
function rows_of_areas()
    out = []
    for each a in g.areas
        out = append(out, { id: string(a.id), name: a.name })
    end for
    return out
end function

' ----------------------------------------------------------------- the screen

function load_current()
    ' THE FORM, re-shown from its subject. `current` is the declared subject,
    ' and this function can only READ `g` -- a function cannot change its
    ' caller -- so the handler assigned `g.current` and this hands it over.
    current = g.current
    drew = qt.refresh(editor, "current")

    ' AND THE SIX DROPDOWNS, whose lists are the areas that exist. Filled
    ' before being set, because `qt.write` REFUSES a choice that is not in the
    ' list -- which is what stops a stale exit being shown as if it were real.
    names = exit_names()
    for each d in ["north", "south", "east", "west", "up", "down"]
        filled = qt.options(editor, d, names)
        qt.write(editor, d, name_of_id(g.current[d]))
    end for
    return nothing
end function

function refresh_listing()
    area_rows = rows_of_areas()
    shown = qt.refresh(editor, "listing")
    return nothing
end function

function say(what)
    g.saying = what
    qt.write(editor, "saying", what)
    return nothing
end function

' ------------------------------------------------------------------ the verbs

function keep()
    ' THE FORM BACK INTO THE RECORD. `qt.subject` reads the widgets AT THE
    ' MOMENT ASKED, so there is nothing to have kept in step.
    typed = qt.subject(editor, "current")
    kept = g.current
    kept.name = typed.name
    kept.description = typed.description
    ' AND THE SIX EXITS, each read as a NAME and stored as an ID.
    kept.north = id_of_name(qt.read(editor, "north"))
    kept.south = id_of_name(qt.read(editor, "south"))
    kept.east  = id_of_name(qt.read(editor, "east"))
    kept.west  = id_of_name(qt.read(editor, "west"))
    kept.up    = id_of_name(qt.read(editor, "up"))
    kept.down  = id_of_name(qt.read(editor, "down"))

    g.current = kept
    g.areas = replace_area(g.chosen, kept)
    refresh_listing()
    say("Kept " + kept.name + ".")
    return nothing
end function

function replace_area(at, area)
    out = []
    i = 0
    for each a in g.areas
        if i = at then
            out = append(out, area)
        else
            out = append(out, a)
        end if
        i = i + 1
    end for
    return out
end function

function add_area()
    fresh_id = 1
    for each a in g.areas
        if a.id >= fresh_id then
            fresh_id = a.id + 1
        end if
    end for
    made = { id: fresh_id, name: "Area " + string(fresh_id),
             description: "", north: 0, south: 0, east: 0, west: 0,
             up: 0, down: 0 }
    g.areas = append(g.areas, made)
    g.chosen = len(g.areas) - 1
    g.current = made
    refresh_listing()
    load_current()
    say("Added " + made.name + ".")
    return nothing
end function

' -------------------------------------------------------------------- the run

program main(args)
    g = { areas: starting_areas(), chosen: 0, current: {}, saying: "" }
    g.current = g.areas[0]

    ' THE TWO SUBJECTS the declaration names, read when the window opens.
    area_rows = rows_of_areas()
    current = g.current

    if not qt.can_display() then
        print "No display is available, so the editor cannot be shown."
        print "Set QT_QPA_PLATFORM=offscreen to render without one, or use"
        print "the text version: ./gbasic examples/bag/bag.bas"
        return
    end if

    qt.theme("QGroupBox { font-weight: bold; }")
    qt.open(editor)
    load_current()

    if contains(args, "check") then
        print "areas:          " + string(qt.read(editor, "listing", "rowCount"))
        print "showing:        " + qt.read(editor, "name")
        print "its north:      " + qt.read(editor, "north")
        print "exit choices:   " + string(qt.read(editor, "north", "count"))
        ' CHOOSING A ROW SHOWS THAT AREA, which is the form refresh.
        qt.select(editor, "listing", 2)
        print "after choosing: " + qt.read(editor, "name")
        print "its south:      " + qt.read(editor, "south")
        ' EDITING AND KEEPING, which is the fold plus the id translation.
        qt.write(editor, "name", "The Long Tunnel")
        qt.write(editor, "description", "Longer than it looks." + chr(10) + "Much longer.")
        qt.write(editor, "north", "The Cellar")
        keep()
        print "kept name:      " + g.areas[2].name
        print "kept north id:  " + string(g.areas[2].north)
        print "kept lines:     " + string(len(split(g.areas[2].description, chr(10))))
        print "listing says:   " + qt.cell(editor, "listing", 2, "name")
        ' A NEW AREA, which grows the list AND every dropdown.
        add_area()
        print "after adding:   " + string(qt.read(editor, "listing", "rowCount"))
        print "choices now:    " + string(qt.read(editor, "north", "count"))
        print "editing:        " + qt.read(editor, "name")
        print "saying:         " + qt.read(editor, "saying")
        qt.stop()
        return
    end if
    qt.run()
end program
