function get_room()
    return {north = 1}
end function

direction = "north"
get_room()[direction] = 2
