a = 0
b = 0
b_runs = 0

watch(a)
    print("A:" + string(a))
    b = b + 1
    b = b + 1
end watch

watch(b)
    b_runs = b_runs + 1
    print("B:" + string(b))
end watch

print("cascade")
a = 1
print("b_runs:" + string(b_runs))

print("top")
b = 5
b = 6
print("b_runs:" + string(b_runs))

x = 0
y = 0

watch(x)
    print("X1:" + string(x))
    y = x
end watch

watch(x)
    print("X2:" + string(x))
end watch

watch(y)
    print("Y:" + string(y))
end watch

print("order")
x = 1
