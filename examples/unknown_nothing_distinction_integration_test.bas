x = unknown
y = nothing
print(x != y)
print(contains([x], y))
if is_unknown(x) and is_nothing(y) then
    print("distinct")
end if
