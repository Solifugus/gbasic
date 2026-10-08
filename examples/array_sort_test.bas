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

' ABSENCES SORT LAST and are NOT ordered against each other (ruled 2026-10-07).
' This used to assert `nothing` before `unknown`, an order the comparator invented
' between two different ways of having no value; the sort is stable, so equal means
' they keep the order they arrived in.
vals = [unknown, nothing, unknown]
vals = sort(vals)
print(len(vals))
if is_unknown(vals[0]) then
    print("entry order kept")
end if
if vals[1] = nothing then
    print("and nothing stayed second")
end if

' AND THEY GO AFTER EVERY ORDINARY VALUE, which is the half that matters: an
' absence is not a small number.
mixed = sort([3, unknown, 1, nothing])
print(join([string(mixed[0]), string(mixed[1]), string(mixed[2]), string(mixed[3])], ","))

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
