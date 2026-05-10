room = {
    name = "Hall",
    north = 2,
    south = 0
}

direction = "north"
print(room[direction])
room[direction] = 3
print(room.north)
print(room["secret"])
room["secret"] = 5
print(room.secret)

areas = [{name = "Hall", east = 1}, {name = "Cellar", east = 0}]
i = 0
direction = "east"
print(areas[i][direction])
areas[i][direction] = 4
print(areas[0].east)

world = {
    rooms = [{name = "Hall", north = 2}, {name = "Cellar", north = 0}]
}
i = 0
world.rooms[i][direction] = 3
print(world.rooms[0].east)
