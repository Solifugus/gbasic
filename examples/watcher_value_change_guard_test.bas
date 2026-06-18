number_value = 5
number_hits = 0
watch(number_value)
    number_hits = number_hits + 1
end watch
number_value = 5
number_value = 6
number_value = 6
print(number_hits)

text_value = "Ada"
text_hits = 0
watch(text_value)
    text_hits = text_hits + 1
end watch
text_value = "Ada"
text_value = "Grace"
text_value = "Grace"
print(text_hits)

flag_value = true
flag_hits = 0
watch(flag_value)
    flag_hits = flag_hits + 1
end watch
flag_value = true
flag_value = false
flag_value = false
print(flag_hits)

array_value = [1, "two", true]
array_hits = 0
watch(array_value)
    array_hits = array_hits + 1
end watch
array_value = [1, "two", true]
array_value = [1, "two", false]
array_value = [1, "two", false]
print(array_hits)

record_value = {name:"Ada", age:36}
record_hits = 0
watch(record_value)
    record_hits = record_hits + 1
end watch
record_value = {age:36, name:"Ada"}
record_value.name = "Ada"
record_value.age = 37
record_value.age = 37
print(record_hits)

nested = {
    info:{city:"London", active:true},
    items:[{name:"lamp", scores:[1, 2]}]
}
nested_hits = 0
watch(nested)
    nested_hits = nested_hits + 1
end watch
nested.info = {active:true, city:"London"}
nested.info.city = "London"
nested.items[0] = {scores:[1, 2], name:"lamp"}
nested.items[0].scores[1] = 3
nested.items[0].scores[1] = 3
print(nested_hits)
