items = ["a", "b", "a", "c", "b"]
unique(items)
print(join(items, ","))

nums = [1, 2, 1, 3, 2]
nums = unique(nums)
print(len(nums))
print(nums[0])
print(nums[1])
print(nums[2])

vals = [nothing, unknown, nothing, unknown]
vals = unique(vals)
print(len(vals))
if vals[0] = nothing then
    print("nothing")
end if
if vals[1] = unknown then
    print("unknown")
end if

print(join(unique(["x", "y", "x"]), ","))

function unique_copy(values)
    unique(values)
    return values
end function

base = ["a", "a", "b"]
changed = unique_copy(base)
print(join(base, ","))
print(join(changed, ","))
