state = {items:["b", "c"]}
events = []

watch state.items
    append(events, join(state.items, ","))
end watch

events = []

append_result = append(state.items, "d")
prepend_result = prepend(state.items, "a")
insert_result = insert(state.items, 2, "x")
remove_result = remove(state.items, 2)
first_result = take_first(state.items)
last_result = take_last(state.items)
reverse_result = reverse(state.items)
sort_result = sort(state.items)
append(state.items, "b")
unique_result = unique(state.items)
remove_value_result = remove_value(state.items, "b")

print(join(events, "|"))
print(count(events))
print(join(append_result, ","))
print(join(prepend_result, ","))
print(join(insert_result, ","))
print(join(remove_result, ","))
print(first_result)
print(last_result)
print(join(reverse_result, ","))
print(join(sort_result, ","))
print(join(unique_result, ","))
print(join(remove_value_result, ","))

before_noops = count(events)
reverse(state.items)
sort(state.items)
unique(state.items)
remove_value(state.items, "missing")
print(count(events) = before_noops)

root_items = ["m"]
root_hits = 0
watch(root_items)
    root_hits = root_hits + 1
end watch
root_hits = 0
insert(root_items, 1, "n")
print(root_hits)
