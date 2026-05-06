print(find("banana", "na"))

if find("banana", "zz") = nothing then
    print("missing")
end if

print(find("banana", ""))

items = ["apple", "banana", "orange"]
print(find(items, "banana"))

if find(items, "pear") = nothing then
    print("missing array")
end if
