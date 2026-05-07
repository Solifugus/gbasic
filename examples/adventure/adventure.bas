function describe(location, lamp_taken, inventory)
    if location = "cellar" then
        print("The cellar is cold and low. Stone steps lead north.")
        if lamp_taken = false then
            print("A brass lamp rests beside a cracked barrel.")
        end if
    end if

    if location = "hall" then
        print("You are in a narrow hall. The cellar lies south, and a library waits north.")
    end if

    if location = "library" then
        if find(inventory, "lamp") = nothing then
            print("The library is too dark to read. Shelves loom like walls.")
        end if
        if find(inventory, "lamp") != nothing then
            print("The lamp glows over rows of dusty books. One title reads The Lantern Room.")
        end if
    end if

    return
end function

function show_inventory(inventory)
    if len(inventory) = 0 then
        print("You are carrying nothing.")
    end if
    if len(inventory) != 0 then
        print("You are carrying: " + join(inventory, ", "))
    end if
    return
end function

function play()
    location = "cellar"
    lamp_taken = false
    inventory = []

    print("The Lantern Room")
    print("")
    describe(location, lamp_taken, inventory)

    while true
        command(trimmed)= input(">")
        words(split)= command
        if len(words) != 0 then
            verb = words[0]
            noun = ""
            if len(words) > 1 then
                noun = words[1]
            end if

            handled = false

            if verb = "look" then
                describe(location, lamp_taken, inventory)
                handled = true
            end if

            if verb = "inventory" then
                show_inventory(inventory)
                handled = true
            end if

            if verb = "take" and noun = "lamp" then
                if location = "cellar" and lamp_taken = false then
                    append(inventory, "lamp")
                    lamp_taken = true
                    print("Taken.")
                    handled = true
                end if
                if handled = false then
                    print("You do not see a lamp here.")
                    handled = true
                end if
            end if

            if verb = "go" and noun = "north" then
                if location = "cellar" then
                    location = "hall"
                    describe(location, lamp_taken, inventory)
                    handled = true
                end if
                if location = "hall" and handled = false then
                    location = "library"
                    describe(location, lamp_taken, inventory)
                    handled = true
                end if
                if handled = false then
                    print("You cannot go north from here.")
                    handled = true
                end if
            end if

            if verb = "go" and noun = "south" then
                if location = "library" then
                    location = "hall"
                    describe(location, lamp_taken, inventory)
                    handled = true
                end if
                if location = "hall" and handled = false then
                    location = "cellar"
                    describe(location, lamp_taken, inventory)
                    handled = true
                end if
                if handled = false then
                    print("You cannot go south from here.")
                    handled = true
                end if
            end if

            if verb = "quit" then
                print("Goodbye.")
                return
            end if

            if handled = false then
                print("I do not understand that command.")
            end if
        end if
    end while
end function

play()
