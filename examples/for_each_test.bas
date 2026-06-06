items = ["lamp", "key", "coin"]

print("Testing for each:")
for each item in items
    print(item)
end for

print("Testing old for in syntax:")
for item in items
    print(item)
end for

print("Testing record iteration:")
player = {name:"Ada", score:10}

for each key in keys(player)
    print(key + ": " + string(player[key]))
end for

print("Testing values iteration:")
for each value in values(player)
    print("Value: " + string(value))
end for

print("Testing break and continue:")
numbers = [1, 2, 3, 4, 5]

for each num in numbers
    if num = 3 then
        continue
    end if
    if num = 5 then
        break
    end if
    print("Number: " + string(num))
end for
print("Done")
