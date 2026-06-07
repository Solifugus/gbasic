nums = [3, 1, 2]
sort(nums)
print(nums[0])
print(nums[1])
print(nums[2])

words = ["banana", "apple", "orange"]
words = sort(words)
print(join(words, ","))

flags = [true, false, true]
flags = sort(flags)
print(flags[0])
print(flags[1])
print(flags[2])

vals = [unknown, nothing, unknown]
vals = sort(vals)
print(len(vals))
if vals[0] = nothing then
    print("nothing first")
end if
if is_unknown(vals[1]) then
    print("unknown second")
end if

print(join(sort(["b", "a", "c"]), ","))

empty = []
sort(empty)
print(len(empty))

one = ["solo"]
sort(one)
print(join(one, ","))

function sorted_copy(values)
    sort(values)
    return values
end function

base = ["b", "a"]
changed = sorted_copy(base)
print(join(base, ","))
print(join(changed, ","))
