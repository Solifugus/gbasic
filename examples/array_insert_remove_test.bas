items = ["a", "c"]
insert(items, 1, "b")
print(join(items, ","))

items = ["a", "b", "c"]
remove(items, 1)
print(join(items, ","))

items = ["a", "c"]
items = insert(items, 1, "b")
items = remove(items, 0)
print(join(items, ","))

items = ["a", "b"]
insert(items, len(items), "c")
print(join(items, ","))

function without_first(values)
    remove(values, 0)
    return values
end function

base = ["x", "y"]
changed = without_first(base)
print(join(base, ","))
print(join(changed, ","))
