state = {
    items:["first"],
    changes:0
}

watch(state.items)
    state.changes = state.changes + 1
end watch

append(state.items, "second")
print(count(state.items))
print(state.items[1])

first = take_first(state.items)
print(first)
print(count(state.items))
print(state.changes)
