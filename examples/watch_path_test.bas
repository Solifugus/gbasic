state = {
    value:"Ready",
    nested:{ text:"A" }
}

watch(state)
    print "root"
end watch

watch(state.value)
    print "value"
    print state.value
end watch

watch state.nested.text
    print "nested"
    print state.nested.text
end watch

state.value = "Busy"
state.nested.text = "B"
