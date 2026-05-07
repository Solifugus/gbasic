items = ["a", "b", "c"]
first = take_first(items)
print(first)
print(join(items, ","))

items = ["a", "b", "c"]
last = take_last(items)
print(last)
print(join(items, ","))

values = [nothing, "x"]
first = take_first(values)
if first = nothing then
    print("nothing taken")
end if
print(join(values, ","))

function take_copy(values)
    return take_last(values)
end function

base = ["a", "b"]
taken = take_copy(base)
print(taken)
print(join(base, ","))
