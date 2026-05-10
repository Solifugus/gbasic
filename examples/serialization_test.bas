print(encode(42))
print(encode("quote: \"yes\" \\ ok"))
print(encode(true))
print(encode(false))
print(encode(nothing))
print(encode(unknown))
print(encode(""))

primitive = decode("[1,\"two\",true,false,nothing,unknown]")
print(primitive[0])
print(primitive[1])
print(primitive[2])
print(primitive[3])
print(primitive[4])
print(primitive[5])

original = {
    name = "Cellar",
    exits = {north = 2, south = 0},
    items = ["lamp", "note"],
    description = "Line one.
Line two with \"quote\" and \\slash."
}

copy = decode(encode(original))
print(copy.name)
print(copy.exits.north)
print(copy.exits.south)
print(copy.items[0])
print(copy.items[1])
print(copy.description)

project = {
    title = "The Lantern Room",
    areas = [{name = "Hall", north = 2}],
    items = [{name = "lamp", location = "cellar"}],
    triggers = []
}

loaded = decode(encode(project))
print(loaded.title)
print(loaded.areas[0].name)
print(loaded.areas[0].north)
print(loaded.items[0].location)
print(len(loaded.triggers))
