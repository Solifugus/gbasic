function make_area(id, name, description, north, south, east, west, up, down)
    return {
        id = id,
        name = name,
        description = description,
        north = north,
        south = south,
        east = east,
        west = west,
        up = up,
        down = down
    }
end function

function make_item(id, name, description, location, portable, examine, use_text)
    return {
        id = id,
        name = name,
        description = description,
        location = location,
        portable = portable,
        examine = examine,
        use_text = use_text
    }
end function

function make_trigger(item_id, area_id, action, from_area, direction, to_area, message)
    return {
        item_id = item_id,
        area_id = area_id,
        action = action,
        from_area = from_area,
        direction = direction,
        to_area = to_area,
        message = message
    }
end function

function find_area_index(areas, id)
    i = 0
    while i < len(areas)
        area = areas[i]
        if area.id = id then
            return i
        end if
        i = i + 1
    end while
    return nothing
end function

function direction_exit(area, direction)
    if direction = "north" then
        return area.north
    end if
    if direction = "south" then
        return area.south
    end if
    if direction = "east" then
        return area.east
    end if
    if direction = "west" then
        return area.west
    end if
    if direction = "up" then
        return area.up
    end if
    if direction = "down" then
        return area.down
    end if
    return 0
end function

function update_area_exit(areas, id, direction, destination)
    updated = []
    i = 0
    while i < len(areas)
        area = areas[i]
        if area.id = id then
            north = area.north
            south = area.south
            east = area.east
            west = area.west
            up = area.up
            down = area.down
            if direction = "north" then
                north = destination
            end if
            if direction = "south" then
                south = destination
            end if
            if direction = "east" then
                east = destination
            end if
            if direction = "west" then
                west = destination
            end if
            if direction = "up" then
                up = destination
            end if
            if direction = "down" then
                down = destination
            end if
            append(updated, make_area(area.id, area.name, area.description, north, south, east, west, up, down))
        else
            append(updated, area)
        end if
        i = i + 1
    end while
    return updated
end function

function list_areas(areas)
    if len(areas) = 0 then
        print("No areas yet.")
        return
    end if
    i = 0
    while i < len(areas)
        area = areas[i]
        id_text(string)= area.id
        north_text(string)= area.north
        south_text(string)= area.south
        east_text(string)= area.east
        west_text(string)= area.west
        up_text(string)= area.up
        down_text(string)= area.down
        print("[" + id_text + "] " + area.name)
        print("    " + area.description)
        print("    exits n/s/e/w/u/d: " + north_text + "/" + south_text + "/" + east_text + "/" + west_text + "/" + up_text + "/" + down_text)
        i = i + 1
    end while
    return
end function

function list_items(items)
    if len(items) = 0 then
        print("No items yet.")
        return
    end if
    i = 0
    while i < len(items)
        item = items[i]
        id_text(string)= item.id
        location_text(string)= item.location
        print("[" + id_text + "] " + item.name + " in area " + location_text)
        print("    " + item.description)
        i = i + 1
    end while
    return
end function

function list_triggers(triggers)
    if len(triggers) = 0 then
        print("No triggers yet.")
        return
    end if
    i = 0
    while i < len(triggers)
        trigger = triggers[i]
        num_text(string)= i + 1
        item_text(string)= trigger.item_id
        area_text(string)= trigger.area_id
        from_text(string)= trigger.from_area
        to_text(string)= trigger.to_area
        print("[" + num_text + "] use item " + item_text + " in area " + area_text + " to " + trigger.action + " " + trigger.direction + " from " + from_text + " to " + to_text)
        print("    " + trigger.message)
        i = i + 1
    end while
    return
end function

function add_area(areas)
    id = len(areas) + 1
    id_text(string)= id
    print("Adding area " + id_text)
    name(trimmed)= input("Name: ")
    description(trimmed)= input("Description: ")
    north(number)= input("North area id, 0 for none: ")
    south(number)= input("South area id, 0 for none: ")
    east(number)= input("East area id, 0 for none: ")
    west(number)= input("West area id, 0 for none: ")
    up(number)= input("Up area id, 0 for none: ")
    down(number)= input("Down area id, 0 for none: ")
    append(areas, make_area(id, name, description, north, south, east, west, up, down))
    print("Area added.")
    return areas
end function

function add_item(items)
    id = len(items) + 1
    id_text(string)= id
    print("Adding item " + id_text)
    name(trimmed)= input("Name: ")
    description(trimmed)= input("Room description: ")
    location(number)= input("Starting area id, 0 for nowhere: ")
    portable_text(trimmed)= input("Portable? y/n: ")
    portable_text(lowered)= portable_text
    portable = false
    if portable_text = "y" or portable_text = "yes" then
        portable = true
    end if
    examine(trimmed)= input("Examine text: ")
    use_text(trimmed)= input("Use text: ")
    append(items, make_item(id, name, description, location, portable, examine, use_text))
    print("Item added.")
    return items
end function

function add_trigger(triggers)
    print("Adding trigger")
    item_id(number)= input("Item id used: ")
    area_id(number)= input("Area id where used: ")
    action(trimmed)= input("Action, open or close: ")
    action(lowered)= action
    from_area(number)= input("Exit from area id: ")
    direction(trimmed)= input("Direction opened/closed: ")
    direction(lowered)= direction
    to_area(number)= input("Destination area id, 0 for closed: ")
    message(trimmed)= input("Message: ")
    append(triggers, make_trigger(item_id, area_id, action, from_area, direction, to_area, message))
    print("Trigger added.")
    return triggers
end function

function emit_engine(out)
    write(out, "function make_area(id, name, description, north, south, east, west, up, down)\n")
    append(out, "    return { id = id, name = name, description = description, north = north, south = south, east = east, west = west, up = up, down = down }\n")
    append(out, "end function\n\n")
    append(out, "function make_item(id, name, description, location, portable, examine, use_text)\n")
    append(out, "    return { id = id, name = name, description = description, location = location, portable = portable, examine = examine, use_text = use_text }\n")
    append(out, "end function\n\n")
    append(out, "function make_trigger(item_id, area_id, action, from_area, direction, to_area, message)\n")
    append(out, "    return { item_id = item_id, area_id = area_id, action = action, from_area = from_area, direction = direction, to_area = to_area, message = message }\n")
    append(out, "end function\n\n")
    append(out, "function find_area_index(areas, id)\n    i = 0\n    while i < len(areas)\n        area = areas[i]\n        if area.id = id then\n            return i\n        end if\n        i = i + 1\n    end while\n    return nothing\nend function\n\n")
    append(out, "function find_item_index(items, name)\n    i = 0\n    while i < len(items)\n        item = items[i]\n        if item.name = name then\n            return i\n        end if\n        i = i + 1\n    end while\n    return nothing\nend function\n\n")
    append(out, "function player_has(inventory, item_id)\n    return find(inventory, item_id) != nothing\nend function\n\n")
    append(out, "function exit_for(area, direction)\n    if direction = \"north\" then\n        return area.north\n    end if\n    if direction = \"south\" then\n        return area.south\n    end if\n    if direction = \"east\" then\n        return area.east\n    end if\n    if direction = \"west\" then\n        return area.west\n    end if\n    if direction = \"up\" then\n        return area.up\n    end if\n    if direction = \"down\" then\n        return area.down\n    end if\n    return 0\nend function\n\n")
    append(out, "function set_exit(areas, id, direction, destination)\n    updated = []\n    i = 0\n    while i < len(areas)\n        area = areas[i]\n        if area.id = id then\n            north = area.north\n            south = area.south\n            east = area.east\n            west = area.west\n            up = area.up\n            down = area.down\n            if direction = \"north\" then\n                north = destination\n            end if\n            if direction = \"south\" then\n                south = destination\n            end if\n            if direction = \"east\" then\n                east = destination\n            end if\n            if direction = \"west\" then\n                west = destination\n            end if\n            if direction = \"up\" then\n                up = destination\n            end if\n            if direction = \"down\" then\n                down = destination\n            end if\n            append(updated, make_area(area.id, area.name, area.description, north, south, east, west, up, down))\n        else\n            append(updated, area)\n        end if\n        i = i + 1\n    end while\n    return updated\nend function\n\n")
    append(out, "function set_item_location(items, id, location)\n    updated = []\n    i = 0\n    while i < len(items)\n        item = items[i]\n        if item.id = id then\n            append(updated, make_item(item.id, item.name, item.description, location, item.portable, item.examine, item.use_text))\n        else\n            append(updated, item)\n        end if\n        i = i + 1\n    end while\n    return updated\nend function\n\n")
    append(out, "function describe(area_id, areas, items)\n    area_index = find_area_index(areas, area_id)\n    if area_index = nothing then\n        print(\"You are nowhere.\")\n        return\n    end if\n    area = areas[area_index]\n    print(area.name)\n    print(area.description)\n    i = 0\n    while i < len(items)\n        item = items[i]\n        if item.location = area_id then\n            print(item.description)\n        end if\n        i = i + 1\n    end while\n    return\nend function\n\n")
    append(out, "function show_inventory(inventory, items)\n    if len(inventory) = 0 then\n        print(\"You are carrying nothing.\")\n    else\n        names = []\n        i = 0\n        while i < len(inventory)\n            j = 0\n            while j < len(items)\n                item = items[j]\n                if item.id = inventory[i] then\n                    append(names, item.name)\n                end if\n                j = j + 1\n            end while\n            i = i + 1\n        end while\n        print(\"You are carrying: \" + join(names, \", \"))\n    end if\n    return\nend function\n\n")
    append(out, "function play(title, areas, items, triggers)\n    location = 1\n    inventory = []\n    print(title)\n    print(\"\")\n    describe(location, areas, items)\n    while true\n        command(trimmed)= input(\">\")\n        command(lowered)= command\n        words(split)= command\n        if len(words) = 0 then\n            continue\n        end if\n        verb = words[0]\n        noun = \"\"\n        if len(words) > 1 then\n            noun = words[1]\n        end if\n        if len(words) > 2 then\n            noun = noun + \" \" + words[2]\n        end if\n        direction = \"\"\n        if verb = \"go\" then\n            direction = noun\n        end if\n        if verb = \"north\" or verb = \"south\" or verb = \"east\" or verb = \"west\" or verb = \"up\" or verb = \"down\" then\n            direction = verb\n        end if\n        handled = false\n        if verb = \"help\" then\n            print(\"Commands: look, north, south, east, west, up, down, go DIR, take ITEM, drop ITEM, examine ITEM, use ITEM, inventory, help, quit\")\n            handled = true\n        end if\n        if verb = \"look\" then\n            describe(location, areas, items)\n            handled = true\n        end if\n        if verb = \"inventory\" then\n            show_inventory(inventory, items)\n            handled = true\n        end if\n")
    append(out, "        if verb = \"take\" then\n            idx = find_item_index(items, noun)\n            if idx = nothing then\n                print(\"You cannot take that.\")\n            else\n                item = items[idx]\n                if item.location = location and item.portable then\n                    append(inventory, item.id)\n                    items = set_item_location(items, item.id, -1)\n                    print(\"Taken.\")\n                else\n                    print(\"You cannot take that.\")\n                end if\n            end if\n            handled = true\n        end if\n        if verb = \"drop\" then\n            idx = find_item_index(items, noun)\n            if idx = nothing then\n                print(\"You are not carrying that.\")\n            else\n                item = items[idx]\n                inv_idx = find(inventory, item.id)\n                if inv_idx = nothing then\n                    print(\"You are not carrying that.\")\n                else\n                    remove(inventory, inv_idx)\n                    items = set_item_location(items, item.id, location)\n                    print(\"Dropped.\")\n                end if\n            end if\n            handled = true\n        end if\n")
    append(out, "        if verb = \"examine\" then\n            idx = find_item_index(items, noun)\n            if idx = nothing then\n                print(\"You see nothing special.\")\n            else\n                item = items[idx]\n                if item.location = location or player_has(inventory, item.id) then\n                    if item.examine = \"\" then\n                        print(\"You see nothing special.\")\n                    else\n                        print(item.examine)\n                    end if\n                else\n                    print(\"You do not see that here.\")\n                end if\n            end if\n            handled = true\n        end if\n        if verb = \"use\" then\n            idx = find_item_index(items, noun)\n            if idx = nothing then\n                print(\"You cannot use that.\")\n            else\n                item = items[idx]\n                if player_has(inventory, item.id) = false then\n                    print(\"You are not carrying that.\")\n                else\n                    used = false\n                    i = 0\n                    while i < len(triggers)\n                        trigger = triggers[i]\n                        if trigger.item_id = item.id and trigger.area_id = location then\n                            areas = set_exit(areas, trigger.from_area, trigger.direction, trigger.to_area)\n                            print(trigger.message)\n                            used = true\n                        end if\n                        i = i + 1\n                    end while\n                    if used = false then\n                        if item.use_text = \"\" then\n                            print(\"Nothing happens.\")\n                        else\n                            print(item.use_text)\n                        end if\n                    end if\n                end if\n            end if\n            handled = true\n        end if\n")
    append(out, "        if direction != \"\" then\n            area = areas[find_area_index(areas, location)]\n            next = exit_for(area, direction)\n            if next = 0 then\n                print(\"You cannot go that way.\")\n            else\n                location = next\n                describe(location, areas, items)\n            end if\n            handled = true\n        end if\n        if verb = \"quit\" then\n            print(\"Goodbye.\")\n            break\n        end if\n        if handled = false then\n            print(\"I do not understand that command.\")\n        end if\n    end while\n    return\nend function\n\n")
    return
end function

function emit_data(out, title, areas, items, triggers)
    append(out, "function build_areas()\n    areas = []\n")
    i = 0
    while i < len(areas)
        area = areas[i]
        id_text(string)= area.id
        north_text(string)= area.north
        south_text(string)= area.south
        east_text(string)= area.east
        west_text(string)= area.west
        up_text(string)= area.up
        down_text(string)= area.down
        append(out, "    append(areas, make_area(" + id_text + ", ")
        append(out, quote(area.name))
        append(out, ", ")
        append(out, quote(area.description))
        append(out, ", " + north_text + ", " + south_text + ", " + east_text + ", " + west_text + ", " + up_text + ", " + down_text + "))\n")
        i = i + 1
    end while
    append(out, "    return areas\nend function\n\n")

    append(out, "function build_items()\n    items = []\n")
    i = 0
    while i < len(items)
        item = items[i]
        id_text(string)= item.id
        location_text(string)= item.location
        portable_text = "false"
        if item.portable then
            portable_text = "true"
        end if
        append(out, "    append(items, make_item(" + id_text + ", ")
        append(out, quote(item.name))
        append(out, ", ")
        append(out, quote(item.description))
        append(out, ", " + location_text + ", " + portable_text + ", ")
        append(out, quote(item.examine))
        append(out, ", ")
        append(out, quote(item.use_text))
        append(out, "))\n")
        i = i + 1
    end while
    append(out, "    return items\nend function\n\n")

    append(out, "function build_triggers()\n    triggers = []\n")
    i = 0
    while i < len(triggers)
        trigger = triggers[i]
        item_text(string)= trigger.item_id
        area_text(string)= trigger.area_id
        from_text(string)= trigger.from_area
        to_text(string)= trigger.to_area
        append(out, "    append(triggers, make_trigger(" + item_text + ", " + area_text + ", ")
        append(out, quote(trigger.action))
        append(out, ", " + from_text + ", ")
        append(out, quote(trigger.direction))
        append(out, ", " + to_text + ", ")
        append(out, quote(trigger.message))
        append(out, "))\n")
        i = i + 1
    end while
    append(out, "    return triggers\nend function\n\n")
    append(out, "play(")
    append(out, quote(title))
    append(out, ", build_areas(), build_items(), build_triggers())\n")
    return
end function

function generate_adventure(title, areas, items, triggers)
    out(file)= "examples/bag/generated_adventure.bas"
    emit_engine(out)
    emit_data(out, title, areas, items, triggers)
    print("Generated examples/bag/generated_adventure.bas")
    print("Project: " + title)
    return
end function

function save_project(title, areas, items, triggers)
    filename(trimmed)= input("Save filename (.bag suggested): ")
    if filename = "" then
        print("Save cancelled.")
        return false
    end if

    project = {
        title = title,
        areas = areas,
        items = items,
        triggers = triggers
    }

    out(file)= filename
    on error goto save_failed
    ok = write(out, encode(project))
    on error stop

    if ok then
        print("Saved project to " + filename)
    else
        print("Could not save project.")
    end if
    return ok

save_failed:
    print("Could not save project: " + error.message)
    error.clear()
    on error stop
    return false
end function

function load_project(title, areas, items, triggers)
    filename(trimmed)= input("Load filename (.bag suggested): ")
    result = {
        ok = false,
        title = title,
        areas = areas,
        items = items,
        triggers = triggers
    }

    if filename = "" then
        print("Load cancelled.")
        return result
    end if

    source(file)= filename
    on error goto load_failed
    text = read(source)

    loaded = decode(text)

    result.title = loaded.title
    result.areas = loaded.areas
    result.items = loaded.items
    result.triggers = loaded.triggers
    on error stop

    result.ok = true
    print("Loaded project from " + filename)
    print("Project: " + result.title)
    return result

load_failed:
    print("Could not load project: " + error.message)
    error.clear()
    on error stop
    return result
end function

function print_menu(title)
    print("")
    print("BAG: BASIC Adventure Generator")
    print("Project: " + title)
    print("1. Add area")
    print("2. List areas")
    print("3. Add item")
    print("4. List items")
    print("5. Add trigger")
    print("6. List triggers")
    print("7. Generate adventure")
    print("8. Quit")
    print("9. Save project")
    print("10. Load project")
    return
end function

function seed_demo(areas, items, triggers)
    append(areas, make_area(1, "Cellar", "A cold stone cellar.", 2, 0, 0, 0, 0, 0))
    append(areas, make_area(2, "Garden", "A walled garden with a locked gate.", 0, 1, 0, 0, 0, 0))
    append(areas, make_area(3, "Tunnel", "A low tunnel beyond the gate.", 0, 2, 0, 0, 0, 0))
    append(items, make_item(1, "brass key", "A brass key lies here.", 1, true, "The key is old but sturdy.", ""))
    append(triggers, make_trigger(1, 2, "open", 2, "north", 3, "The key turns. The gate opens."))
    return {
        title = "The Lantern Room",
        areas = areas,
        items = items,
        triggers = triggers
    }
end function

function main()
    title = "Untitled BAG Adventure"
    areas = []
    items = []
    triggers = []

    demo(trimmed)= input("Load tiny demo project? y/n: ")
    demo(lowered)= demo
    if demo = "y" or demo = "yes" then
        seeded = seed_demo(areas, items, triggers)
        title = seeded.title
        areas = seeded.areas
        items = seeded.items
        triggers = seeded.triggers
    end if

    while true
        print_menu(title)
        choice(trimmed)= input("Choice: ")
        quit = false
        consider choice
        if "1" then
            areas = add_area(areas)
        if "2" then
            list_areas(areas)
        if "3" then
            items = add_item(items)
        if "4" then
            list_items(items)
        if "5" then
            triggers = add_trigger(triggers)
        if "6" then
            list_triggers(triggers)
        if "7" then
            generate_adventure(title, areas, items, triggers)
        if "8" then
            print("Goodbye.")
            quit = true
        if "9" then
            on error resume next
            save_project(title, areas, items, triggers)
            if error then
                error.clear()
            end if
            on error stop
        if "10" then
            loaded = {
                ok = false,
                title = title,
                areas = areas,
                items = items,
                triggers = triggers
            }
            on error resume next
            loaded = load_project(title, areas, items, triggers)
            if error then
                error.clear()
            end if
            on error stop
            if loaded.ok then
                title = loaded.title
                areas = loaded.areas
                items = loaded.items
                triggers = loaded.triggers
            end if
        else
            print("Unknown choice.")
        end consider
        if quit then
            break
        end if
    end while
    return
end function

main()
