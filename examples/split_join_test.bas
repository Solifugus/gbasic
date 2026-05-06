print(trim("  joe jones  "))

words = split("apple banana orange")
print(len(words))
print(words[0])
print(words[1])
print(words[2])

csv = split("apple,banana,orange", ",")
print(csv[0])
print(csv[1])
print(csv[2])

print(join(words))
print(join(words, ", "))
