inventory = ["torch"]
inventory[0] = "lamp"
print(inventory[0])

items = [{name = "lamp", location = "cellar"}]
i = 0
items[i].location = "inventory"
print(items[0].location)

world = {
    rooms = [{name = "Hall", visited = false}]
}
index = 0
world.rooms[index].visited = true
if world.rooms[0].visited then
    print("visited")
end if

player = {
    inventory = [{name = "old key"}]
}
slot = 0
player.inventory[slot].name = "brass key"
print(player.inventory[0].name)

field = "name"
player.inventory[slot][field] = "silver key"
print(player.inventory[0].name)

player.inventory[slot].name{trimmed}= "  gold key  "
print(player.inventory[0].name)
