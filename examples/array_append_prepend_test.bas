items = ["banana"]
append(items, "orange")
print(len(items))
print(items[1])

items = ["banana"]
prepend(items, "apple")
print(items[0])

items = ["b"]
items = append(items, "c")
items = prepend(items, "a")
print(join(items, ","))

function add_tail(values)
    append(values, "c")
    return values
end function

base = ["a", "b"]
grown = add_tail(base)
print(join(base, ","))
print(join(grown, ","))
