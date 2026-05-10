function make_area(id, name, description, north, south, east, west, up, down)
    return { id = id, name = name, description = description, north = north, south = south, east = east, west = west, up = up, down = down }
end function

function make_item(id, name, description, location, portable, examine, use_text)
    return { id = id, name = name, description = description, location = location, portable = portable, examine = examine, use_text = use_text }
end function

function make_trigger(item_id, area_id, action, from_area, direction, to_area, message)
    return { item_id = item_id, area_id = area_id, action = action, from_area = from_area, direction = direction, to_area = to_area, message = message }
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

function find_item_index(items, name)
    i = 0
    while i < len(items)
        item = items[i]
        if item.name = name then
            return i
        end if
        i = i + 1
    end while
    return nothing
end function

function player_has(inventory, item_id)
    return find(inventory, item_id) != nothing
end function

function exit_for(area, direction)
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

function set_exit(areas, id, direction, destination)
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

function set_item_location(items, id, location)
    updated = []
    i = 0
    while i < len(items)
        item = items[i]
        if item.id = id then
            append(updated, make_item(item.id, item.name, item.description, location, item.portable, item.examine, item.use_text))
        else
            append(updated, item)
        end if
        i = i + 1
    end while
    return updated
end function

function describe(area_id, areas, items)
    area_index = find_area_index(areas, area_id)
    if area_index = nothing then
        print("You are nowhere.")
        return
    end if
    area = areas[area_index]
    print(area.name)
    print(area.description)
    i = 0
    while i < len(items)
        item = items[i]
        if item.location = area_id then
            print(item.description)
        end if
        i = i + 1
    end while
    return
end function

function show_inventory(inventory, items)
    if len(inventory) = 0 then
        print("You are carrying nothing.")
    else
        names = []
        i = 0
        while i < len(inventory)
            j = 0
            while j < len(items)
                item = items[j]
                if item.id = inventory[i] then
                    append(names, item.name)
                end if
                j = j + 1
            end while
            i = i + 1
        end while
        print("You are carrying: " + join(names, ", "))
    end if
    return
end function

function play(title, areas, items, triggers)
    location = 1
    inventory = []
    print(title)
    print("")
    describe(location, areas, items)
    while true
        command(trimmed)= input(">")
        command(lowered)= command
        words(split)= command
        if len(words) = 0 then
            continue
        end if
        verb = words[0]
        noun = ""
        if len(words) > 1 then
            noun = words[1]
        end if
        if len(words) > 2 then
            noun = noun + " " + words[2]
        end if
        direction = ""
        if verb = "go" then
            direction = noun
        end if
        if verb = "north" or verb = "south" or verb = "east" or verb = "west" or verb = "up" or verb = "down" then
            direction = verb
        end if
        handled = false
        if verb = "help" then
            print("Commands: look, north, south, east, west, up, down, go DIR, take ITEM, drop ITEM, examine ITEM, use ITEM, inventory, help, quit")
            handled = true
        end if
        if verb = "look" then
            describe(location, areas, items)
            handled = true
        end if
        if verb = "inventory" then
            show_inventory(inventory, items)
            handled = true
        end if
        if verb = "take" then
            idx = find_item_index(items, noun)
            if idx = nothing then
                print("You cannot take that.")
            else
                item = items[idx]
                if item.location = location and item.portable then
                    append(inventory, item.id)
                    items = set_item_location(items, item.id, -1)
                    print("Taken.")
                else
                    print("You cannot take that.")
                end if
            end if
            handled = true
        end if
        if verb = "drop" then
            idx = find_item_index(items, noun)
            if idx = nothing then
                print("You are not carrying that.")
            else
                item = items[idx]
                inv_idx = find(inventory, item.id)
                if inv_idx = nothing then
                    print("You are not carrying that.")
                else
                    remove(inventory, inv_idx)
                    items = set_item_location(items, item.id, location)
                    print("Dropped.")
                end if
            end if
            handled = true
        end if
        if verb = "examine" then
            idx = find_item_index(items, noun)
            if idx = nothing then
                print("You see nothing special.")
            else
                item = items[idx]
                if item.location = location or player_has(inventory, item.id) then
                    if item.examine = "" then
                        print("You see nothing special.")
                    else
                        print(item.examine)
                    end if
                else
                    print("You do not see that here.")
                end if
            end if
            handled = true
        end if
        if verb = "use" then
            idx = find_item_index(items, noun)
            if idx = nothing then
                print("You cannot use that.")
            else
                item = items[idx]
                if player_has(inventory, item.id) = false then
                    print("You are not carrying that.")
                else
                    used = false
                    i = 0
                    while i < len(triggers)
                        trigger = triggers[i]
                        if trigger.item_id = item.id and trigger.area_id = location then
                            areas = set_exit(areas, trigger.from_area, trigger.direction, trigger.to_area)
                            print(trigger.message)
                            used = true
                        end if
                        i = i + 1
                    end while
                    if used = false then
                        if item.use_text = "" then
                            print("Nothing happens.")
                        else
                            print(item.use_text)
                        end if
                    end if
                end if
            end if
            handled = true
        end if
        if direction != "" then
            area = areas[find_area_index(areas, location)]
            next = exit_for(area, direction)
            if next = 0 then
                print("You cannot go that way.")
            else
                location = next
                describe(location, areas, items)
            end if
            handled = true
        end if
        if verb = "quit" then
            print("Goodbye.")
            break
        end if
        if handled = false then
            print("I do not understand that command.")
        end if
    end while
    return
end function

function build_areas()
    areas = []
    append(areas, make_area(1, "Cellar", "A cold stone cellar.", 2, 0, 0, 0, 0, 0))
    append(areas, make_area(2, "Garden", "A walled garden with a locked gate.", 0, 1, 0, 0, 0, 0))
    append(areas, make_area(3, "Tunnel", "A low tunnel beyond the gate.", 0, 2, 0, 0, 0, 0))
    return areas
end function

function build_items()
    items = []
    append(items, make_item(1, "brass key", "A brass key lies here.", 1, true, "The key is old but sturdy.", ""))
    return items
end function

function build_triggers()
    triggers = []
    append(triggers, make_trigger(1, 2, "open", 2, "north", 3, "The key turns. The gate opens."))
    return triggers
end function

play("The Lantern Room", build_areas(), build_items(), build_triggers())
