' DOGFOOD 32: the likeliest absence bug a reader writes. `find_by` misses with
' `nothing`, so the absence travels to an index -- and the message used to name
' neither the value nor which absence it was.
people = [{ name: "ada" }]
who = find_by(people, "name", "grace")
print people[who]
