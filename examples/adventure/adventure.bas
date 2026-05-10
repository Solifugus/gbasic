function room(name, description, dark_description, north, south, east, west, needs_light)
    return {
        name = name,
        description = description,
        dark = dark_description,
        north = north,
        south = south,
        east = east,
        west = west,
        needs_light = needs_light
    }
end function

function item(name, location, description, read_text, needs_light)
    return {
        name = name,
        location = location,
        description = description,
        read_text = read_text,
        needs_light = needs_light
    }
end function

function find_room_index(rooms, name)
    return find_by(rooms, "name", name)
end function

function find_item_index(items, name)
    return find_by(items, "name", name)
end function

function player_has(inventory, item_name)
    return contains(inventory, item_name)
end function

function item_is_here(items, item_name, location)
    item_index = find_item_index(items, item_name)
    if item_index = nothing then
        return false
    end if
    current = items[item_index]
    return current.location = location
end function

function has_light(inventory)
    return player_has(inventory, "lamp")
end function

function room_exit(room, direction)
    next = room[direction]
    if next = unknown then
        return ""
    end if
    return next
end function

function set_item_location(items, item_name, location)
    i = 0
    while i < len(items)
        if items[i].name = item_name then
            items[i].location = location
        end if
        i = i + 1
    end while
    return items
end function

function parse_command(command)
    words(split)= command
    verb = ""
    noun = ""
    direction = ""

    if len(words) > 0 then
        verb = words[0]
    end if
    if len(words) > 1 then
        noun = join_from(words, 1, " ")
    end if
    if noun = "key" then
        noun = "brass key"
    end if

    if verb = "go" then
        direction = noun
    else
        if verb = "north" or verb = "south" or verb = "east" or verb = "west" then
            direction = verb
        end if
    end if

    return {
        verb = verb,
        noun = noun,
        direction = direction
    }
end function

function show_help()
    print("Commands: look, north, south, east, west, go north, take ITEM, drop ITEM, read ITEM, inventory, help, quit")
    return
end function

function show_inventory(inventory)
    if len(inventory) = 0 then
        print("You are carrying nothing.")
    else
        print("You are carrying: " + join(inventory, ", "))
    end if
    return
end function

function describe_room(location, rooms, items, inventory, gate_unlocked)
    room_index = find_room_index(rooms, location)
    if room_index = nothing then
        print("You are nowhere.")
        return
    end if

    current_room = rooms[room_index]
    light = has_light(inventory)

    if current_room.needs_light and light = false then
        print(current_room.dark)
        return
    end if

    print(current_room.description)

    if location = "garden" then
        if gate_unlocked then
            print("The iron gate to the north stands open.")
        else
            print("A locked iron gate blocks the path north.")
        end if
    end if

    i = 0
    while i < len(items)
        current_item = items[i]
        if current_item.location = location then
            if current_item.needs_light and light = false then
                nothing_to_show = true
            else
                print(current_item.description)
            end if
        end if
        i = i + 1
    end while

    return
end function

function take_item(items, inventory, item_name, location)
    item_index = find_item_index(items, item_name)
    if item_index = nothing then
        print("You cannot take that.")
        return {
            items = items,
            inventory = inventory
        }
    end if

    current_item = items[item_index]
    if current_item.location != location then
        print("You do not see a " + item_name + " here.")
        return {
            items = items,
            inventory = inventory
        }
    end if

    if current_item.needs_light and player_has(inventory, "lamp") = false then
        print("It is too dark to find the " + item_name + ".")
        return {
            items = items,
            inventory = inventory
        }
    end if

    append(inventory, item_name)
    items = set_item_location(items, item_name, "inventory")
    print("Taken.")
    return {
        items = items,
        inventory = inventory
    }
end function

function drop_item(items, inventory, item_name, location)
    item_index = find(inventory, item_name)
    if item_index = nothing then
        print("You are not carrying that.")
        return {
            items = items,
            inventory = inventory
        }
    end if

    remove(inventory, item_index)
    items = set_item_location(items, item_name, location)
    print("Dropped.")
    return {
        items = items,
        inventory = inventory
    }
end function

function read_item(items, inventory, item_name, location)
    item_index = find_item_index(items, item_name)
    if item_index = nothing then
        print("There is nothing useful to read.")
        return
    end if

    current_item = items[item_index]
    available = player_has(inventory, item_name) or current_item.location = location
    if available = false then
        print("You do not see a " + item_name + " here.")
        return
    end if

    if current_item.needs_light and player_has(inventory, "lamp") = false then
        print("It is too dark to read here.")
        return
    end if

    if current_item.read_text = "" then
        print("There is nothing useful to read.")
    else
        print(current_item.read_text)
    end if
    return
end function

function move_player(location, direction, rooms, inventory, gate_unlocked)
    room_index = find_room_index(rooms, location)
    if room_index = nothing then
        print("You cannot go that way.")
        return {
            location = location,
            gate_unlocked = gate_unlocked,
            moved = false
        }
    end if

    current_room = rooms[room_index]
    next_location = room_exit(current_room, direction)

    if next_location = "" then
        if location = "tunnel" then
            print("The collapsed tunnel blocks the way.")
        else
            print("You cannot go that way.")
        end if
        return {
            location = location,
            gate_unlocked = gate_unlocked,
            moved = false
        }
    end if

    if location = "garden" and direction = "north" and gate_unlocked = false then
        if player_has(inventory, "brass key") then
            gate_unlocked = true
            print("The brass key turns. The gate opens.")
        else
            print("The iron gate is locked.")
            return {
                location = location,
                gate_unlocked = gate_unlocked,
                moved = false
            }
        end if
    end if

    return {
        location = next_location,
        gate_unlocked = gate_unlocked,
        moved = true
    }
end function

function build_rooms()
    rooms = []
    append(rooms, room("cellar", "The cellar is cold and low. Stone steps lead north.", "", "hall", "", "", "", false))
    append(rooms, room("hall", "You are in a narrow hall. The cellar lies south, the library north, the kitchen east, and the garden west.", "", "library", "cellar", "kitchen", "garden", false))
    append(rooms, room("library", "The lamp glows over rows of dusty books. A study door stands east.", "The library is too dark to read. Shelves loom like walls.", "", "hall", "study", "", true))
    append(rooms, room("kitchen", "The kitchen smells of cold ashes. A back window looks toward the garden.", "", "", "", "", "hall", false))
    append(rooms, room("garden", "Wet paths cross a moonlit garden. The hall is east.", "", "tunnel", "", "hall", "", false))
    append(rooms, room("study", "The study is cramped and dust-choked. A desk faces the library door to the west.", "The study is black. You need a light to search it.", "", "", "", "library", true))
    append(rooms, room("tunnel", "A brick tunnel runs under the garden. Damp air moves from the north, but the passage has collapsed.", "", "", "garden", "", "", false))
    return rooms
end function

function build_items()
    items = []
    append(items, item("lamp", "cellar", "A brass lamp rests beside a cracked barrel.", "", false))
    append(items, item("brass key", "kitchen", "A brass key hangs from a nail by the stove.", "", false))
    append(items, item("note", "study", "A folded note lies on the desk.", "The note says: The garden gate listens for brass, not force.", true))
    return items
end function

function play()
    rooms = build_rooms()
    items = build_items()
    location = "cellar"
    inventory = []
    gate_unlocked = false

    print("The Lantern Room")
    print("")
    describe_room(location, rooms, items, inventory, gate_unlocked)

    while true
        command(trimmed)= input(">")
        command(lowered)= command

        if len(command) = 0 then
            continue
        end if

        parsed = parse_command(command)
        verb = parsed.verb
        noun = parsed.noun
        direction = parsed.direction
        handled = false
        done = false

        consider verb
        if "help" then
            show_help()
            handled = true
        if "look" then
            describe_room(location, rooms, items, inventory, gate_unlocked)
            handled = true
        if "inventory" then
            show_inventory(inventory)
            handled = true
        if "read" then
            read_item(items, inventory, noun, location)
            handled = true
        if "take" then
            result = take_item(items, inventory, noun, location)
            items = result.items
            inventory = result.inventory
            handled = true
        if "drop" then
            result = drop_item(items, inventory, noun, location)
            items = result.items
            inventory = result.inventory
            handled = true
        if "quit" then
            print("Goodbye.")
            handled = true
            done = true
        end consider

        if direction != "" then
            result = move_player(location, direction, rooms, inventory, gate_unlocked)
            location = result.location
            gate_unlocked = result.gate_unlocked
            if result.moved then
                describe_room(location, rooms, items, inventory, gate_unlocked)
            end if
            handled = true
        end if

        if done then
            break
        end if

        if handled = false then
            print("I do not understand that command.")
        end if
    end while
    return
end function

play()
