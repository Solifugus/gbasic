on error goto next

x = 0
hits = 0

watch(x)
    hits = hits + 1
    if hits > 1 then x = x + 1
end watch

x = 1

if error then
    print(error.message)
    print(error.code)
    print(error.source)
    print(error.line > 0)
    error.clear()
end if

on error stop

y = 0
y_hits = 0

watch(y)
    y_hits = y_hits + 1
end watch

y = 1
print(y_hits)
