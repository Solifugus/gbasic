items = ["a", "b", "c"]
reverse(items)
print(join(items, ","))

items = ["a", "b", "c"]
items = reverse(items)
print(join(items, ","))

print(join(reverse(["a", "b", "c"]), ","))

empty = []
reverse(empty)
print(len(empty))

one = ["solo"]
reverse(one)
print(join(one, ","))

function reversed_copy(values)
    reverse(values)
    return values
end function

base = ["x", "y"]
changed = reversed_copy(base)
print(join(base, ","))
print(join(changed, ","))
