inventory = ["lamp", "key"]
print(contains(inventory, "lamp"))
print(contains(inventory, "coin"))
empty = []
print(contains(empty, "lamp"))

remove_value(inventory, "lamp")
print(len(inventory))
print(inventory[0])
remove_value(inventory, "coin")
print(len(inventory))
print(inventory[0])

items = [{name = "lamp", location = "cellar"}, {name = "key", location = "kitchen"}]
print(find_by(items, "name", "key"))
print(find_by(items, "missing", "key"))
print(find_by(items, "name", "coin"))

words = ["take", "brass", "key"]
print(join_from(words, 0, " "))
print(join_from(words, 1, " "))
print(join_from(words, 5, " "))

print(first(words))
print(first(empty))

tail = rest(words)
print(len(tail))
print(tail[0])
print(tail[1])

empty_tail = rest(empty)
print(len(empty_tail))
one_tail = rest(["solo"])
print(len(one_tail))
