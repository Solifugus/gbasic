state = {
    value:{inner:"A"},
    other:"O"
}
hits = []
state_runs = 0
value_runs = 0
inner_runs = 0
other_runs = 0
namespace_runs = 0

watch(state)
    state_runs = state_runs + 1
    append(hits, "state")
end watch

watch(state.value)
    value_runs = value_runs + 1
    append(hits, "value")
end watch

watch(state.value.inner)
    inner_runs = inner_runs + 1
    append(hits, "inner")
end watch

watch(state.other)
    other_runs = other_runs + 1
    append(hits, "other")
end watch

namespace = {value:"N"}
watch(namespace.value)
    namespace_runs = namespace_runs + 1
    append(hits, "namespace")
end watch

hits = []
state_runs = 0
value_runs = 0
inner_runs = 0
other_runs = 0
namespace_runs = 0

state.value.inner = "B"
print(join(hits, ","))
print(state_runs)
print(value_runs)
print(inner_runs)
print(other_runs)
print(namespace_runs)

hits = []
state.value = {inner:"C"}
print(join(hits, ","))
print(state_runs)
print(value_runs)
print(inner_runs)
print(other_runs)
print(namespace_runs)

hits = []
state.other = "P"
print(join(hits, ","))
print(state_runs)
print(value_runs)
print(inner_runs)
print(other_runs)
print(namespace_runs)

hits = []
state = {
    value:{inner:"D"},
    other:"Q"
}
print(join(hits, ","))
print(state_runs)
print(value_runs)
print(inner_runs)
print(other_runs)
print(namespace_runs)

hits = []
state = {
    value:{inner:"D"},
    other:"Q"
}
print(join(hits, ","))
print(state_runs)
print(value_runs)
print(inner_runs)
print(other_runs)
print(namespace_runs)

state.items = ["x"]
item_runs = 0
watch(state.items)
    item_runs = item_runs + 1
end watch
item_runs = 0
append(state.items, "y")
print(item_runs)
