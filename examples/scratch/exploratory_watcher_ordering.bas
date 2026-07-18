a = 0
b = 0

watch(a)
    print("A1 a=" + string(a) + " b=" + string(b))
    b = a * 10
end watch

watch(a)
    print("A2 a=" + string(a) + " b=" + string(b))
end watch

watch(b)
    print("B b=" + string(b))
end watch

print("mutate a")
a = 1

without watchers
    a = 2
    b = 20
end without

print("after suppression a=" + string(a) + " b=" + string(b))

a = 3
