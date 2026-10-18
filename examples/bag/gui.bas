' BAG IN A WINDOW -- the adventure BAG generates, played with a mouse.
'
' Run it:
'     ./gbasic examples/bag/gui.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/bag/gui.bas check
'
' WHY THIS EXAMPLE EXISTS. BAG was the first real program written in the
' earliest gBASIC: you describe a world and it writes a playable adventure.
' The game it writes is a READ-EVAL-PRINT LOOP -- `print` the room, `input(">")`
' a command, repeat -- and that is the shape a GUI cannot have, because QT OWNS
' THE LOOP. So this is the same game with its control flow turned inside out,
' and it is a proof rather than a demo: if a whole small application can be
' built on this surface, the surface works.
'
' THE ONE THING TO TAKE FROM IT: WHERE THE STATE LIVES. gBASIC has no closures,
' and a handler fires from inside Qt's loop beneath `qt.run()`, so the frame it
' sees is the one that called `qt.run` -- `main`. MEASURED, and the two halves
' differ:
'
'   a handler CAN READ main's locals
'   a handler CANNOT REBIND A SCALAR -- `count = count + 1` silently makes a
'       function-local, and warning 2104 says so AND NAMES THE REMEDY:
'       "mutate a field of a shared record instead"
'
' So the whole game state is ONE RECORD and every handler mutates its fields.
' That is not a trick; it is the diagnostic's own advice, and it is the pattern
' any stateful gBASIC GUI wants. A helper function called from a handler can
' READ that record and write into widgets, and must `return nothing` or
' warning 2101 fires for the discarded result.
'
' WHAT IT EXERCISES, which is most of the Qt surface in one screen: `text` for
' the room name and its description, `memo` for the transcript, a `table` for
' what is here, `field` plus `button` for typed commands, six buttons whose
' `enabled` is set per room with `qt.set` -- which is what the by-name property
' route is FOR -- a `menu` bar with a separator and a submenu, `group`/`row`/
' `column` layout, and `qt.theme` for the look.

window game( title: "The Lantern Room", width: 620, height: 520 )

    menu "Game"
        item restart(   label: "Start again", shortcut: "Ctrl+N" )
        separator()
        item quit_item( label: "Quit", shortcut: "Ctrl+Q" )
    end menu
    menu "Help"
        item about( label: "About" )
    end menu

    column( margin: 10, spacing: 8 )

        text where( label: "...", styleSheet: "font-size: 15pt; font-weight: bold" )
        text blurb( label: "..." )

        group "What has happened"
            memo log( readOnly: true )
        end group

        group "What is here"
            table here to show visible
                field name(        label: "Thing" )
                field description( label: "As you see it" )
            end table
        end group

        group "Go"
            row
                button north( text: "North" )
                button south( text: "South" )
                button east(  text: "East" )
                button west(  text: "West" )
                button up(    text: "Up" )
                button down(  text: "Down" )
            end row
        end group

        row
            field command( label: "Or type", placeholderText: "take key, examine key, look, inventory" )
            button send( text: "Do it" )
        end row

        text carried( label: "You are carrying nothing." )
    end column

    ' SIX HANDLERS, ONE PER DIRECTION. Each mutates `g.location` through the
    ' shared record and then redraws; nothing else is shared.
    on north.clicked
        travel("north")
    end on
    on south.clicked
        travel("south")
    end on
    on east.clicked
        travel("east")
    end on
    on west.clicked
        travel("west")
    end on
    on up.clicked
        travel("up")
    end on
    on down.clicked
        travel("down")
    end on

    on send.clicked
        obey(qt.read(game, "command"))
        qt.write(game, "command", "")
    end on

    ' A FIELD'S Enter KEY is an ordinary Qt signal, so typing and pressing
    ' return works without a second code path.
    on command.returnPressed
        obey(qt.read(game, "command"))
        qt.write(game, "command", "")
    end on

    on restart.triggered
        g.location = 1
        g.inventory = []
        g.visible = things_here_at(1)
        g.opened = false
        g.lines = ["You are back at the beginning."]
        redraw()
    end on

    on quit_item.triggered
        qt.stop()
    end on

    on about.triggered
        say("BAG: the Basic Adventure Generator, in a window.")
        redraw()
    end on
end window

' ---------------------------------------------------------------- the world

function build_areas()
    return [ { id: 1, name: "The Cellar",
               description: "A cold stone cellar. Water drips somewhere behind you, and the only light falls through a grate overhead.",
               north: 2, south: 0, east: 0, west: 0, up: 0, down: 0 },
             { id: 2, name: "The Garden",
               description: "A walled garden gone to seed. A locked gate stands in the north wall.",
               north: 0, south: 1, east: 0, west: 0, up: 0, down: 0 },
             { id: 3, name: "The Tunnel",
               description: "A low tunnel beyond the gate. It smells of cold earth.",
               north: 0, south: 2, east: 0, west: 0, up: 0, down: 0 } ]
end function

function build_items()
    return [ { id: 1, name: "brass key", description: "A brass key lies here.",
               location: 1, portable: true,
               examine: "The key is old but sturdy.",
               opens: 2 } ]
end function

' ------------------------------------------------------------- the machinery
'
' EVERY ONE OF THESE READS `g` and NONE OF THEM ASSIGNS TO IT. A function
' cannot change its caller, so anything that changes the world is done in the
' HANDLER, on `g.field` directly; these only answer questions and draw.

function area_at(id)
    for each a in g.areas
        if a.id = id then
            return a
        end if
    end for
    return nothing
end function

function exit_of(area, direction)
    if direction = "north" then return area.north
    if direction = "south" then return area.south
    if direction = "east"  then return area.east
    if direction = "west"  then return area.west
    if direction = "up"    then return area.up
    if direction = "down"  then return area.down
    return 0
end function

function item_named(what)
    for each it in g.items
        if it.name = what then
            return it
        end if
    end for
    return nothing
end function

' WHAT IS IN THIS ROOM, which is the `table`'s subject: a LIST, so each
' `field` is a column read from every element.
function things_here()
    out = []
    for each it in g.items
        if it.location = g.location then
            out = append(out, it)
        end if
    end for
    return out
end function

' WHAT IS IN A GIVEN ROOM, which `things_here` answers for the CURRENT one.
' Separated because a handler sets `g.visible` for the room it is moving TO,
' before `g.location` has been read back anywhere.
function things_here_at(where)
    out = []
    for each it in g.items
        if it.location = where then
            out = append(out, it)
        end if
    end for
    return out
end function

function things_here_without(id)
    out = []
    for each it in g.visible
        if it.id != id then
            out = append(out, it)
        end if
    end for
    return out
end function

function dropped_here(id)
    for each it in g.items
        if it.id = id then
            return it
        end if
    end for
    return nothing
end function

function carrying()
    names = []
    for each id in g.inventory
        for each it in g.items
            if it.id = id then
                names = append(names, it.name)
            end if
        end for
    end for
    return names
end function

' ----------------------------------------------------------------- drawing
'
' ONE PLACE REDRAWS THE WINDOW, which is what replaces the text game's
' `describe()` call after every command. A GUI has no "print the room again"
' -- the widgets are already there and are simply written over.

function say(line)
    ' THE TRANSCRIPT. `g.lines` is a field of the shared record, so appending
    ' here would be lost -- `append` returns a new array and a function cannot
    ' change its caller. So this APPENDS THROUGH THE RECORD, which does reach,
    ' and is the same one-hop field write every handler uses.
    g.lines = append(g.lines, line)
    return nothing
end function

function redraw()
    place = area_at(g.location)
    qt.write(game, "where", place.name)
    qt.write(game, "blurb", place.description)
    qt.write(game, "log", join(g.lines, chr(10)))

    ' THE BUTTONS SAY WHERE YOU CAN GO, which is `qt.set` doing the job the
    ' declaration cannot: which exits exist is a fact about the room you are
    ' standing in, and the declaration was written before the game began.
    for each d in ["north", "south", "east", "west", "up", "down"]
        qt.set(game, d, "enabled", exit_of(place, d) != 0)
    end for

    held = carrying()
    if len(held) = 0 then
        qt.write(game, "carried", "You are carrying nothing.")
    else
        qt.write(game, "carried", "You are carrying: " + join(held, ", "))
    end if

    ' WHAT IS HERE FOLLOWS THE ROOM. `visible` is the table's declared subject
    ' and this function can only READ it -- a function cannot change its
    ' caller -- so the handler that moved us assigned `g.visible`, and this
    ' hands that to the table. The list is read AT THE MOMENT ASKED, which is
    ' `qt.subject`'s principle the other way round.
    visible = g.visible
    shown = qt.refresh(game, "here")
    return nothing
end function

' ----------------------------------------------------------------- the verbs
'
' THESE ARE CALLED FROM HANDLERS AND MUTATE `g` THEMSELVES, which is the one
' place that rule bends: a function cannot change its CALLER's scalars, but
' `g` is a record reached through the parent chain, so a field write lands on
' main's copy. That is exactly what warning 2104 tells you to do.

function travel(direction)
    place = area_at(g.location)
    onward = exit_of(place, direction)
    if onward = 0 then
        say("You cannot go " + direction + ".")
    else
        g.location = onward
        g.visible = things_here_at(onward)
        say("You go " + direction + ".")
        say(area_at(onward).name + ".")
    end if
    redraw()
    return nothing
end function

function obey(line)
    said = lower(trim(line))
    if said = "" then
        return nothing
    end if
    words = split(said, " ")
    verb = words[0]
    rest = ""
    if len(words) > 1 then
        rest = trim(mid(said, len(verb) + 1, len(said) - len(verb)))
    end if

    if contains(["north", "south", "east", "west", "up", "down"], verb) then
        travel(verb)
        return nothing
    end if
    if verb = "n" then
        travel("north")
        return nothing
    end if
    if verb = "s" then
        travel("south")
        return nothing
    end if

    if verb = "look" then
        say(area_at(g.location).description)
        redraw()
        return nothing
    end if

    if verb = "inventory" or verb = "i" then
        held = carrying()
        if len(held) = 0 then
            say("You are empty-handed.")
        else
            say("You have: " + join(held, ", "))
        end if
        redraw()
        return nothing
    end if

    if verb = "take" or verb = "get" then
        it = item_named(rest)
        if it = nothing then
            say("There is no " + rest + " here.")
        else
            if it.location != g.location then
                say("There is no " + rest + " here.")
            else
                if not it.portable then
                    say("You cannot take the " + rest + ".")
                else
                    ' TWO FIELDS OF THE SHARED RECORD, both one hop.
                    g.inventory = append(g.inventory, it.id)
                    g.items = relocate(it.id, 0)
                    g.visible = things_here_without(it.id)
                    say("You take the " + rest + ".")
                end if
            end if
        end if
        redraw()
        return nothing
    end if

    if verb = "drop" then
        it = item_named(rest)
        if it = nothing or not contains(g.inventory, it.id) then
            say("You are not carrying a " + rest + ".")
        else
            g.inventory = g.inventory excluding [it.id]
            g.items = relocate(it.id, g.location)
            g.visible = append(g.visible, dropped_here(it.id))
            say("You drop the " + rest + ".")
        end if
        redraw()
        return nothing
    end if

    if verb = "examine" or verb = "x" then
        it = item_named(rest)
        if it = nothing then
            say("You see nothing special.")
        else
            say(it.examine)
        end if
        redraw()
        return nothing
    end if

    ' THE TRIGGER, which is BAG's one bit of real game logic: an item used in
    ' the right place changes the world. Here the key opens the gate, which
    ' adds an exit that was not there before -- so the North button becomes
    ' enabled on the next redraw without anything saying so.
    if verb = "open" or verb = "use" then
        it = item_named(rest)
        if it = nothing or not contains(g.inventory, it.id) then
            say("You have no " + rest + ".")
        else
            if it.opens = g.location and not g.opened then
                g.opened = true
                g.areas = open_gate()
                say("The key turns. The gate opens.")
            else
                say("Nothing happens.")
            end if
        end if
        redraw()
        return nothing
    end if

    say("I do not understand \"" + said + "\".")
    redraw()
    return nothing
end function

' A NEW LIST, NOT A MUTATION. A record is a value, so changing one item means
' rebuilding the list -- and the handler assigns the result to `g.items`,
' which is the one-hop write that reaches.
function relocate(id, where)
    out = []
    for each it in g.items
        if it.id = id then
            moved = it
            moved.location = where
            out = append(out, moved)
        else
            out = append(out, it)
        end if
    end for
    return out
end function

function open_gate()
    out = []
    for each a in g.areas
        if a.id = 2 then
            opened = a
            opened.north = 3
            out = append(out, opened)
        else
            out = append(out, a)
        end if
    end for
    return out
end function

' ------------------------------------------------------------------- the game

program main(args)
    ' ONE RECORD, AND EVERY HANDLER MUTATES ITS FIELDS. See the note at the
    ' top: this is not a style choice, it is what warning 2104 tells you to do
    ' when a handler cannot rebind a scalar in the frame it can see.
    g = { location: 1, inventory: [], opened: false,
          areas: build_areas(), items: build_items(), visible: [],
          lines: ["You are in the cellar.", "A brass key lies here."] }
    g.visible = things_here_at(1)

    ' THE TABLE'S SUBJECT. A `table to show` reads its list when the window
    ' OPENS, so this is what it shows to begin with -- and `qt.refresh` is
    ' what makes it follow the room you are standing in. Writing this example
    ' is what found that gap: without the verb, "what is here" showed the
    ' cellar's contents for the whole game.
    visible = things_here()

    if not qt.can_display() then
        print "No display is available, so the game cannot be shown."
        print "Set QT_QPA_PLATFORM=offscreen to render without one, or play"
        print "the text version: ./gbasic examples/bag/generated_adventure.bas"
        return
    end if

    qt.theme("QGroupBox { font-weight: bold; }" +
             "QPlainTextEdit { background: #fbfbfb; }")

    qt.open(game)
    redraw()

    if contains(args, "check") then
        ' A WHOLE GAME PLAYED WITHOUT A PERSON, which is what makes this
        ' example a proof rather than a demo: every move goes through the same
        ' handlers a mouse would fire.
        print "start:        " + qt.read(game, "where")
        print "north open:   " + string(qt.read(game, "north", "enabled"))
        print "east open:    " + string(qt.read(game, "east", "enabled"))
        obey("take brass key")
        print "after taking: " + qt.read(game, "carried")
        qt.invoke(game, "north", "click")
        print "moved to:     " + qt.read(game, "where")
        print "gate shut:    " + string(qt.read(game, "north", "enabled"))
        obey("open brass key")
        print "gate open:    " + string(qt.read(game, "north", "enabled"))
        qt.invoke(game, "north", "click")
        print "and through:  " + qt.read(game, "where")
        obey("drop brass key")
        print "after drop:   " + qt.read(game, "carried")
        print "menus:        " + string(qt.menus(game))
        print "transcript:   " + string(len(g.lines)) + " lines"
        ' THE TABLE FOLLOWED THE ROOM, which is what `qt.refresh` is for and
        ' what the first draft of this example could not do: the key was
        ' dropped in the Tunnel, so that is the one thing here.
        print "here now:     " + string(qt.read(game, "here", "rowCount")) + " thing(s)"
        print "and it is:    " + qt.cell(game, "here", 0, "name")
        qt.stop()
        return
    end if
    qt.run()
end program
